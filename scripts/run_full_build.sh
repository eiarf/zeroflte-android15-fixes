#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-${ROM_DIR:-$HOME/rising17}}"
SYNC_JOBS="${SYNC_JOBS:-6}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
WITH_GMS="${WITH_GMS:-true}"
WEB_DIR="/var/www/html/eiarf"

echo "=========================================================="
echo " Starting Full Automated Build for Galaxy S6 (zeroflte)"
echo " ROM_DIR:    $ROM_DIR"
echo " BUNDLE_DIR: $BUNDLE_DIR"
echo " SYNC_JOBS:  $SYNC_JOBS"
echo " BUILD_JOBS: $BUILD_JOBS"
echo " WITH_GMS:   $WITH_GMS"
echo " WEB_DIR:    $WEB_DIR"
echo "=========================================================="

# 1. Environment & limits
ulimit -n 65536 || true
export USE_CCACHE=1
export CCACHE_EXEC=/usr/bin/ccache
export CCACHE_DIR="$HOME/.ccache"
ccache -M 50G || true

# 2. Sync source tree, install local repos, and apply patches
echo ""
echo "=== STEP 1: Sync & Setup (JOBS=$SYNC_JOBS) ==="
JOBS="$SYNC_JOBS" bash "$BUNDLE_DIR/scripts/server_sync_and_setup.sh" "$ROM_DIR"

# 3. Build Kernel Linux 6.6.144 + SukiSU-Nova
echo ""
echo "=== STEP 2: Build Linux 6.6 Kernel + SukiSU-Nova (JOBS=$BUILD_JOBS) ==="
JOBS="$BUILD_JOBS" bash "$BUNDLE_DIR/scripts/build_kernel_linux.sh" "$ROM_DIR"

# 4. Build RisingOS-Revived 17 ROM
echo ""
echo "=== STEP 3: Compile RisingOS-Revived 17 ROM (JOBS=$BUILD_JOBS) ==="
JOBS="$BUILD_JOBS" WITH_GMS="$WITH_GMS" bash "$BUNDLE_DIR/scripts/build_rom.sh" "$ROM_DIR"

# 5. Publish to Apache Webserver
echo ""
echo "=== STEP 4: Publishing Artifacts to $WEB_DIR ==="
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

echo ""
echo "=========================================================="
echo " BUILD & PUBLISH SUCCESSFUL!"
echo " Files available at: https://ferrari.serverhive.in/eiarf/"
echo "=========================================================="
ls -lh "$WEB_DIR"
