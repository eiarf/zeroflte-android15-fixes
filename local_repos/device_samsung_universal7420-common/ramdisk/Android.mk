LOCAL_PATH := $(call my-dir)

ifneq ($(filter noblelte nobleltecan nobleltedd nobleltedv nobleltejv nobleltektt nobleltelgt noblelteskt nobleltespr nobleltetmo nobleltextc nobleltezt zenlte zenltecan zenltedd zenltedv zenltejv zenltektt zenltelgt zenlteskt zenltetmo zenltexx zenltezt,$(TARGET_DEVICE)),)
NK_FSTAB_SRC := etc/fstab.samsungexynos7420.noble
else
NK_FSTAB_SRC := etc/fstab.samsungexynos7420.zero
endif

include $(CLEAR_VARS)
LOCAL_MODULE		:= fstab.samsungexynos7420
LOCAL_MODULE_STEM 	:= fstab.samsungexynos7420
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES		:= $(NK_FSTAB_SRC)
LOCAL_MODULE_PATH	:= $(TARGET_OUT_VENDOR_ETC)
include $(BUILD_PREBUILT)

# First-stage init reads its fstab only from a file; mount /system, /efs, /metadata.
include $(CLEAR_VARS)
LOCAL_MODULE		:= fstab.samsungexynos7420_ramdisk
LOCAL_MODULE_STEM 	:= fstab.samsungexynos7420
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES		:= $(NK_FSTAB_SRC)
LOCAL_MODULE_PATH	:= $(TARGET_RAMDISK_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE		:= init.recovery.samsungexynos7420.rc
LOCAL_MODULE_TAGS	:= optional
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES        := etc/init.recovery.samsungexynos7420.rc
LOCAL_MODULE_PATH	:= $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.samsung.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.samsung.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init/hw
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.baseband.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.baseband.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init/hw
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.gps.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.gps.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.sensorhub.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.sensorhub.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE		:= ueventd.samsungexynos7420.rc
LOCAL_MODULE_STEM   := ueventd.rc
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES		:= etc/ueventd.samsungexynos7420.rc
LOCAL_MODULE_PATH	:= $(TARGET_OUT_VENDOR_ETC)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := mobicore.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/mobicore.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.power.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.power.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init/hw
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE		:= init.samsungexynos7420.rc
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES		:= etc/init.samsungexynos7420.rc
LOCAL_MODULE_PATH	:= $(TARGET_OUT_VENDOR_ETC)/init/hw
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE		:= init.samsungexynos7420.usb.rc
LOCAL_MODULE_CLASS	:= ETC
LOCAL_SRC_FILES		:= etc/init.samsungexynos7420.usb.rc
LOCAL_MODULE_PATH	:= $(TARGET_OUT_VENDOR_ETC)/init/hw
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.nk-modem.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.nk-modem.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.nk-nomodem.rc
LOCAL_MODULE_CLASS      := ETC
LOCAL_SRC_FILES         := etc/init.nk-nomodem.rc
LOCAL_MODULE_PATH       := $(TARGET_OUT_VENDOR_ETC)/init
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.touch.sh
LOCAL_MODULE_CLASS      := EXECUTABLES
LOCAL_SRC_FILES         := bin/init.touch.sh
LOCAL_VENDOR_MODULE     := true
LOCAL_CHECK_ELF_FILES   := false
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := init.postboot.sh
LOCAL_MODULE_CLASS      := EXECUTABLES
LOCAL_SRC_FILES         := bin/init.postboot.sh
LOCAL_VENDOR_MODULE     := true
LOCAL_CHECK_ELF_FILES   := false
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE            := set_resolution.sh
LOCAL_MODULE_CLASS      := EXECUTABLES
LOCAL_SRC_FILES         := bin/set_resolution.sh
LOCAL_VENDOR_MODULE     := true
LOCAL_CHECK_ELF_FILES   := false
include $(BUILD_PREBUILT)
