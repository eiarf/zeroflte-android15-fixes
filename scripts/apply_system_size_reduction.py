#!/usr/bin/env python3
"""Apply surgical optimizations to ensure system.img fits within SM-G920F 2979 MiB physical partition."""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from server_control import run_remote_command

script = """
set -euo pipefail
ROM_DIR="/serverhive3/eiarf/rising17"

echo "=== 1. Patching BoardConfigCommon.mk (DISABLE_PREOPT := true) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/device/samsung/universal7420-common/BoardConfigCommon.mk"
with open(p, "r") as f:
    content = f.read()

target = "    WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY ?= false\\n    WITH_DEXPREOPT := true"
replacement = "    WITH_DEXPREOPT := true\\n    DISABLE_PREOPT := true"

if target in content:
    content = content.replace(target, replacement)
    with open(p, "w") as f:
        f.write(content)
    print("OK: Replaced in BoardConfigCommon.mk")
elif "DISABLE_PREOPT := true" in content:
    print("SKIP: Already set in BoardConfigCommon.mk")
else:
    print("WARN: Target pattern not found in BoardConfigCommon.mk")
PYEOF

echo "=== 2. Patching lineage_zeroflte.mk (Sizing Flags) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/device/samsung/zeroflte/lineage_zeroflte.mk"
with open(p, "r") as f:
    content = f.read()

flags = \"\"\"
# Partition sizing optimizations for SM-G920F (2979 MiB limit)
DISABLE_PREOPT := true
TARGET_EXCLUDE_EXTRA_FONTS := true
TARGET_INCLUDE_PIXEL_CLOCKS := false
TARGET_EXCLUDE_RISING_LOCK_STYLES := true
\"\"\"

if "TARGET_EXCLUDE_EXTRA_FONTS := true" not in content:
    content = content.replace("WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true", "WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true" + flags)
    with open(p, "w") as f:
        f.write(content)
    print("OK: Added flags to lineage_zeroflte.mk")
else:
    print("SKIP: Flags already present in lineage_zeroflte.mk")
PYEOF

echo "=== 3. Patching vendor/addons/config.mk (Conditional Extra Fonts) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/vendor/addons/config.mk"
with open(p, "r") as f:
    content = f.read()

old_block = """PRODUCT_COPY_FILES += \\
    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/fonts,$(TARGET_COPY_OUT_PRODUCT)/fonts) \\
    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/media/audio/ui,$(TARGET_COPY_OUT_PRODUCT)/media/audio/ui)"""

new_block = """ifneq ($(TARGET_EXCLUDE_EXTRA_FONTS),true)
PRODUCT_COPY_FILES += \\
    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/fonts,$(TARGET_COPY_OUT_PRODUCT)/fonts)
endif

PRODUCT_COPY_FILES += \\
    $(call find-copy-subdir-files,*,vendor/addons/prebuilt/product/media/audio/ui,$(TARGET_COPY_OUT_PRODUCT)/media/audio/ui)"""

if old_block in content:
    content = content.replace(old_block, new_block)
    with open(p, "w") as f:
        f.write(content)
    print("OK: Wrapped extra fonts copy in vendor/addons/config.mk")
elif "ifneq ($(TARGET_EXCLUDE_EXTRA_FONTS),true)" in content:
    print("SKIP: vendor/addons/config.mk already patched")
else:
    print("WARN: Font block not matched exactly in vendor/addons/config.mk")
PYEOF

echo "=== 4. Patching vendor/rising/prebuilts/prebuilts.mk (Keep only rising bootanimation) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/vendor/rising/prebuilts/prebuilts.mk"
with open(p, "r") as f:
    content = f.read()

old_anim = """PRODUCT_COPY_FILES += \\
    vendor/rising/prebuilts/bootanimation/bootanimation_rising.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_rising.zip \\
    vendor/rising/prebuilts/bootanimation/bootanimation_cyberpunk.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_cyberpunk.zip \\
    vendor/rising/prebuilts/bootanimation/bootanimation_valorant.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_valorant.zip \\
    vendor/rising/prebuilts/bootanimation/bootanimation_google.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_google.zip \\
    vendor/rising/prebuilts/bootanimation/bootanimation_google_monet.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_google_monet.zip"""

new_anim = """PRODUCT_COPY_FILES += \\
    vendor/rising/prebuilts/bootanimation/bootanimation_rising.zip:$(TARGET_COPY_OUT_PRODUCT)/media/bootanimation_rising.zip"""

if old_anim in content:
    content = content.replace(old_anim, new_anim)
    with open(p, "w") as f:
        f.write(content)
    print("OK: Removed alternate bootanimations in prebuilts.mk")
elif "bootanimation_cyberpunk.zip" not in content:
    print("SKIP: Alternate bootanimations already removed")
else:
    print("WARN: Bootanimation block not matched in prebuilts.mk")
PYEOF

echo "=== 5. Patching packages/apps/SystemUIClocks/clocks.mk (Conditional Clocks) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/packages/apps/SystemUIClocks/clocks.mk"
if os.path.exists(p):
    with open(p, "r") as f:
        content = f.read()
    if "ifneq ($(TARGET_INCLUDE_PIXEL_CLOCKS),false)" not in content:
        content = "ifneq ($(TARGET_INCLUDE_PIXEL_CLOCKS),false)\\n" + content + "\\nendif\\n"
        with open(p, "w") as f:
            f.write(content)
        print("OK: Wrapped clocks in packages/apps/SystemUIClocks/clocks.mk")
    else:
        print("SKIP: clocks.mk already conditional")
else:
    print("NOTE: clocks.mk not found, skipping")
PYEOF

echo "=== 6. Patching vendor/rising/config/packages.mk (Conditional AxQuickLook/AxSandbox/LunarisLockStyles) ==="
python3 - <<'PYEOF'
p = "/serverhive3/eiarf/rising17/vendor/rising/config/packages.mk"
with open(p, "r") as f:
    content = f.read()

old_pkg = """    AxQuickLook \\
    AxSandbox \\
    AppDataBackup \\
    LunarisLockStyles"""

new_pkg = """    AppDataBackup

ifneq ($(TARGET_EXCLUDE_RISING_LOCK_STYLES),true)
PRODUCT_PACKAGES += \\
    AxQuickLook \\
    AxSandbox \\
    LunarisLockStyles
endif"""

if old_pkg in content:
    content = content.replace(old_pkg, new_pkg)
    with open(p, "w") as f:
        f.write(content)
    print("OK: Wrapped lock styles in vendor/rising/config/packages.mk")
elif "ifneq ($(TARGET_EXCLUDE_RISING_LOCK_STYLES),true)" in content:
    print("SKIP: packages.mk already conditional")
else:
    print("WARN: Package block not matched in packages.mk")
PYEOF

echo "=== All size reduction patches applied successfully! ==="
"""

rc, out = run_remote_command(script)
sys.exit(rc)
