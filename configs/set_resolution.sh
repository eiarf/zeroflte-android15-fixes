#!/vendor/bin/sh
# Helper script to toggle resolution & density on Samsung Galaxy S6 (zeroflte)
MODE="$1"

if [ "$MODE" = "native" ] || [ "$MODE" = "qhd" ] || [ "$MODE" = "reset" ]; then
    echo "[*] Setting display to Native QHD (1440x2560 @ 560dpi)..."
    setprop persist.vendor.display.resolution "native"
    wm size reset
    wm density reset
    echo "[+] Done. Native QHD active."
elif [ "$MODE" = "hd" ] || [ "$MODE" = "720p" ]; then
    echo "[*] Setting display to HD (720x1280 @ 320dpi) ultra-performance mode..."
    setprop persist.vendor.display.resolution "hd"
    wm size 720x1280
    wm density 320
    echo "[+] Done. HD 720p active."
else
    echo "[*] Setting display to FHD (1080x1920 @ 420dpi) balanced mode..."
    setprop persist.vendor.display.resolution "fhd"
    wm size 1080x1920
    wm density 420
    echo "[+] Done. FHD 1080p active."
fi
