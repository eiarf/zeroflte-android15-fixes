#!/vendor/bin/sh
# Helper script to toggle resolution & density on Samsung Galaxy S6 (zeroflte)
MODE="$1"

if [ "$MODE" = "fhd" ] || [ "$MODE" = "1080p" ]; then
    echo "[*] Setting display to FHD (1080x1920 @ 420dpi) balanced mode..."
    wm size 1080x1920
    wm density 420
    echo "[+] Done. FHD 1080p active."
elif [ "$MODE" = "hd" ] || [ "$MODE" = "720p" ]; then
    echo "[*] Setting display to HD (720x1280 @ 320dpi) power-saver mode..."
    wm size 720x1280
    wm density 320
    echo "[+] Done. HD 720p active."
else
    echo "[*] Resetting display to Native QHD (1440x2560 @ 560dpi) default..."
    wm size reset
    wm density reset
    echo "[+] Done. Native QHD default active."
fi
