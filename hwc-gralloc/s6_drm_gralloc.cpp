#define LOG_TAG "s6_gralloc"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#include <cutils/native_handle.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifdef __cplusplus
struct private_handle_t : public native_handle {
#else
struct private_handle_t {
    struct native_handle nativeHandle;
#endif

    enum {
        PRIV_FLAGS_FRAMEBUFFER = 0x00000001
    };

    int     fd; // DRM PRIME DMA-BUF file descriptor

    int     magic;
    int     flags;
    int     size;
    int     offset;

    uint64_t base __attribute__((aligned(8)));
    int     pid;

    int     width;
    int     height;
    int     format;
    int     stride;

    static const int sMagic = 0x3141592;
    static const int sNumFds = 1;

    static inline int sNumInts() {
        return (((sizeof(private_handle_t) - sizeof(native_handle_t)) / sizeof(int)) - sNumFds);
    }

    private_handle_t(int fd, int size, int flags, int width, int height, int format, int stride) :
        fd(fd), magic(sMagic), flags(flags), size(size), offset(0),
        base(0), pid(getpid()), width(width), height(height), format(format), stride(stride)
    {
        version = sizeof(native_handle);
        numInts = sNumInts();
        numFds = sNumFds;
    }

    ~private_handle_t() {
        magic = 0;
    }

    static int validate(const native_handle* h) {
        const private_handle_t* hnd = (const private_handle_t*)h;
        if (!h || h->version != sizeof(native_handle) ||
            h->numInts != sNumInts() || h->numFds != sNumFds ||
            hnd->magic != sMagic)
        {
            ALOGE("invalid gralloc handle (at %p)", h);
            return -EINVAL;
        }
        return 0;
    }
};

static int gralloc_map(buffer_handle_t handle, void** vaddr) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    if (!hnd->base || hnd->pid != getpid()) {
        size_t size = hnd->size;
        void* mapped = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, hnd->fd, 0);
        if (mapped == MAP_FAILED) {
            ALOGE("gralloc_map: mmap failed for fd=%d size=%zu: %s", hnd->fd, size, strerror(errno));
            return -errno;
        }
        hnd->base = uintptr_t(mapped) + hnd->offset;
        hnd->pid = getpid();
    }
    if (vaddr) *vaddr = (void*)hnd->base;
    return 0;
}

static int gralloc_unmap(buffer_handle_t handle) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->base && hnd->pid == getpid()) {
        void* base = (void*)(hnd->base - hnd->offset);
        size_t size = hnd->size;
        if (munmap(base, size) < 0) {
            ALOGE("gralloc_unmap: munmap failed: %s", strerror(errno));
        }
        hnd->base = 0;
    }
    return 0;
}

static int gralloc_register_buffer(gralloc_module_t const* /*module*/, buffer_handle_t handle) {
    void* vaddr = NULL;
    return gralloc_map(handle, &vaddr);
}

static int gralloc_unregister_buffer(gralloc_module_t const* /*module*/, buffer_handle_t handle) {
    return gralloc_unmap(handle);
}

static int gralloc_lock(gralloc_module_t const* /*module*/,
                 buffer_handle_t handle, int /*usage*/,
                 int /*l*/, int /*t*/, int /*w*/, int /*h*/,
                 void** vaddr) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    if (!hnd->base || hnd->pid != getpid()) {
        int err = gralloc_map(handle, vaddr);
        if (err) return err;
    } else {
        if (vaddr) *vaddr = (void*)hnd->base;
    }
    return 0;
}

static int gralloc_unlock(gralloc_module_t const* /*module*/, buffer_handle_t handle) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;
    return 0;
}

static int gralloc_lock_ycbcr(gralloc_module_t const* /*module*/,
                       buffer_handle_t handle, int /*usage*/,
                       int /*l*/, int /*t*/, int w, int h,
                       struct android_ycbcr *ycbcr) {
    if (private_handle_t::validate(handle) < 0 || !ycbcr)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    if (!hnd->base || hnd->pid != getpid()) {
        void *vaddr = NULL;
        int err = gralloc_map(handle, &vaddr);
        if (err) return err;
    }

    uint8_t *base = (uint8_t *)hnd->base;
    int ystride = hnd->stride ? hnd->stride : w;
    int cstride = ystride / 2;

    ycbcr->y = base;
    ycbcr->ystride = ystride;

    if (hnd->format == HAL_PIXEL_FORMAT_YV12) {
        ycbcr->cr = (void*)(base + ystride * h);
        ycbcr->cb = (void*)((uint8_t*)ycbcr->cr + cstride * (h / 2));
        ycbcr->cstride = cstride;
        ycbcr->chroma_step = 1;
    } else {
        // NV12 / NV21 / default semiplanar
        ycbcr->cb = (void*)(base + ystride * h);
        ycbcr->cr = (void*)((uint8_t*)ycbcr->cb + 1);
        ycbcr->cstride = ystride;
        ycbcr->chroma_step = 2;
    }
    return 0;
}

static void wait_fence(int fenceFd) {
    if (fenceFd >= 0) {
        struct pollfd pfd;
        pfd.fd = fenceFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, -1);
        close(fenceFd);
    }
}

static int gralloc_lockAsync(struct gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr, int fenceFd) {
    wait_fence(fenceFd);
    return gralloc_lock(module, handle, usage, l, t, w, h, vaddr);
}

static int gralloc_unlockAsync(struct gralloc_module_t const* module,
        buffer_handle_t handle, int* fenceFd) {
    if (fenceFd) *fenceFd = -1;
    return gralloc_unlock(module, handle);
}

static int gralloc_lockAsync_ycbcr(struct gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        struct android_ycbcr *ycbcr, int fenceFd) {
    wait_fence(fenceFd);
    return gralloc_lock_ycbcr(module, handle, usage, l, t, w, h, ycbcr);
}

