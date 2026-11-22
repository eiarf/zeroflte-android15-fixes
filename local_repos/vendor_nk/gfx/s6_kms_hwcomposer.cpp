#define LOG_TAG "s6kms_hwc"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>

#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <drm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <cutils/properties.h>
#include "cros_gralloc_handle.h"

#define S6_GRALLOC_MAGIC 0x3141592
#define CROS_GRALLOC_MAGIC 0xABCDDCBAU
#define HWC_FB_CACHE_SIZE 16

struct private_handle_t {
    struct native_handle nativeHandle;
    int fd;
    int magic;
    int flags;
    int size;
    int offset;
    uint64_t base __attribute__((aligned(8)));
    int pid;
    int width;
    int height;
    int format;
    int stride;
};

struct dumb_buf {
    uint32_t handle;
    uint32_t fb_id;
    uint32_t size;
    uint32_t pitch;
    void *vaddr;
};

struct hwc_fb_cache_entry {
    uint32_t gem_handle;
    uint32_t fb_id;
    uint32_t drm_format;
    int width;
    int height;
    int stride;
    uint64_t last_used;
};

struct s6_hwc_device {
    hwc_composer_device_1_t base;
    int drm_fd;
    uint32_t crtc_id;
    uint32_t connector_id;
    drmModeModeInfo mode;

    struct dumb_buf bufs[2];
    int current_buf;
    int has_drm;
    int crtc_active;
    int crtc_off_enabled;

    struct hwc_fb_cache_entry fb_cache[HWC_FB_CACHE_SIZE];
    uint64_t use_seq;
    uint32_t current_fb_id;
    uint32_t prev_fb_id;
    int zc_logged;
    int zc_failed_logged;

    const gralloc_module_t *gralloc;

    const hwc_procs_t *procs;
    pthread_t vsync_thread;
    volatile int vsync_enabled;
    volatile int vsync_running;
};

static void *vsync_thread_func(void *arg) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)arg;
    const uint64_t period_ns = 16666666ULL; // ~60 Hz

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t next_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    while (dev->vsync_running) {
        next_ns += period_ns;
        struct timespec req = { (time_t)(next_ns / 1000000000ULL), (long)(next_ns % 1000000000ULL) };
        while (dev->vsync_running && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL) == EINTR) {
        }

        // Re-sync if timer fell behind by more than 2 periods (e.g. suspend/resume)
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        if (now_ns > next_ns + 2 * period_ns) {
            next_ns = now_ns;
        }

        if (dev->vsync_enabled && dev->procs && dev->procs->vsync) {
            dev->procs->vsync(dev->procs, 0, next_ns);
        }
    }
    return NULL;
}

static void close_gem_handle(int drm_fd, uint32_t handle) {
    if (!handle)
        return;
    struct drm_gem_close creq;
    memset(&creq, 0, sizeof(creq));
    creq.handle = handle;
    ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &creq);
}

static void release_fb_slot(int drm_fd, struct hwc_fb_cache_entry *slot) {
    if (slot->fb_id) {
        drmModeRmFB(drm_fd, slot->fb_id);
    }
    if (slot->gem_handle) {
        close_gem_handle(drm_fd, slot->gem_handle);
    }
    memset(slot, 0, sizeof(*slot));
}

static uint32_t hal_format_to_drm(int format) {
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            // Window 0 on Exynos 7420 DECON has no background plane; XBGR8888 selects
            // 24BPP_xBGR ([R, G, B, X]) to match Android RGBA without stray alpha blend.
            return DRM_FORMAT_XBGR8888;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return DRM_FORMAT_XRGB8888;
        default:
            return 0;
    }
}

/*
 * Import a gralloc DMA-BUF PRIME FD directly into exynos-drm as a scanout FB.
 * Keying the cache by gem_handle (which the kernel keeps unique per dma_buf
 * within dev->drm_fd) avoids fd-number/inode reuse hazards and keeps scanout
 * FBs alive while they remain active on CRTC.
 */
