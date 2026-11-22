#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-${ROM_DIR:-$HOME/rising17}}"
BUILD_TYPE="${BUILD_TYPE:-user}"
JOBS="${JOBS:-$(nproc)}"
WITH_GMS="${WITH_GMS:-true}"
CLEAN_MODE="${CLEAN_MODE:-none}"   # none | installclean | clean
MAX_SYSTEM_BYTES=3124019200       # SM-G920F /system partition = 762700 * 4096 (2979.3 MiB)

if [[ ! -f "$ROM_DIR/build/envsetup.sh" ]]; then
    echo "error: ROM_DIR ($ROM_DIR) missing build/envsetup.sh" >&2
    exit 1
fi

echo "=== Applying and verifying platform patches ==="
bash "$BUNDLE_DIR/scripts/apply_patches.sh" "$ROM_DIR"

PREBUILT_DIR="$ROM_DIR/device/samsung/universal7420-common/prebuilt"
python3 "$BUNDLE_DIR/scripts/patch_kernel_image.py" "$PREBUILT_DIR/Image" --verify --text-offset 0x205000
python3 - "$PREBUILT_DIR/dt.img" <<'PYEOF'
import struct, sys
p = sys.argv[1]
with open(p, "rb") as f:
    magic, ver, cnt = struct.unpack("<4sII", f.read(12))
assert magic == b"DTBH" and ver == 2 and cnt >= 1, f"Invalid dt.img header: {magic!r} v{ver} cnt={cnt}"
print(f"OK: {p} DTBH v{ver} entries={cnt}")
PYEOF

if [[ "$WITH_GMS" == "true" && -d "$ROM_DIR/vendor/gms" ]]; then
    # Verify Git LFS APKs are materialized (Play Store Phonesky.apk must not be a 130-byte LFS pointer)
    SAMPLE_APK="$(find "$ROM_DIR/vendor/gms" -name 'Phonesky.apk' -print -quit)"
    if [[ -n "$SAMPLE_APK" ]]; then
        SZ="$(stat -c%s "$SAMPLE_APK")"
        if (( SZ < 10000 )); then
            echo "error: $SAMPLE_APK is only $SZ bytes (Git LFS pointer! Run 'git -C $ROM_DIR/vendor/gms lfs pull')" >&2
            exit 1
        fi
        echo "OK: Phonesky.apk size=${SZ} bytes (LFS materialized)"
    fi
fi

# Honor explicit WITH_GMS override on command line (e.g. WITH_GMS=false)
export WITH_GMS

cd "$ROM_DIR"
set +u
source build/envsetup.sh
riseup zeroflte "$BUILD_TYPE"
set -u

case "$CLEAN_MODE" in
    clean)        mka clean ;;
    installclean) mka installclean ;;
    none)         ;;
    *)            echo "Unknown CLEAN_MODE=$CLEAN_MODE" >&2; exit 1 ;;
esac

echo "=== Building RisingOS 17 (lineage_zeroflte-$BUILD_TYPE, WITH_GMS=$WITH_GMS, -j$JOBS) ==="
mka bacon -j"$JOBS"

PROD_OUT="$ROM_DIR/out/target/product/zeroflte"
SYS_IMG=""
for cand in "$PROD_OUT/obj/PACKAGING/systemimage_intermediates/system.img" "$PROD_OUT/system.img"; do
    if [[ -f "$cand" ]]; then
        SYS_IMG="$cand"
        break
    fi
done

if [[ -n "$SYS_IMG" ]]; then
    # Convert sparse image if needed to verify true raw ext4 size
    MAGIC_HEX="$(od -An -tx1 -N4 "$SYS_IMG" | tr -d ' \n')"
    RAW_SIZE=0
    if [[ "$MAGIC_HEX" == "3aff26ed" ]]; then
        if command -v simg2img >/dev/null 2>&1; then
            TMP_RAW="$(mktemp)"
            simg2img "$SYS_IMG" "$TMP_RAW"
            RAW_SIZE="$(stat -c%s "$TMP_RAW")"
            rm -f "$TMP_RAW"
        else
            RAW_SIZE="$(stat -c%s "$SYS_IMG")"
            echo "[INFO] Sparse system.img file size = $RAW_SIZE bytes (install simg2img for raw ext4 check)"
        fi
    else
        RAW_SIZE="$(stat -c%s "$SYS_IMG")"
    fi
    if (( RAW_SIZE > 0 )); then
        RAW_MIB="$(awk "BEGIN {printf \"%.1f\", $RAW_SIZE / 1048576}")"
        MAX_MIB="$(awk "BEGIN {printf \"%.1f\", $MAX_SYSTEM_BYTES / 1048576}")"
        echo "=== /system size check: ${RAW_MIB} MiB / ${MAX_MIB} MiB max ($RAW_SIZE / $MAX_SYSTEM_BYTES bytes) ==="
        if (( RAW_SIZE > MAX_SYSTEM_BYTES )); then
            echo "ERROR: system.img exceeds SM-G920F physical SYSTEM partition!" >&2
            exit 1
        fi
    fi
fi

echo "=== Build artifacts in $PROD_OUT ==="
ls -lh "$PROD_OUT"/RisingOS*.zip "$PROD_OUT"/boot.img "$PROD_OUT"/recovery.img 2>/dev/null || true
