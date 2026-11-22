#
# SPDX-FileCopyrightText: 2017 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Boot and recovery images in the layout the Galaxy S6 S-Boot reads: a
# version 0 header whose header-version field holds the size of the
# separate device-tree image (dt.img), placed after the second stage, and
# SEANDROIDENFORCE appended. Lineage 24 dropped this layout from
# hardware/samsung (mkbootimg.mk), build/make (--dt) and mkbootimg, so the
# device builds it here; patches/system/tools/mkbootimg brings back
# Lineage's --dt option. Based on hardware/samsung/mkbootimg.mk of
# Lineage 22.2, prebuilt-DT case only.

NK_DTIMAGE := $(PRODUCT_OUT)/dt.img

$(INSTALLED_BOOTIMAGE_TARGET): $(MKBOOTIMG) $(INTERNAL_BOOTIMAGE_FILES) $(BOOTIMAGE_EXTRA_DEPS) $(INSTALLED_KERNEL_TARGET) $(NK_DTIMAGE)
	$(call pretty,"Target boot image: $@")
	$(hide) $(MKBOOTIMG) --kernel $(call bootimage-to-kernel,$@) $(INTERNAL_BOOTIMAGE_ARGS) $(INTERNAL_MKBOOTIMG_VERSION_ARGS) $(BOARD_MKBOOTIMG_ARGS) --dt $(NK_DTIMAGE) --output $@
	$(hide) echo -n "SEANDROIDENFORCE" >> $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE))
	@echo "Made boot image: $@"

ifneq ($(INSTALLED_RECOVERYIMAGE_TARGET),)
$(INSTALLED_RECOVERYIMAGE_TARGET): $(MKBOOTIMG) $(recovery_ramdisk) $(recovery_kernel) $(RECOVERYIMAGE_EXTRA_DEPS) $(NK_DTIMAGE)
	@echo "----- Making recovery image ------"
	$(hide) $(MKBOOTIMG) $(INTERNAL_RECOVERYIMAGE_ARGS) $(INTERNAL_MKBOOTIMG_VERSION_ARGS) $(BOARD_MKBOOTIMG_ARGS) --dt $(NK_DTIMAGE) --output $@
	$(hide) echo -n "SEANDROIDENFORCE" >> $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE))
	@echo "Made recovery image: $@"
endif