static int extract_buffer_desc(buffer_handle_t raw_hnd, int *out_fd, int *out_w, int *out_h,
                               int *out_pixel_stride, uint32_t *out_pitch, uint32_t *out_offset,
                               uint32_t *out_drm_format, size_t *out_byte_size) {
    if (!raw_hnd)
        return 0;

    // 1. Check minigbm / cros_gralloc_handle (Android 17 AIDL allocator + mapper 5)
    const size_t min_cros_ints = (sizeof(struct cros_gralloc_handle) - sizeof(native_handle_t)) / sizeof(int) - DRV_MAX_FDS;
    if (raw_hnd->numFds >= 1 && raw_hnd->numInts >= (int)min_cros_ints) {
        cros_gralloc_handle_t ch = reinterpret_cast<cros_gralloc_handle_t>(raw_hnd);
        if (ch->magic == CROS_GRALLOC_MAGIC && ch->fds[0] >= 0) {
            uint32_t dfmt = hal_format_to_drm(ch->droid_format);
            if (!dfmt)
                return 0;
            *out_fd = ch->fds[0];
            *out_w = (int)ch->width;
            *out_h = (int)ch->height;
            *out_pixel_stride = (int)ch->pixel_stride;
            *out_pitch = ch->strides[0] ? ch->strides[0] : (ch->pixel_stride * 4U);
            *out_offset = ch->offsets[0];
            *out_drm_format = dfmt;
            *out_byte_size = (size_t)ch->total_size;
            return 1;
        }
    }

    // 2. Fallback check legacy S6PrivateHandle (s6_drm_gralloc)
    if (raw_hnd->numFds >= 1 && raw_hnd->numInts >= 10) {
        const struct private_handle_t *hnd = (const struct private_handle_t *)raw_hnd;
        if (hnd->magic == S6_GRALLOC_MAGIC && hnd->fd >= 0) {
            uint32_t dfmt = hal_format_to_drm(hnd->format);
            if (!dfmt)
                return 0;
            *out_fd = hnd->fd;
            *out_w = hnd->width;
            *out_h = hnd->height;
            *out_pixel_stride = hnd->stride;
            *out_pitch = (uint32_t)hnd->stride * 4U;
            *out_offset = (uint32_t)hnd->offset;
            *out_drm_format = dfmt;
            *out_byte_size = (hnd->size > 0) ? (size_t)hnd->size : 0;
            return 1;
        }
    }
    return 0;
}

static uint32_t s6_hwc_get_or_import_fb(struct s6_hwc_device *dev, buffer_handle_t raw_hnd) {
    int prime_fd = -1, width = 0, height = 0, stride = 0;
    uint32_t pitch = 0, offset = 0, drm_format = 0;
    size_t byte_size = 0;

    if (!extract_buffer_desc(raw_hnd, &prime_fd, &width, &height, &stride, &pitch, &offset, &drm_format, &byte_size))
        return 0;

    if (width != (int)dev->mode.hdisplay || height != (int)dev->mode.vdisplay || stride < width)
        return 0;

    uint32_t gem_handle = 0;
    if (drmPrimeFDToHandle(dev->drm_fd, prime_fd, &gem_handle) != 0 || gem_handle == 0) {
        if (!dev->zc_failed_logged) {
            ALOGW("drmPrimeFDToHandle failed (%s), falling back to memcpy", strerror(errno));
            dev->zc_failed_logged = 1;
        }
        return 0;
    }

    struct hwc_fb_cache_entry *empty_slot = NULL;
    struct hwc_fb_cache_entry *lru_slot = NULL;
    for (int i = 0; i < HWC_FB_CACHE_SIZE; i++) {
        struct hwc_fb_cache_entry *e = &dev->fb_cache[i];
        if (e->fb_id && e->gem_handle == gem_handle) {
            if (e->width == width && e->height == height &&
                e->stride == stride && e->drm_format == drm_format) {
                e->last_used = ++dev->use_seq;
                return e->fb_id;
            }
            drmModeRmFB(dev->drm_fd, e->fb_id);
            e->fb_id = 0;
            empty_slot = e;
            break;
        }
        if (!e->fb_id && !empty_slot) {
            empty_slot = e;
        } else if (e->fb_id && e->fb_id != dev->current_fb_id && e->fb_id != dev->prev_fb_id) {
            if (!lru_slot || e->last_used < lru_slot->last_used) {
                lru_slot = e;
            }
        }
    }

    struct hwc_fb_cache_entry *target = empty_slot ? empty_slot : lru_slot;
    if (!target)
        return 0;

    int owns_new_handle = (target->gem_handle != gem_handle);
    if (target->fb_id)
        release_fb_slot(dev->drm_fd, target);

    uint32_t fb_id = 0;
    uint32_t handles[4] = { gem_handle, 0, 0, 0 };
    uint32_t pitches[4] = { pitch, 0, 0, 0 };
    uint32_t offsets[4] = { offset, 0, 0, 0 };
    if (drmModeAddFB2(dev->drm_fd, (uint32_t)width, (uint32_t)height,
                      drm_format, handles, pitches, offsets, &fb_id, 0) != 0 || fb_id == 0) {
        if (!dev->zc_failed_logged) {
            ALOGW("Zero-copy drmModeAddFB2 failed (%s, fourcc=0x%08x pitch=%u), falling back to memcpy",
                  strerror(errno), drm_format, pitch);
            dev->zc_failed_logged = 1;
        }
        if (owns_new_handle) {
            close_gem_handle(dev->drm_fd, gem_handle);
        } else {
            target->gem_handle = 0;
        }
        return 0;
    }

    target->gem_handle = gem_handle;
    target->fb_id = fb_id;
    target->drm_format = drm_format;
    target->width = width;
    target->height = height;
    target->stride = stride;
    target->last_used = ++dev->use_seq;

    if (!dev->zc_logged) {
        ALOGI("Zero-copy DRM PRIME scanout active (%dx%d stride=%d pitch=%u fourcc=0x%08x fb_id=%u)",
              width, height, stride, pitch, drm_format, fb_id);
        dev->zc_logged = 1;
    }
    return fb_id;
}

