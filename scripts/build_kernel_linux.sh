#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-${ROM_DIR:-$HOME/rising17}}"
KERNEL_SRC="${KERNEL_SRC:-$ROM_DIR/kernel_src/SM_G92X}"
KERNEL_OUT="${KERNEL_OUT:-$KERNEL_SRC/out}"
JOBS="${JOBS:-$(nproc)}"
KSU_OFFLINE="${KSU_OFFLINE:-0}"

SM_G92X_REPO="${SM_G92X_REPO:-https://github.com/eiarf/SM_G92X.git}"
SM_G92X_BRANCH="${SM_G92X_BRANCH:-lineage-22.2-android15-6.6}"

# 1. Auto-detect clang / LLVM toolchain cleanly (AOSP prebuilts -> NDK -> system clang)
detect_llvm_bin() {
    if [[ -n "${LLVM_BIN:-}" && -x "$LLVM_BIN/clang" && -x "$LLVM_BIN/ld.lld" ]]; then
        echo "$LLVM_BIN"
        return 0
    fi
    local candidate=""
    for d in "$ROM_DIR"/prebuilts/clang/host/linux-x86/clang-r*/bin \
             "${ANDROID_NDK_HOME:-/nonexistent}"/toolchains/llvm/prebuilt/linux-x86_64/bin \
             "$HOME"/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin \
             /usr/lib/llvm-*/bin \
             /usr/bin; do
        if [[ -x "$d/clang" && -x "$d/ld.lld" ]]; then
            candidate="$d"
            break
        fi
    done
    if [[ -z "$candidate" ]]; then
        echo "error: no clang+ld.lld toolchain found. Set LLVM_BIN=/path/to/clang/bin" >&2
        exit 1
    fi
    echo "$candidate"
}

LLVM_BIN="$(detect_llvm_bin)"
export PATH="$LLVM_BIN:$PATH"
echo "=== Using LLVM toolchain: $LLVM_BIN ==="
"$LLVM_BIN/clang" --version | head -n1

# 2. Fetch or verify SM_G92X (Linux 6.6.144)
if [[ ! -d "$KERNEL_SRC/.git" ]]; then
    mkdir -p "$(dirname "$KERNEL_SRC")"
    echo "=== Cloning eiarf/SM_G92X ($SM_G92X_BRANCH) ==="
    git clone --depth=1 -b "$SM_G92X_BRANCH" "$SM_G92X_REPO" "$KERNEL_SRC"
fi

# 3. Inject SukiSU-Nova kernel/ (with offline-safe Kbuild & materialized include/uapi/)
KSU_SRC_BUNDLE="$BUNDLE_DIR/local_repos/sukisu_nova_kernel"
if [[ ! -f "$KSU_SRC_BUNDLE/Kbuild" || ! -f "$KSU_SRC_BUNDLE/include/uapi/ksu.h" ]]; then
    echo "error: sukisu_nova_kernel bundle incomplete at $KSU_SRC_BUNDLE" >&2
    exit 1
fi
echo "=== Syncing SukiSU-Nova into drivers/kernelsu ==="
rm -rf "$KERNEL_SRC/drivers/kernelsu"
mkdir -p "$KERNEL_SRC/drivers/kernelsu"
cp -a "$KSU_SRC_BUNDLE/." "$KERNEL_SRC/drivers/kernelsu/"
rm -rf "$KERNEL_SRC/drivers/kernelsu/.git"

# 4. Apply kernel patches (SukiSU-Nova config + exynos-drm defer fix, idempotent)
for KPATCH in "$BUNDLE_DIR/patches/kernel/"*.patch; do
    [ -f "$KPATCH" ] || continue
    if git -C "$KERNEL_SRC" apply --reverse --check "$KPATCH" >/dev/null 2>&1; then
        echo "[SKIP] Kernel patch already applied: $(basename "$KPATCH")"
    else
        git -C "$KERNEL_SRC" apply --check "$KPATCH"
        git -C "$KERNEL_SRC" apply "$KPATCH"
        echo "[OK]   Applied kernel patch: $(basename "$KPATCH")"
    fi
done

# 5. Merge gki_defconfig + zeroflte_gki.fragment and prepare out/
mkdir -p "$KERNEL_OUT"
COMMON_MAKE_ARGS=(
    -C "$KERNEL_SRC"
    "O=$KERNEL_OUT"
    "ARCH=arm64"
    "LLVM=1"
    "LLVM_IAS=1"
    "HOSTCC=gcc"
    "HOSTCXX=g++"
    "KSU_OFFLINE=$KSU_OFFLINE"
)

echo "=== Generating .config (gki_defconfig + zeroflte_gki.fragment) ==="
cp "$KERNEL_SRC/arch/arm64/configs/gki_defconfig" "$KERNEL_OUT/.config"
KCONFIG_CONFIG="$KERNEL_OUT/.config" \
    "$KERNEL_SRC/scripts/kconfig/merge_config.sh" -m -r -O "$KERNEL_OUT" \
    "$KERNEL_OUT/.config" \
    "$KERNEL_SRC/arch/arm64/configs/zeroflte_gki.fragment" >/dev/null
