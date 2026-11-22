#!/usr/bin/env bash
set -euo pipefail

BUNDLE_DIR="/serverhive3/eiarf/zeroflte-rising17"
ROM_DIR="/serverhive3/eiarf/rising17"
WEB_DIR="/var/www/html/eiarf"

echo "=== Starting ROM Build with $(nproc) jobs ==="
bash "$BUNDLE_DIR/scripts/build_rom.sh" "$ROM_DIR"

echo "=== Publishing to $WEB_DIR ==="
mkdir -p "$WEB_DIR"
OUT_DIR="$ROM_DIR/out/target/product/zeroflte"

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
echo " BUILD AND PUBLISH SUCCESSFUL!"
echo " Files available at: https://ferrari.serverhive.in/eiarf/"
echo "=========================================================="
ls -lh "$WEB_DIR"