static int s6_drm_page_flip(struct s6_hwc_device *dev, uint32_t fb_id) {
    if (!dev->crtc_active) {
        int rc = drmModeSetCrtc(dev->drm_fd, dev->crtc_id, fb_id, 0, 0, &dev->connector_id, 1, &dev->mode);
        if (rc == 0) {
            dev->crtc_active = 1;
            return 0;
        }
        return rc;
    }
    int ret = drmModePageFlip(dev->drm_fd, dev->crtc_id, fb_id, 0, NULL);
    for (int retry = 0; retry < 12 && (ret == -EBUSY || (ret < 0 && errno == EBUSY)); retry++) {
        usleep(1500);
        ret = drmModePageFlip(dev->drm_fd, dev->crtc_id, fb_id, 0, NULL);
    }
    if (ret != 0 && errno == EINVAL) {
        ret = drmModeSetCrtc(dev->drm_fd, dev->crtc_id, fb_id, 0, 0, &dev->connector_id, 1, &dev->mode);
    }
    return ret;
}

static int s6_hwc_prepare(hwc_composer_device_1_t *dev, size_t numDisplays, hwc_display_contents_1_t **displays) {
    (void)dev;
    if (!displays || numDisplays == 0 || !displays[0])
        return 0;

    hwc_display_contents_1_t *disp = displays[0];
    for (size_t i = 0; i < disp->numHwLayers; i++) {
        if (disp->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET)
            continue;
        disp->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
        disp->hwLayers[i].hints = 0;
    }
    return 0;
}

