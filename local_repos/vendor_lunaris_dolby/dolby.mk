#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DOLBY_PATH := vendor/lunaris/dolby

# Soong Namespace
PRODUCT_SOONG_NAMESPACES += \
    $(DOLBY_PATH)

# DAX default tuning config
PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/proprietary/odm/etc/dolby/multimedia_dolby_dax_default.xml:$(TARGET_COPY_OUT_ODM)/etc/dolby/multimedia_dolby_dax_default.xml

# LunarisDolby app + localized profile resources
PRODUCT_PACKAGES += \
    LunarisDolby \
    XiaomiDolbyResCommon

# Permissions
PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/configs/permissions/privapp-permissions-dolby.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp-permissions-dolby.xml

# Init script
PRODUCT_PACKAGES += \
    init.dolby.rc

# ODM HAL init rc
PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/proprietary/odm/etc/init/vendor.dolby_v3_6.hardware.dms360@2.0-service.rc:$(TARGET_COPY_OUT_ODM)/etc/init/vendor.dolby_v3_6.hardware.dms360@2.0-service.rc

# ODM HAL DAX v3.6 libs + 32-bit soundfx + vendor shared deps
PRODUCT_PACKAGES += \
    libstagefright_foundation-v33.vendor \
    libxml2.vendor \
    libsqlite.vendor \
    libdapparamstorage_v3_6 \
    libdeccfg_v3_6 \
    libdlbdsservice_v3_6 \
    vendor.dolby_v3_6.hardware.dms360@2.0 \
    vendor.dolby_v3_6.hardware.dms360@2.0-impl \
    libhwdap_v3_6 \
    libswdap_v3_6 \
    manifest_dax_dolby_v3_6.xml \
    vendor.dolby_v3_6.hardware.dms360@2.0-service

# VINTF compatibility matrix (optional HIDL dms360 v2.0)
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += \
    $(DOLBY_PATH)/vintf/dolby_framework_compatibility_matrix.xml
