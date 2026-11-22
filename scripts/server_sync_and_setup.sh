#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROM_DIR="${1:-${ROM_DIR:-$HOME/rising17}}"
JOBS="${JOBS:-$(nproc)}"
INIT_ONLY="${INIT_ONLY:-0}"
SKIP_GMS="${SKIP_GMS:-0}"

for cmd in git git-lfs repo python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: required command '$cmd' not found in PATH" >&2
        exit 1
    fi
done

mkdir -p "$ROM_DIR/.repo/local_manifests"

if [[ ! -f "$ROM_DIR/.repo/manifest.xml" ]]; then
    echo "=== Initializing RisingOS-Revived seventeen in $ROM_DIR ==="
    (
        cd "$ROM_DIR"
        repo init -u https://github.com/RisingOS-Revived/android.git -b seventeen --git-lfs
    )
else
    echo "=== Existing repo checkout detected in $ROM_DIR ==="
fi

echo "=== Installing local manifest (.repo/local_manifests/zeroflte.xml) ==="
cp -f "$BUNDLE_DIR/manifests/zeroflte_rising17.xml" "$ROM_DIR/.repo/local_manifests/zeroflte.xml"

if [[ "$INIT_ONLY" != "1" ]]; then
    echo "=== Running repo sync (-j$JOBS) ==="
    (
        cd "$ROM_DIR"
        SYNC_RETRY=0
        until repo sync -c -j"$JOBS" --force-sync --no-clone-bundle --no-tags --retry-fetches=5; do
            SYNC_RETRY=$((SYNC_RETRY+1))
            if [ "$SYNC_RETRY" -ge 6 ]; then
                echo "Repo sync failed after 6 attempts!" >&2
                exit 1
            fi
            echo "=== Sync retry $SYNC_RETRY/6: waiting 15s to bypass Google rate-limits (HTTP 429) ==="
            sleep 15
            JOBS=4
        done
        if [[ "$SKIP_GMS" != "1" && -d "$ROM_DIR/vendor/gms" ]]; then
            echo "=== Pulling Git LFS payloads in vendor/gms ==="
            git -C "$ROM_DIR/vendor/gms" lfs pull
        fi
    )
fi

sync_local_repo() {
    local name="$1"
    local target="$2"
    local src="$BUNDLE_DIR/local_repos/$name"
    local dst="$ROM_DIR/$target"
    if [[ ! -d "$src" ]]; then
        echo "error: missing local repo $src" >&2
        exit 1
    fi
    echo "=== Installing local repo $name -> $target ==="
    rm -rf "$dst"
    mkdir -p "$(dirname "$dst")"
    cp -a "$src" "$dst"
}

sync_local_repo "device_samsung_universal7420-common" "device/samsung/universal7420-common"
sync_local_repo "device_samsung_zeroflte"             "device/samsung/zeroflte"
sync_local_repo "vendor_nk"                           "vendor/nk"

# Apply all platform, minigbm, mkbootimg, releasetools, GMS, and addons patches
SKIP_GMS="$SKIP_GMS" bash "$BUNDLE_DIR/scripts/apply_patches.sh" "$ROM_DIR"

echo ""
echo "=== Setup COMPLETE! Next steps ==="
echo "1. Build Linux 6.6.144 + SukiSU-Nova kernel:"
echo "   bash $BUNDLE_DIR/scripts/build_kernel_linux.sh $ROM_DIR"
echo "2. Compile RisingOS-Revived Android 17 ROM:"
echo "   bash $BUNDLE_DIR/scripts/build_rom.sh $ROM_DIR"
