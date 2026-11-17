#!/sbin/sh
set -e
V=/system_root/system/vendor

cp "$V/lib64/hw/gralloc.samsungexynos7420.so" "$V/lib64/hw/gralloc.default.so"
chmod 0644 "$V/lib64/hw/gralloc.samsungexynos7420.so" "$V/lib64/hw/gralloc.default.so"
chcon u:object_r:same_process_hal_file:s0 "$V/lib64/hw/gralloc.samsungexynos7420.so" "$V/lib64/hw/gralloc.default.so"

mkdir -p "$V/firmware/brcm"
cp "$V/etc/wifi/bcmdhd_sta.bin" "$V/firmware/brcm/brcmfmac4358-pcie.samsung,zeroflte.bin"
cp "$V/etc/wifi/bcmdhd_sta.bin" "$V/firmware/brcm/brcmfmac4358-pcie.bin"
cp "$V/etc/wifi/nvram_net.txt" "$V/firmware/brcm/brcmfmac4358-pcie.samsung,zeroflte.txt"
cp "$V/etc/wifi/nvram_net.txt" "$V/firmware/brcm/brcmfmac4358-pcie.txt"
cp "$V/firmware/bcm4358A3_V0083.0138.hcd" "$V/firmware/brcm/BCM4358A3.samsung,zeroflte.hcd"
cp "$V/firmware/bcm4358A3_V0083.0138.hcd" "$V/firmware/brcm/BCM4358A3.hcd"

chmod 0755 "$V/firmware/brcm"
chmod 0644 "$V/firmware/brcm/"*
chcon -R u:object_r:vendor_firmware_file:s0 "$V/firmware/brcm"

ls -laZ "$V/firmware/brcm"
sync
sync
echo "WIFI_YUV_OK"