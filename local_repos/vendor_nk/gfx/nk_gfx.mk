# vendor/nk/gfx/nk_gfx.mk
PRODUCT_SOONG_NAMESPACES += \
    vendor/nk/gfx

# Mesa Panfrost + S6 KMS HWComposer + minigbm gralloc0 (32+64)
PRODUCT_PACKAGES += \
    hwcomposer.s6kms \
    gralloc.minigbm \
    gralloc.default.32_symlink \
    gralloc.default.64_symlink \
    libdrm \
    libnk_c++_shared \
    libglapi \
    libgbm_mesa \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    panfrost_dri
