# vendor/nk/wifi/nk_wifi.mk
PRODUCT_PACKAGES += \
    init.wifi.rc

# Stage brcmfmac4358 firmware & NVRAM with exact devicetree/PCIe names from vendor/samsung blobs
PRODUCT_COPY_FILES += \
    vendor/samsung/zeroflte/proprietary/vendor/etc/wifi/bcmdhd_sta.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac4358-pcie.bin \
    vendor/samsung/zeroflte/proprietary/vendor/etc/wifi/bcmdhd_sta.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac4358-pcie.samsung,zeroflte.bin \
    vendor/samsung/zeroflte/proprietary/vendor/etc/wifi/nvram_net.txt:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac4358-pcie.txt \
    vendor/samsung/zeroflte/proprietary/vendor/etc/wifi/nvram_net.txt:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac4358-pcie.samsung,zeroflte.txt