static int32_t gralloc_getTransportSize(struct gralloc_module_t const* /*module*/,
        buffer_handle_t handle, uint32_t *outNumFds, uint32_t *outNumInts) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;
    private_handle_t* hnd = (private_handle_t*)handle;
    if (outNumFds) *outNumFds = hnd->numFds;
    if (outNumInts) *outNumInts = hnd->numInts;
    return 0;
}

static int32_t gralloc_validateBufferSize(struct gralloc_module_t const* /*device*/,
        buffer_handle_t handle, uint32_t /*w*/, uint32_t /*h*/, int32_t /*format*/, int /*usage*/,
        uint32_t /*stride*/) {
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;
    return 0;
}

static int gralloc_alloc(alloc_device_t* /*dev*/,
        int width, int height, int format, int usage,
        buffer_handle_t* pHandle, int* pStride)
{
    if (!pHandle || !pStride)
        return -EINVAL;

    int bytesPerPixel = 4;
    int bpp = 32;
    int alloc_width = width;
    int alloc_height = height;

    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_FP16:
            bytesPerPixel = 8;
            bpp = 64;
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            bytesPerPixel = 4;
            bpp = 32;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            bytesPerPixel = 3;
            bpp = 24;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_RAW16:
            bytesPerPixel = 2;
            bpp = 16;
            break;
        case HAL_PIXEL_FORMAT_BLOB:
        case HAL_PIXEL_FORMAT_Y8:
            bytesPerPixel = 1;
            bpp = 8;
            alloc_width = width;
            alloc_height = 1;
            break;
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            bytesPerPixel = 1;
            bpp = 8;
            alloc_width = width;
            alloc_height = height * 3 / 2;
            break;
        default:
            bytesPerPixel = 4;
            bpp = 32;
            break;
    }

    int drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        ALOGE("gralloc_alloc: Failed to open /dev/dri/card0: %s", strerror(errno));
        return -errno;
    }

    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.width = alloc_width;
    creq.height = alloc_height;
    creq.bpp = bpp;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        ALOGE("gralloc_alloc: DRM_IOCTL_MODE_CREATE_DUMB failed (%dx%d bpp=%d): %s",
              alloc_width, alloc_height, bpp, strerror(errno));
        close(drm_fd);
        return -errno;
    }

    struct drm_prime_handle prime;
    memset(&prime, 0, sizeof(prime));
    prime.handle = creq.handle;
    prime.flags = DRM_CLOEXEC | DRM_RDWR;
    if (ioctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) < 0) {
        ALOGE("gralloc_alloc: DRM_IOCTL_PRIME_HANDLE_TO_FD failed: %s", strerror(errno));
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = creq.handle;
        ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        close(drm_fd);
        return -errno;
    }

    // Destroy dumb handle on card0; DMA-BUF reference keeps GEM object alive.
    struct drm_mode_destroy_dumb dreq;
    memset(&dreq, 0, sizeof(dreq));
    dreq.handle = creq.handle;
    ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    close(drm_fd);

    int stride = creq.pitch / bytesPerPixel;
    if (stride < width) stride = width;

    private_handle_t* hnd = new private_handle_t(
        prime.fd, creq.size, 0, width, height, format, stride);

    *pHandle = hnd;
    *pStride = stride;

    return 0;
}

static int gralloc_free(alloc_device_t* /*dev*/, buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = const_cast<private_handle_t*>(
        reinterpret_cast<private_handle_t const*>(handle));

    gralloc_unmap(hnd);

    if (hnd->fd >= 0) {
        close(hnd->fd);
        hnd->fd = -1;
    }

    delete hnd;
    return 0;
}

static int gralloc_close(struct hw_device_t *dev)
{
    alloc_device_t* alloc_dev = reinterpret_cast<alloc_device_t*>(dev);
    if (alloc_dev) {
        free(alloc_dev);
    }
    return 0;
}

static int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    if (strcmp(name, GRALLOC_HARDWARE_GPU0) == 0) {
        alloc_device_t *dev = (alloc_device_t*)malloc(sizeof(alloc_device_t));
        if (!dev) return -ENOMEM;

        memset(dev, 0, sizeof(*dev));
        dev->common.tag = HARDWARE_DEVICE_TAG;
        dev->common.version = 0;
        dev->common.module = const_cast<hw_module_t*>(module);
        dev->common.close = gralloc_close;

        dev->alloc = gralloc_alloc;
        dev->free  = gralloc_free;

        *device = &dev->common;
        return 0;
    }
    return -EINVAL;
}

static struct hw_module_methods_t gralloc_module_methods = {
    .open = gralloc_device_open
};

extern "C" struct gralloc_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = GRALLOC_MODULE_API_VERSION_0_3,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = GRALLOC_HARDWARE_MODULE_ID,
        .name = "Samsung Exynos 7420 DRM DMA-BUF Allocator Module",
        .author = "Antigravity",
        .methods = &gralloc_module_methods,
        .dso = NULL,
        .reserved = {0},
    },
    .registerBuffer = gralloc_register_buffer,
    .unregisterBuffer = gralloc_unregister_buffer,
    .lock = gralloc_lock,
    .unlock = gralloc_unlock,
    .perform = NULL,
    .lock_ycbcr = gralloc_lock_ycbcr,
    .lockAsync = gralloc_lockAsync,
    .unlockAsync = gralloc_unlockAsync,
    .lockAsync_ycbcr = gralloc_lockAsync_ycbcr,
    .getTransportSize = gralloc_getTransportSize,
    .validateBufferSize = gralloc_validateBufferSize,
    .reserved_proc = {NULL},
};
