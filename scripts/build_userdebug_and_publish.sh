#!/usr/bin/env bash
set -euo pipefail

BUNDLE_DIR="/serverhive5/eiarf/zeroflte-rising17"
ROM_DIR="/serverhive5/eiarf/rising17"
WEB_DIR="/var/www/html/eiarf"
BUILD_TYPE="userdebug"
CLEAN_MODE="installclean"
OUT_DIR="$ROM_DIR/out/target/product/zeroflte"

echo "=== Cleaning prior zip and image artifacts in $OUT_DIR ==="
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/RisingOS*.zip "$OUT_DIR"/boot.img "$OUT_DIR"/recovery.img

echo "=== Verifying and applying all platform patches ==="
bash "$BUNDLE_DIR/scripts/apply_patches.sh" "$ROM_DIR"

echo "=== Starting RisingOS 17 USERDEBUG variant build with $(nproc) jobs ==="
export WITH_ADB_INSECURE=true
export BUILD_TYPE CLEAN_MODE
bash "$BUNDLE_DIR/scripts/build_rom.sh" "$ROM_DIR"

echo "=== Publishing to $WEB_DIR ==="
mkdir -p "$WEB_DIR"
rm -f "$WEB_DIR"/RisingOS*.zip "$WEB_DIR"/boot.img "$WEB_DIR"/recovery.img "$WEB_DIR"/SHA256SUMS.txt

for f in "$OUT_DIR"/RisingOS*.zip "$OUT_DIR"/boot.img "$OUT_DIR"/recovery.img; do
    if [[ -f "$f" ]]; then
        echo "Copying $(basename "$f") to $WEB_DIR..."
        cp -f "$f" "$WEB_DIR/"
    fi
done

chmod 644 "$WEB_DIR"/* 2>/dev/null || true
(
    cd "$WEB_DIR"
    sha256sum RisingOS*.zip boot.img recovery.img > SHA256SUMS.txt 2>/dev/null || true
)

echo "=========================================================="
echo " USERDEBUG BUILD AND PUBLISH SUCCESSFUL!"
echo " Files available at: https://ferrari.serverhive.in/eiarf/"
echo "=========================================================="
ls -lh "$WEB_DIR"
cat "$WEB_DIR/SHA256SUMS.txt"