static int s6_hwc_set(hwc_composer_device_1_t *base_dev, size_t numDisplays, hwc_display_contents_1_t **displays) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)base_dev;
    if (!displays || numDisplays == 0 || !displays[0])
        return 0;

    hwc_display_contents_1_t *disp = displays[0];
    hwc_layer_1_t *fb_layer = NULL;

    for (size_t i = 0; i < disp->numHwLayers; i++) {
        if (disp->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            fb_layer = &disp->hwLayers[i];
            break;
        }
    }

    // Wait for GPU/RenderEngine to finish writing before scanout
    if (fb_layer && fb_layer->acquireFenceFd >= 0) {
        struct pollfd pfd = { fb_layer->acquireFenceFd, POLLIN, 0 };
        poll(&pfd, 1, 200);
        close(fb_layer->acquireFenceFd);
        fb_layer->acquireFenceFd = -1;
    }

    int flipped = 0;
    if (fb_layer && fb_layer->handle && dev->has_drm) {
        uint32_t zc_fb_id = s6_hwc_get_or_import_fb(dev, fb_layer->handle);
        if (zc_fb_id) {
            int ret = s6_drm_page_flip(dev, zc_fb_id);
            if (ret == 0) {
                dev->prev_fb_id = dev->current_fb_id;
                dev->current_fb_id = zc_fb_id;
                flipped = 1;
            } else if (!dev->zc_failed_logged) {
                ALOGW("Zero-copy drmModePageFlip failed (%d: %s), falling back to memcpy", ret, strerror(errno));
                dev->zc_failed_logged = 1;
            }
        }
    }

    if (!flipped && fb_layer && fb_layer->handle && dev->has_drm && dev->gralloc) {
        void *vaddr = NULL;
        int err = dev->gralloc->lock(dev->gralloc, fb_layer->handle,
                                     GRALLOC_USAGE_SW_READ_OFTEN,
                                     0, 0, dev->mode.hdisplay, dev->mode.vdisplay,
                                     &vaddr);
        if (err == 0 && vaddr) {
            struct dumb_buf *buf = &dev->bufs[dev->current_buf];
            if (buf->vaddr && buf->fb_id) {
                size_t copy_size = buf->size;
                int pfd = -1, pw = 0, ph = 0, ps = 0;
                uint32_t pp = 0, po = 0, pdf = 0;
                size_t hsz = 0;
                if (extract_buffer_desc(fb_layer->handle, &pfd, &pw, &ph, &ps, &pp, &po, &pdf, &hsz) &&
                    hsz > 0 && hsz < copy_size) {
                    copy_size = hsz;
                }
                memcpy(buf->vaddr, vaddr, copy_size);
                if (s6_drm_page_flip(dev, buf->fb_id) == 0) {
                    dev->prev_fb_id = dev->current_fb_id;
                    dev->current_fb_id = buf->fb_id;
                }
                dev->current_buf ^= 1;
            }
            dev->gralloc->unlock(dev->gralloc, fb_layer->handle);
        }
    }

    for (size_t i = 0; i < disp->numHwLayers; i++) {
        if (disp->hwLayers[i].acquireFenceFd >= 0) {
            close(disp->hwLayers[i].acquireFenceFd);
            disp->hwLayers[i].acquireFenceFd = -1;
        }
    }
    disp->retireFenceFd = -1;
    return 0;
}

static int s6_hwc_eventControl(hwc_composer_device_1_t *base_dev, int disp, int event, int enabled) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)base_dev;
    if (disp != HWC_DISPLAY_PRIMARY)
        return -EINVAL;

    if (event == HWC_EVENT_VSYNC) {
        dev->vsync_enabled = enabled ? 1 : 0;
        return 0;
    }
    return -EINVAL;
}

static int s6_hwc_setPowerMode(hwc_composer_device_1_t *dev, int disp, int mode);
static int s6_hwc_blank(hwc_composer_device_1_t *dev, int disp, int blank) {
    return s6_hwc_setPowerMode(dev, disp, blank ? HWC_POWER_MODE_OFF : HWC_POWER_MODE_NORMAL);
}

static int s6_hwc_query(hwc_composer_device_1_t *dev, int what, int *value) {
    (void)dev;
    switch (what) {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            *value = 0;
            return 0;
        case HWC_VSYNC_PERIOD:
            *value = 16666666;
            return 0;
        case HWC_DISPLAY_TYPES_SUPPORTED:
            *value = HWC_DISPLAY_PRIMARY_BIT;
            return 0;
        default:
            return -EINVAL;
    }
}

static void s6_hwc_registerProcs(hwc_composer_device_1_t *base_dev, hwc_procs_t const *procs) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)base_dev;
    dev->procs = procs;
}

static void s6_hwc_dump(hwc_composer_device_1_t *dev, char *buff, int buff_len) {
    (void)dev;
    snprintf(buff, buff_len, "S6 KMS Hardware Composer active\n");
}

static int s6_hwc_getDisplayConfigs(hwc_composer_device_1_t *dev, int disp, uint32_t *configs, size_t *numConfigs) {
    (void)dev;
    if (disp != HWC_DISPLAY_PRIMARY)
        return -EINVAL;
    if (*numConfigs > 0) {
        configs[0] = 0;
        *numConfigs = 1;
    }
    return 0;
}

