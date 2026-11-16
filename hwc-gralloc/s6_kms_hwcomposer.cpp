#define LOG_TAG "s6kms_hwc"

#include <errno.h>
#include <fcntl.h>
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
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

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

struct s6_hwc_device {
    hwc_composer_device_1_t base;
    int drm_fd;
    uint32_t crtc_id;
    uint32_t connector_id;
    drmModeModeInfo mode;

    struct dumb_buf bufs[2];
    int current_buf;
    int has_drm;

    const gralloc_module_t *gralloc;

    const hwc_procs_t *procs;
    pthread_t vsync_thread;
    volatile int vsync_enabled;
    volatile int vsync_running;
};

static void *vsync_thread_func(void *arg) {
    struct s6_hwc_device *dev = (struct s6_hwc_device *)arg;
    const uint64_t period_ns = 16666666ULL; // ~60 Hz

    while (dev->vsync_running) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        uint64_t next_ns = ((now_ns / period_ns) + 1) * period_ns;
        uint64_t sleep_ns = next_ns - now_ns;

        struct timespec req = { (time_t)(sleep_ns / 1000000000ULL), (long)(sleep_ns % 1000000000ULL) };
        nanosleep(&req, NULL);

        if (dev->vsync_enabled && dev->procs && dev->procs->vsync) {
            dev->procs->vsync(dev->procs, 0, next_ns);
        }
    }
    return NULL;
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

    if (fb_layer && fb_layer->handle && dev->has_drm && dev->gralloc) {
        struct private_handle_t *hnd = (struct private_handle_t *)fb_layer->handle;
        void *vaddr = NULL;
        int err = dev->gralloc->lock(dev->gralloc, fb_layer->handle,
                                     GRALLOC_USAGE_SW_READ_OFTEN,
                                     0, 0, dev->mode.hdisplay, dev->mode.vdisplay,
                                     &vaddr);
        if (err == 0 && vaddr) {
            struct dumb_buf *buf = &dev->bufs[dev->current_buf];
            if (buf->vaddr && buf->fb_id) {
                size_t copy_size = buf->size;
                if (hnd && hnd->size > 0 && (size_t)hnd->size < copy_size) {
                    copy_size = (size_t)hnd->size;
                }
                memcpy(buf->vaddr, vaddr, copy_size);
                drmModePageFlip(dev->drm_fd, dev->crtc_id, buf->fb_id, 0, NULL);
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

static int s6_hwc_blank(hwc_composer_device_1_t *dev, int disp, int blank) {
    (void)dev;
    (void)disp;
    (void)blank;
    return 0;
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

static int s6_hwc_setPowerMode(hwc_composer_device_1_t *dev, int disp, int mode) {
    (void)dev;
    (void)disp;
    (void)mode;
    return 0;
}

static int s6_hwc_close(hw_device_t *dev) {
    struct s6_hwc_device *s6dev = (struct s6_hwc_device *)dev;
    if (s6dev) {
        s6dev->vsync_running = 0;
        pthread_join(s6dev->vsync_thread, NULL);
        if (s6dev->has_drm && s6dev->drm_fd >= 0) {
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
    for (int i = 0; nodes[i]; i++) {
        fd = open(nodes[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) break;
    }
    if (fd < 0) {
        ALOGW("No DRM card node could be opened");
        return -1;
    }

    drmSetMaster(fd);
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        ALOGW("drmModeGetResources failed");
        close(fd);
        return -1;
    }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            break;
        if (conn) { drmModeFreeConnector(conn); conn = NULL; }
    }

    if (!conn) {
        ALOGW("No connected DRM connector found");
        drmModeFreeResources(res);
        close(fd);
        return -1;
    }

    dev->drm_fd = fd;
    dev->connector_id = conn->connector_id;
    dev->mode = conn->modes[0];

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
        if (drmModeAddFB(fd, width, height, 24, 32, creq.pitch, creq.handle, &fb_id) != 0) {
            ALOGE("drmModeAddFB failed");
            break;
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
