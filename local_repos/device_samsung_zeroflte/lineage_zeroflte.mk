#
# Copyright (C) 2020 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

DEVICE_PATH := device/samsung/zeroflte

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# RisingOS-Revived flags (Core GApps + 3GB RAM & 2979 MiB /system size fit)
RISING_MAINTAINER := eiarf
WITH_GMS ?= true
TARGET_USES_PICO_GAPPS ?= true
ifeq ($(WITH_GMS),true)
RISING_PACKAGE_TYPE ?= CORE
else
RISING_PACKAGE_TYPE ?= VANILLA
endif
TARGET_DEFAULT_PIXEL_LAUNCHER := false
TARGET_PREBUILT_LAWNCHAIR_LAUNCHER := false
TARGET_ENABLE_BLUR := false
TARGET_FACE_UNLOCK_SUPPORTED := false
TARGET_SUPPORTS_QUICK_TAP := false
TARGET_INCLUDE_VIPERFX := false
WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true

# Size optimizations for 3GB system partition (2979 MiB physical limit)
TARGET_EXCLUDE_CUSTOM_FONTS := false
TARGET_SLIM_SYSTEM_EXT := true
DONT_DEXPREOPT_PREBUILTS := true
WITH_DEXPREOPT_DEBUG_INFO := false


# RisingOS About Phone hardware info
RISING_RAM := 3GB
RISING_STORAGE := 32GB / 64GB
RISING_BATTERY := 2550mAh
RISING_DISPLAY := 1440x2560

# Inherit from local device
$(call inherit-product, $(DEVICE_PATH)/device.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit common RisingOS / Lineage phone product
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Product identity (riseup zeroflte maps to lineage_zeroflte)
PRODUCT_NAME := lineage_zeroflte
PRODUCT_DEVICE := zeroflte
PRODUCT_MODEL := SM-G920F
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := samsung
PRODUCT_GMS_CLIENTID_BASE := android-samsung

# Use the latest approved GMS identifiers
PRODUCT_BUILD_PROP_OVERRIDES += \
	DeviceProduct=zerofltexx \
	BuildDesc="zerofltexx-user 7.0 NRD90M G920FXXU6EVG1 release-keys"

BUILD_FINGERPRINT := samsung/zerofltexx/zeroflte:7.0/NRD90M/G920FXXU6EVG1:user/release-keys