static int s6_hwc_getDisplayAttributes(hwc_composer_device_1_t *base_dev, int disp, uint32_t config, const uint32_t *attributes, int32_t *values) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)base_dev;
    if (disp != HWC_DISPLAY_PRIMARY || config != 0)
        return -EINVAL;

    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch (attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = 16666666;
                break;
            case HWC_DISPLAY_WIDTH:
                values[i] = dev->mode.hdisplay ? dev->mode.hdisplay : 1440;
                break;
            case HWC_DISPLAY_HEIGHT:
                values[i] = dev->mode.vdisplay ? dev->mode.vdisplay : 2560;
                break;
            case HWC_DISPLAY_DPI_X:
                values[i] = 577000; // ~577 dpi
                break;
            case HWC_DISPLAY_DPI_Y:
                values[i] = 577000;
                break;
            default:
                values[i] = -1;
                break;
        }
    }
    return 0;
}

static int s6_hwc_getActiveConfig(hwc_composer_device_1_t *dev, int disp) {
    (void)dev;
    if (disp != HWC_DISPLAY_PRIMARY)
        return -EINVAL;
    return 0;
}

static int s6_hwc_setActiveConfig(hwc_composer_device_1_t *dev, int disp, int index) {
    (void)dev;
    if (disp != HWC_DISPLAY_PRIMARY || index != 0)
        return -EINVAL;
    return 0;
}

static int s6_hwc_setPowerMode(hwc_composer_device_1_t *base_dev, int disp, int mode) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)base_dev;
    if (!dev || disp != HWC_DISPLAY_PRIMARY || !dev->has_drm || dev->drm_fd < 0)
        return 0;

    if (mode == HWC_POWER_MODE_OFF) {
        if (dev->crtc_off_enabled && dev->crtc_active) {
            drmModeSetCrtc(dev->drm_fd, dev->crtc_id, 0, 0, 0, NULL, 0, NULL);
            dev->crtc_active = 0;
        }
    } else if (mode == HWC_POWER_MODE_NORMAL || mode == HWC_POWER_MODE_DOZE || mode == HWC_POWER_MODE_DOZE_SUSPEND) {
        if (!dev->crtc_active && dev->current_fb_id) {
            if (drmModeSetCrtc(dev->drm_fd, dev->crtc_id, dev->current_fb_id, 0, 0, &dev->connector_id, 1, &dev->mode) == 0) {
                dev->crtc_active = 1;
            }
        }
    }
    return 0;
}

static int s6_hwc_close(hw_device_t *dev) {
    struct s6_hwc_device *s6dev = (struct s6_hwc_device *)dev;
    if (s6dev) {
        s6dev->vsync_running = 0;
        pthread_join(s6dev->vsync_thread, NULL);
        if (s6dev->has_drm && s6dev->drm_fd >= 0) {
            for (int i = 0; i < HWC_FB_CACHE_SIZE; i++) {
                release_fb_slot(s6dev->drm_fd, &s6dev->fb_cache[i]);
            }
            for (int i = 0; i < 2; i++) {
                if (s6dev->bufs[i].vaddr)
                    munmap(s6dev->bufs[i].vaddr, s6dev->bufs[i].size);
                if (s6dev->bufs[i].fb_id)
                    drmModeRmFB(s6dev->drm_fd, s6dev->bufs[i].fb_id);
            }
            close(s6dev->drm_fd);
        }
        free(s6dev);
    }
    return 0;
}

