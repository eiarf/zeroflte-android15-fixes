#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-${ROM_DIR:-$HOME/rising17}}"
PATCH_ROOT="$BUNDLE_DIR/patches"
SKIP_GMS="${SKIP_GMS:-0}"

if [[ ! -d "$ROM_DIR/.repo" && ! -d "$ROM_DIR/build/make" ]]; then
    echo "error: ROM_DIR ($ROM_DIR) does not look like an Android source tree" >&2
    exit 1
fi

apply_patch() {
    local target_rel="$1"
    local patch_file="$2"
    local repo_dir="$ROM_DIR/$target_rel"

    if [[ ! -d "$repo_dir" ]]; then
        echo "[WARN] Target directory missing, skipping: $target_rel ($patch_file)"
        return 0
    fi

    if git -C "$repo_dir" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        echo "[SKIP] Already applied: $target_rel/$(basename "$patch_file")"
        return 0
    fi

    if ! git -C "$repo_dir" apply --check "$patch_file"; then
        echo "[FAIL] Patch conflict in $target_rel: $patch_file" >&2
        exit 1
    fi

    git -C "$repo_dir" apply "$patch_file"
    echo "[OK]   Applied: $target_rel/$(basename "$patch_file")"
}

echo "=== Applying platform patches into $ROM_DIR ==="

apply_patch "bionic"                                  "$PATCH_ROOT/bionic/0001-arc4random-fall-back-to-fork-detection-without-MADV_WIPEONFORK.patch"
apply_patch "build/make"                              "$PATCH_ROOT/build/make/0001-build-make-zeroflte-cache-size-and-releasetools.patch"
apply_patch "build/soong"                             "$PATCH_ROOT/build/soong/0001-fsgen-install-lib-and-lib64-copies-at-their-literal-path.patch"
apply_patch "external/minigbm"                        "$PATCH_ROOT/external/minigbm/0001-cros-gralloc-panfrost-simpledrm-card-nodes-aidl.patch"
apply_patch "frameworks/base"                         "$PATCH_ROOT/frameworks/base/0001-fontmanager-systemfonts-null-safe-and-watchdog-timeout.patch"
apply_patch "frameworks/native"                       "$PATCH_ROOT/frameworks/native/0001-gralloc2-gralloc5-renderengine-egl-loader.patch"
apply_patch "system/core"                             "$PATCH_ROOT/system/core/0001-init-selinux-permissive-for-universal7420.patch"
apply_patch "frameworks/opt/telephony"                "$PATCH_ROOT/frameworks/opt/telephony/0001-uicc-fall-back-to-the-SIM-records-ICCID.patch"
apply_patch "hardware/interfaces"                     "$PATCH_ROOT/hardware/interfaces/0001-graphics-composer-2.1-service-stays-on-dev-binder.patch"
apply_patch "hardware/samsung_slsi-linaro/exynos"     "$PATCH_ROOT/hardware/samsung_slsi-linaro/exynos/0001-ssp-keep-the-unused-StrongBox-code-out-of-the-build.patch"
apply_patch "hardware/samsung_slsi-linaro/exynos"     "$PATCH_ROOT/hardware/samsung_slsi-linaro/exynos/0002-exynos-keep-the-HIDL-dumpstate-and-unit-tests-out-of-the-build.patch"
apply_patch "system/tools/mkbootimg"                  "$PATCH_ROOT/system/tools/mkbootimg/0001-mkbootimg-add-support-for-dt.patch"
apply_patch "packages/apps/Settings"                  "$PATCH_ROOT/packages/apps/Settings/0001-settings-volume-slider-skip-missing-action-max-changed.patch"

if [[ "$SKIP_GMS" != "1" && -d "$ROM_DIR/vendor/gms" ]]; then
    apply_patch "vendor/gms"                          "$PATCH_ROOT/vendor/gms/0001-gms-pico-slim-for-3gb-ram-and-small-system.patch"
else
    echo "[SKIP] vendor/gms patch skipped (SKIP_GMS=$SKIP_GMS)"
fi

if [[ -d "$ROM_DIR/vendor/addons" ]]; then
    apply_patch "vendor/addons"                       "$PATCH_ROOT/vendor/addons/0001-addons-drop-optional-heavy-emoji-fonts-for-small-system.patch"
fi

if [[ -d "$ROM_DIR/vendor/rising" ]]; then
    apply_patch "vendor/rising"                       "$PATCH_ROOT/vendor/rising/0001-rising-slim-optional-prebuilts-for-small-system.patch"
fi

echo "=== All platform patches verified and applied ==="
