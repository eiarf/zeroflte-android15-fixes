# CHECKPOINT — zeroflte (RisingOS 9 / Android 17)

- **Tanggal/Waktu:** 2026-11-25 21:16 WIB
- **Build ID / Artifact Terakhir:** `RisingOS-9-261125-1403-CORE-COMMUNITY-zeroflte.zip` (`1,609,912,088` bytes, SHA-256: `b927d399903f439c521cd7da0254fa17b59a1a4246b8523827e70556547ce498`)
- **Boot Image:** `boot.img` (`25,114,624` bytes, SHA-256: `3874a400f81fdc1cc683f1d0b89a461e5ff8b67dab90ef0ed7dcf299adad66f5`)
- **Kernel:** `Linux 6.6.144-4k` (SukiSU-Nova v4.1.0) | **SELinux:** Permissive
- **Batas Partisi:** `/system` <= `3124019200` bytes (`2979.3 MiB` verified exact fit) | `boot` <= `29360128` bytes (`28.0 MiB`)

## 1. Do Not Break List (Fitur Terverifikasi Bekerja)
- [x] Display & Touch (`1440x2560 @ 60Hz`, `s6_kms_hwcomposer` + `s6_drm_gralloc`)
- [x] Audio Speaker & 3.5mm Jack (`zero_wm1840` jack clamp mode 0x4)
- [x] Deep Sleep (`[s2idle] deep` built-in via `init.samsung.rc` + `exynos7-pm` `sleep_watchdog = false` + `s3c2410_wdt` `WTCON` clear + `RisingOS-17-zeroflte-deepsleep-fix.zip`)
- [x] Rear Camera (`s5c73c2` / `S5K2P2`) — **Terverifikasi cold-boot** (`dumpsys media.camera` 2 cameras, `i2c_recover_bus` + 3x retry + `cam_i2c0/1` pull-none)
- [x] Front Camera (`s5k4e6` + `SoftISP` high-ISO noise/speckles fix: `totalDgainMax = 768`, Gain-Adaptive CCM desat, 5-tap Chroma NR)
- [x] Wi-Fi / BT / RIL / GNSS / NFC / IR / Root (`SukiSU-Nova`)

## 2. Daftar Patch Aktif (Synced & Idempotent)
- `patches/kernel/0001-sm_g92x-sukisu-nova-zeroflte-config.patch` — SukiSU-Nova config
- `patches/kernel/0002-drm-exynos-defer-probe-until-component-drivers-bind.patch` — DRM probe defer
- `patches/kernel/0003-dma-pl330-guard-tasklet-stop-when-inactive.patch` — PL330 DMA safety
- `patches/kernel/0004-asoc-zero-wm1840-jack-clamp-mode-0x4.patch` — 3.5mm jack clamp
- `patches/kernel/0005-camera-s5c73c2-cold-boot-probe-and-i2c-pinctrl-fix.patch` — `cam_i2c0/1` pull-none pinctrl + 3x retry/`i2c_recover_bus`/POR delay in `s5c73c2` (`probe` & `runtime_resume`)
- `patches/kernel/0006-soc-samsung-disable-sleep-watchdog-during-suspend.patch` — Disable `sleep_watchdog` in `exynos7-pm.c` and clear `WTCON` in `s3c2410_wdt.c` on suspend
- `patches/vendor/rising/0001-rising-slim-optional-prebuilts-for-small-system.patch` — Gate `OmniStyle`, `OmniJaws`, `AxQuickLook`, and 4 extra bootanimation zips behind `TARGET_SLIM_SYSTEM_EXT=true` (~268.7 MB saved)
- `local_repos/device_samsung_universal7420-common/` — Updated `prebuilt/{Image,dt.img,modules/*.ko}`, `hardware/camera/soft_isp.cpp`, `ramdisk/etc/init.samsung.rc` (`s2idle`), and `ramdisk/etc/init.samsungexynos7420.usb.rc` (`rm` f1/f2 guard)

## 3. Build Errors Encountered & Patched (Build Error Log)
- **`s5c73c2.c` (`-Werror,-Wunused-variable` on `ret` in `s5c73c2_runtime_resume`)**: Cleaned up directly in `patches/kernel/0005-camera-s5c73c2-cold-boot-probe-and-i2c-pinctrl-fix.patch`.
- **`e2fsdroid: Could not allocate block in ext2 filesystem` (`ThemePicker.apk`, `system/` tree `3167 MB > 2979 MB`)**: Fixed via `patches/vendor/rising/0001-rising-slim-optional-prebuilts-for-small-system.patch` + registered in `scripts/apply_patches.sh`.

## 4. Hipotesis yang Sudah Digugurkan (Ruled-Out)
- Bukan bug di aplikasi kamera (Aperture) atau `ANDROID_LENS_FACING` di Camera HAL; `dumpsys media.camera` membuktikan Camera 0 hilang saat cold boot karena `s5c73c2 10-003d` gagal I2C (`-ENXIO`).

## 5. Reminder Wajib Saat Release
- **REQUIRED COMPANION ZIP: Deep Sleep Fix (`s2idle`)**: Sertakan `RisingOS-17-zeroflte-deepsleep-fix.zip` di release dan jelaskan urutan flash di TWRP:
  1. Flash `RisingOS-9-261125-1403-CORE-COMMUNITY-zeroflte.zip`
  2. Flash `RisingOS-17-zeroflte-deepsleep-fix.zip`