static int init_drm(struct s6_hwc_device *dev) {
    const char *nodes[] = { "/dev/dri/card0", "/dev/dri/card1", NULL };
    int fd = -1;
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;

    for (int i = 0; nodes[i]; i++) {
        fd = open(nodes[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            ALOGW("init_drm: open(%s) failed: %s", nodes[i], strerror(errno));
            continue;
        }

        drmSetMaster(fd);
        res = drmModeGetResources(fd);
        if (!res) {
            ALOGW("init_drm: drmModeGetResources failed on %s: %s", nodes[i], strerror(errno));
            close(fd);
            fd = -1;
            continue;
        }

        ALOGI("init_drm: %s crtcs=%d connectors=%d encoders=%d",
              nodes[i], res->count_crtcs, res->count_connectors, res->count_encoders);

        conn = NULL;
        for (int j = 0; j < res->count_connectors; j++) {
            conn = drmModeGetConnector(fd, res->connectors[j]);
            if (conn) {
                ALOGI("init_drm: %s conn[%d] id=%u type=%u conn=%d modes=%d",
                      nodes[i], j, conn->connector_id, conn->connector_type,
                      conn->connection, conn->count_modes);
                if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
                    break;
                drmModeFreeConnector(conn);
                conn = NULL;
            }
        }

        if (conn) {
            ALOGI("init_drm: found active connected connector on %s", nodes[i]);
            break;
        }

        ALOGW("init_drm: %s has %d connectors but none connected, trying next node", nodes[i], res->count_connectors);
        drmModeFreeResources(res);
        res = NULL;
        close(fd);
        fd = -1;
    }

    if (fd < 0 || !conn || !res) {
        // Fallback: on exynos-drm (card0-VGA-1), connector may report unknown/0 modes in sysfs
        // even when DECON + DSIM is active at 1440x2560. Try card0 conn[0] with 1440x2560 mode.
        fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            drmSetMaster(fd);
            res = drmModeGetResources(fd);
            if (res && res->count_connectors > 0 && res->count_crtcs > 0) {
                conn = drmModeGetConnector(fd, res->connectors[0]);
            }
        }
        if (fd < 0 || !res || !conn) {
            ALOGW("No connected DRM connector found on any card node");
            if (res) drmModeFreeResources(res);
            if (fd >= 0) close(fd);
            return -1;
        }
        ALOGW("init_drm: using fallback connector id=%u (conn=%d modes=%d) with 1440x2560@60Hz",
              conn->connector_id, conn->connection, conn->count_modes);
    }

    dev->drm_fd = fd;
    dev->connector_id = conn->connector_id;
    if (conn->count_modes > 0 && conn->modes) {
        dev->mode = conn->modes[0];
    } else {
        memset(&dev->mode, 0, sizeof(dev->mode));
        dev->mode.clock = 230000;
        dev->mode.hdisplay = 1440;
        dev->mode.hsync_start = 1448;
        dev->mode.hsync_end = 1456;
        dev->mode.htotal = 1464;
        dev->mode.vdisplay = 2560;
        dev->mode.vsync_start = 2568;
        dev->mode.vsync_end = 2576;
        dev->mode.vtotal = 2584;
        dev->mode.vrefresh = 60;
        dev->mode.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
        snprintf(dev->mode.name, sizeof(dev->mode.name), "1440x2560");
    }

    drmModeEncoder *enc = NULL;
    if (conn->encoder_id)
        enc = drmModeGetEncoder(fd, conn->encoder_id);
    if (enc && enc->crtc_id) {
        dev->crtc_id = enc->crtc_id;
    } else {
        dev->crtc_id = res->crtcs[0];
    }
    if (enc) drmModeFreeEncoder(enc);

    uint32_t width = dev->mode.hdisplay;
    uint32_t height = dev->mode.vdisplay;
    ALOGI("Configuring DRM: %ux%u on CRTC %u connector %u", width, height, dev->crtc_id, dev->connector_id);

    // Create 2 dumb buffers for double buffering
    for (int i = 0; i < 2; i++) {
        struct drm_mode_create_dumb creq;
        memset(&creq, 0, sizeof(creq));
        creq.width = width;
        creq.height = height;
        creq.bpp = 32;
        if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
            ALOGE("DRM_IOCTL_MODE_CREATE_DUMB failed: %s", strerror(errno));
            break;
        }

        uint32_t fb_id = 0;
        uint32_t handles[4] = { creq.handle, 0, 0, 0 };
        uint32_t pitches[4] = { creq.pitch, 0, 0, 0 };
        uint32_t offsets[4] = { 0, 0, 0, 0 };
        if (drmModeAddFB2(fd, width, height, DRM_FORMAT_XBGR8888,
                          handles, pitches, offsets, &fb_id, 0) != 0) {
            if (drmModeAddFB(fd, width, height, 24, 32, creq.pitch, creq.handle, &fb_id) != 0) {
                ALOGE("drmModeAddFB2/drmModeAddFB failed");
                break;
            }
        }

        struct drm_mode_map_dumb mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.handle = creq.handle;
        if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
            ALOGE("DRM_IOCTL_MODE_MAP_DUMB failed");
            break;
        }

        void *vaddr = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
        if (vaddr == MAP_FAILED) {
            ALOGE("mmap dumb buffer failed");
            break;
        }

        dev->bufs[i].handle = creq.handle;
        dev->bufs[i].fb_id = fb_id;
        dev->bufs[i].size = creq.size;
        dev->bufs[i].pitch = creq.pitch;
        dev->bufs[i].vaddr = vaddr;
    }

    if (dev->bufs[0].fb_id) {
        int ret = drmModeSetCrtc(fd, dev->crtc_id, dev->bufs[0].fb_id, 0, 0, &dev->connector_id, 1, &dev->mode);
        if (ret == 0) {
            dev->current_fb_id = dev->bufs[0].fb_id;
            dev->crtc_active = 1;
        }
        ALOGI("drmModeSetCrtc returned %d", ret);
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return 0;
}

