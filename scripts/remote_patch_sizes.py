import os
import sys

def patch_board_config_common():
    p = "/serverhive3/eiarf/rising17/device/samsung/universal7420-common/BoardConfigCommon.mk"
    with open(p, "r") as f:
        content = f.read()
    target = "    WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY ?= false\n    WITH_DEXPREOPT := true"
    replacement = "    WITH_DEXPREOPT := true\n    DISABLE_PREOPT := true"
    if target in content:
        content = content.replace(target, replacement)
        with open(p, "w") as f:
            f.write(content)
        print("OK: Replaced in BoardConfigCommon.mk")
    elif "DISABLE_PREOPT := true" in content:
        print("SKIP: Already set in BoardConfigCommon.mk")
    else:
        print("WARN: Target pattern not found in BoardConfigCommon.mk")

def patch_lineage_zeroflte():
    p = "/serverhive3/eiarf/rising17/device/samsung/zeroflte/lineage_zeroflte.mk"
    with open(p, "r") as f:
        content = f.read()
    flags = (
        "\n# Partition sizing optimizations for SM-G920F (2979 MiB limit)\n"
        "DISABLE_PREOPT := true\n"
        "TARGET_EXCLUDE_EXTRA_FONTS := true\n"
        "TARGET_INCLUDE_PIXEL_CLOCKS := false\n"
        "TARGET_EXCLUDE_RISING_LOCK_STYLES := true\n"
    )
    if "TARGET_EXCLUDE_EXTRA_FONTS := true" not in content:
        content = content.replace(
            "WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true",
            "WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true" + flags
        )
        with open(p, "w") as f:
            f.write(content)
        print("OK: Added flags to lineage_zeroflte.mk")
    else:
        print("SKIP: Flags already present in lineage_zeroflte.mk")

def patch_vendor_addons():
    p = "/serverhive3/eiarf/rising17/vendor/addons/config.mk"
    with open(p, "r") as f:
        content = f.read()
    old_block = (
        "PRODUCT_COPY_FILES += \\\n"
        "    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/fonts,$(TARGET_COPY_OUT_PRODUCT)/fonts) \\\n"
        "    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/media/audio/ui,$(TARGET_COPY_OUT_PRODUCT)/media/audio/ui)"
    )
    new_block = (
        "ifneq ($(TARGET_EXCLUDE_EXTRA_FONTS),true)\n"
        "PRODUCT_COPY_FILES += \\\n"
        "    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/fonts,$(TARGET_COPY_OUT_PRODUCT)/fonts)\n"
        "endif\n\n"
        "PRODUCT_COPY_FILES += \\\n"
        "    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/media/audio/ui,$(TARGET_COPY_OUT_PRODUCT)/media/audio/ui)"
    )
    if old_block in content:
        content = content.replace(old_block, new_block)
        with open(p, "w") as f:
            f.write(content)
        print("OK: Wrapped extra fonts copy in vendor/addons/config.mk")
    elif "ifneq ($(TARGET_EXCLUDE_EXTRA_FONTS),true)" in content:
        print("SKIP: vendor/addons/config.mk already patched")
    else:
        print("WARN: Font block not matched exactly in vendor/addons/config.mk")

def patch_rising_prebuilts():
    p = "/serverhive3/eiarf/rising17/vendor/rising/prebuilts/prebuilts.mk"
    with open(p, "r") as f:
        content = f.read()
    old_anim = (
        "PRODUCT_COPY_FILES += \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_rising.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_rising.zip \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_cyberpunk.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_cyberpunk.zip \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_valorant.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_valorant.zip \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_google.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_google.zip \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_google_monet.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_google_monet.zip"
    )
    new_anim = (
        "PRODUCT_COPY_FILES += \\\n"
        "    vendor/rising/prebuilts/bootanimation/bootanimation_rising.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_rising.zip"
    )
    if old_anim in content:
        content = content.replace(old_anim, new_anim)
        with open(p, "w") as f:
            f.write(content)
        print("OK: Removed alternate bootanimations in prebuilts.mk")
    elif "bootanimation_cyberpunk.zip" not in content:
        print("SKIP: Alternate bootanimations already removed")
    else:
        print("WARN: Bootanimation block not matched in prebuilts.mk")

def patch_systemui_clocks():
    p = "/serverhive3/eiarf/rising17/packages/apps/SystemUIClocks/clocks.mk"
    if os.path.exists(p):
        with open(p, "r") as f:
            content = f.read()
        if "ifneq ($(TARGET_INCLUDE_PIXEL_CLOCKS),false)" not in content:
            content = "ifneq ($(TARGET_INCLUDE_PIXEL_CLOCKS),false)\n" + content + "\nendif\n"
            with open(p, "w") as f:
                f.write(content)
            print("OK: Wrapped clocks in packages/apps/SystemUIClocks/clocks.mk")
        else:
            print("SKIP: clocks.mk already conditional")

def patch_rising_packages():
    p = "/serverhive3/eiarf/rising17/vendor/rising/config/packages.mk"
    with open(p, "r") as f:
        content = f.read()
    old_pkg = (
        "    AxQuickLook \\\n"
        "    AxSandbox \\\n"
        "    AppDataBackup \\\n"
        "    LunarisLockStyles"
    )
    new_pkg = (
        "    AppDataBackup\n\n"
        "ifneq ($(TARGET_EXCLUDE_RISING_LOCK_STYLES),true)\n"
        "PRODUCT_PACKAGES += \\\n"
        "    AxQuickLook \\\n"
        "    AxSandbox \\\n"
        "    LunarisLockStyles\n"
        "endif"
    )
    if old_pkg in content:
        content = content.replace(old_pkg, new_pkg)
        with open(p, "w") as f:
            f.write(content)
        print("OK: Wrapped lock styles in vendor/rising/config/packages.mk")
    elif "ifneq ($(TARGET_EXCLUDE_RISING_LOCK_STYLES),true)" in content:
        print("SKIP: packages.mk already conditional")
    else:
        print("WARN: Package block not matched in packages.mk")

if __name__ == "__main__":
    patch_board_config_common()
    patch_lineage_zeroflte()
    patch_vendor_addons()
    patch_rising_prebuilts()
    patch_systemui_clocks()
    patch_rising_packages()
    print("ALL PATCHES APPLIED.")