make "${COMMON_MAKE_ARGS[@]}" olddefconfig >/dev/null

for sym in CONFIG_KSU=y CONFIG_KSU_MANUAL_SU=y CONFIG_HEADERS_INSTALL=y CONFIG_CRYPTO_LZ4=y CONFIG_EROFS_FS=y CONFIG_EROFS_FS_ZIP=y CONFIG_MEDIA_PLATFORM_SUPPORT=y CONFIG_VIDEO_EXYNOS7_FLITE=m; do
    if ! grep -qx "$sym" "$KERNEL_OUT/.config"; then
        echo "error: expected kernel config symbol missing: $sym" >&2
        exit 1
    fi
done

# 6. Install UAPI headers first so SukiSU-Nova builds preinit/runner.elf cleanly
echo "=== Installing UAPI headers into out/usr/include ==="
make "${COMMON_MAKE_ARGS[@]}" headers_install

# 7. Generate dsh_build_id.h required by dsh_esslog
mkdir -p "$KERNEL_OUT/include/generated"
GIT_HEAD16="$(git -C "$KERNEL_SRC" rev-parse HEAD | cut -c1-16)"
printf '#define DSH_BUILD_ID 0x%sUL\n' "$GIT_HEAD16" > "$KERNEL_OUT/include/generated/dsh_build_id.h"
echo "=== DSH_BUILD_ID = 0x${GIT_HEAD16}UL ==="

# 8. Build kernel Image, zeroflte DTB, and loadable modules
echo "=== Building Linux 6.6 Image + zeroflte DTB + modules (-j$JOBS) ==="
make "${COMMON_MAKE_ARGS[@]}" -j"$JOBS" \
    Image exynos/exynos7420-zeroflte.dtb modules

IMAGE_RAW="$KERNEL_OUT/arch/arm64/boot/Image"
DTB_RAW="$KERNEL_OUT/arch/arm64/boot/dts/exynos/exynos7420-zeroflte.dtb"
if [[ ! -f "$IMAGE_RAW" || ! -f "$DTB_RAW" ]]; then
    echo "error: Image or DTB missing after kernel build" >&2
    exit 1
fi

# 9. Patch Image text_offset=0x205000 for Samsung S-Boot & generate DTBH v2 dt.img
OUT_IMAGE="$KERNEL_OUT/Image.sboot"
OUT_DTIMG="$KERNEL_OUT/dt.img"
cp -f "$IMAGE_RAW" "$OUT_IMAGE"
python3 "$BUNDLE_DIR/scripts/patch_kernel_image.py" "$OUT_IMAGE" --text-offset 0x205000
python3 "$BUNDLE_DIR/scripts/patch_kernel_image.py" "$OUT_IMAGE" --verify --text-offset 0x205000
python3 "$BUNDLE_DIR/scripts/mkdtbh.py" -d "$DTB_RAW" -o "$OUT_DTIMG"

# 10. Stage stripped modules
MOD_STAGE="$KERNEL_OUT/mod_stage"
rm -rf "$MOD_STAGE"
make "${COMMON_MAKE_ARGS[@]}" -j"$JOBS" modules_install \
    "INSTALL_MOD_PATH=$MOD_STAGE" INSTALL_MOD_STRIP=1 >/dev/null

PREBUILT_DIR="$ROM_DIR/device/samsung/universal7420-common/prebuilt"
if [[ ! -d "$PREBUILT_DIR" ]]; then
    echo "error: $PREBUILT_DIR not found! Run server_sync_and_setup.sh first or set ROM_DIR" >&2
    exit 1
fi

echo "=== Updating $PREBUILT_DIR with fresh Linux 6.6 + SukiSU-Nova artifacts ==="
cp -f "$OUT_IMAGE" "$PREBUILT_DIR/Image"
cp -f "$OUT_DTIMG" "$PREBUILT_DIR/dt.img"
rm -f "$PREBUILT_DIR/modules/"*.ko
mkdir -p "$PREBUILT_DIR/modules"
find "$MOD_STAGE/lib/modules" -name '*.ko' -exec cp -f {} "$PREBUILT_DIR/modules/" \;

KO_COUNT="$(ls -1 "$PREBUILT_DIR/modules/"*.ko | wc -l)"
echo "=== Kernel build COMPLETE ==="
echo "  Image:   $PREBUILT_DIR/Image ($(du -h "$PREBUILT_DIR/Image" | cut -f1))"
echo "  dt.img:  $PREBUILT_DIR/dt.img ($(du -h "$PREBUILT_DIR/dt.img" | cut -f1))"
echo "  Modules: $KO_COUNT .ko files in $PREBUILT_DIR/modules/"