static int s6_hwc_open(const struct hw_module_t *module, const char *name, struct hw_device_t **dev) {
    if (strcmp(name, HWC_HARDWARE_COMPOSER))
        return -EINVAL;

    struct s6_hwc_device *s6dev = (struct s6_hwc_device *)calloc(1, sizeof(struct s6_hwc_device));
    if (!s6dev)
        return -ENOMEM;

    s6dev->base.common.tag = HARDWARE_DEVICE_TAG;
    s6dev->base.common.version = HWC_DEVICE_API_VERSION_1_4;
    s6dev->base.common.module = (struct hw_module_t *)module;
    s6dev->base.common.close = s6_hwc_close;

    s6dev->base.prepare = s6_hwc_prepare;
    s6dev->base.set = s6_hwc_set;
    s6dev->base.eventControl = s6_hwc_eventControl;
    s6dev->base.blank = s6_hwc_blank;
    s6dev->base.query = s6_hwc_query;
    s6dev->base.registerProcs = s6_hwc_registerProcs;
    s6dev->base.dump = s6_hwc_dump;
    s6dev->base.getDisplayConfigs = s6_hwc_getDisplayConfigs;
    s6dev->base.getDisplayAttributes = s6_hwc_getDisplayAttributes;
    s6dev->base.getActiveConfig = s6_hwc_getActiveConfig;
    s6dev->base.setActiveConfig = s6_hwc_setActiveConfig;
    s6dev->base.setPowerMode = s6_hwc_setPowerMode;

    s6dev->mode.hdisplay = 1440;
    s6dev->mode.vdisplay = 2560;

    void *libhw = dlopen("libhardware.so", RTLD_NOW);
    if (libhw) {
        int (*dyn_hw_get_module)(const char *, const struct hw_module_t **) =
            (int (*)(const char *, const struct hw_module_t **))dlsym(libhw, "hw_get_module");
        if (dyn_hw_get_module) {
            hw_module_t const *gralloc_mod = NULL;
            if (dyn_hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &gralloc_mod) == 0) {
                s6dev->gralloc = (const gralloc_module_t *)gralloc_mod;
            }
        }
    }

    s6dev->crtc_off_enabled = property_get_bool("vendor.hwc.s6kms.crtc_off", 0) ? 1 : 0;
    if (init_drm(s6dev) == 0) {
        s6dev->has_drm = 1;
    }

    s6dev->vsync_running = 1;
    pthread_create(&s6dev->vsync_thread, NULL, vsync_thread_func, s6dev);

    *dev = &s6dev->base.common;
    ALOGI("s6kms Hardware Composer successfully opened!");
    return 0;
}

static struct hw_module_methods_t s6_hwc_module_methods = {
    .open = s6_hwc_open,
};

extern "C" hwc_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = HWC_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Samsung Exynos 7420 S6KMS Hardware Composer",
        .author = "Antigravity Porter",
        .methods = &s6_hwc_module_methods,
        .dso = NULL,
        .reserved = {0},
    }
};
