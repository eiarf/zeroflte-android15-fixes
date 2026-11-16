#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

int main() {
    int card_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (card_fd < 0) { perror("open card0"); return 1; }

    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.width = 128;
    creq.height = 128;
    creq.bpp = 32;
    if (ioctl(card_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("CREATE_DUMB");
        return 1;
    }

    struct drm_prime_handle prime;
    memset(&prime, 0, sizeof(prime));
    prime.handle = creq.handle;
    prime.flags = DRM_CLOEXEC | DRM_RDWR;
    if (ioctl(card_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) < 0) {
        perror("PRIME_HANDLE_TO_FD");
        return 1;
    }

    // Destroy dumb handle immediately!
    struct drm_mode_destroy_dumb dreq;
    memset(&dreq, 0, sizeof(dreq));
    dreq.handle = creq.handle;
    ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    close(card_fd); // Close card_fd too!

    printf("Destroyed dumb handle and closed card_fd. Now testing prime.fd=%d...\n", prime.fd);

    void *vaddr = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, prime.fd, 0);
    if (vaddr == MAP_FAILED) {
        perror("mmap prime.fd after dumb destroy");
        return 1;
    }
    printf("mmap succeeded! vaddr=%p\n", vaddr);
    uint32_t *pixels = (uint32_t *)vaddr;
    pixels[0] = 0x12345678;
    printf("Readback: 0x%08X\n", pixels[0]);
    munmap(vaddr, creq.size);

    int pan_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (pan_fd >= 0) {
        struct drm_prime_handle pan_prime;
        memset(&pan_prime, 0, sizeof(pan_prime));
        pan_prime.fd = prime.fd;
        if (ioctl(pan_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &pan_prime) < 0) {
            perror("Panfrost import after dumb destroy");
        } else {
            printf("Panfrost imported handle=%u SUCCESS after dumb destroy!\n", pan_prime.handle);
            struct drm_gem_close gclose;
            gclose.handle = pan_prime.handle;
            gclose.pad = 0;
            ioctl(pan_fd, DRM_IOCTL_GEM_CLOSE, &gclose);
        }
        close(pan_fd);
    }
    close(prime.fd);
    printf("Clean exit!\n");
    return 0;
}
