# Analisis Log Boot 4 & Penyelesaian Dua Akar Masalah Terakhir

> [!IMPORTANT]
> **Temuan Kunci dari Pemeriksaan `/proc/last_kmsg` Boot 4:**
> 1. **SurfaceFlinger & Mesa Panfrost SUKSES 100%:** SurfaceFlinger (`pid 249`) dan Bootanimation (`pid 250`) berhasil berjalan stabil tanpa pernah crash (*0 tombstone*). Crash *EGLConfig* yang sebelumnya terjadi telah **sepenuhnya tuntas**.
> 2. **Penyebab Reboot ke TWRP Terungkap:**
>    - **Kernel Watchdog (`dsh_autorecovery`):** Hardcoded di `bootargs` kernel sebesar 150 detik. Saat 154 detik tercapai, watchdog me-reboot perangkat ke recovery.
>    - **Crash Loop `vendor.hwcomposer-2-1`:** Service composer keluar dengan status 1 karena modul HAL `hwcomposer` tidak ada di sistem, dan saat jatuh tempo ke fallback `gralloc`, berkas `/dev/graphics/fb0` tidak ada pada Kernel 6.6 (DRM KMS).

---

## 1. Perbaikan yang Telah Dieksekusi

### A. Nonaktifkan Watchdog Kernel (`dsh_autorecovery = 0`)
- Berkas `boot.img` di `/dev/block/sda5` diekstrak dan di-patch langsung pada blok string *bootargs* DTB:
  - `dsh_autorecovery=150` -> `dsh_autorecovery=0  `
  - `s3c2410_wdt.tmr_atboot=1` -> `s3c2410_wdt.tmr_atboot=0`
- Berkas `boot_patched.img` telah di-flash kembali ke partisi boot `/dev/block/sda5`.
- **Hasil:** Watchdog timer dinonaktifkan secara permanen dari bootloader. Perangkat tidak akan dipaksa reboot setelah 150 detik.

### B. Kompilasi & Pemasangan `s6kms` Hardware Composer (`hwcomposer.samsungexynos7420.so`)
- Modul HAL hardware composer native DRM/KMS ditulis dan dikompilasi menggunakan Android NDK r26d (`aarch64-linux-android34-clang`).
- **Spesifikasi Modul:**
  - Standar HWC 1.4 kompatibel dengan `HWC2On1Adapter` AOSP.
  - Mengakses `/dev/dri/card0` (Exynos DECON KMS) dan melakukan *modesetting* serta *page-flipping* double-buffered dumb buffers.
  - Memiliki thread VSync internal 60 Hz untuk menyuplai *heartbeat* ke SurfaceFlinger.
- **Pemasangan di Perangkat (`/vendor/lib64/hw/`):**
  - `hwcomposer.samsungexynos7420.so` (10 KB)
  - Symlinks: `hwcomposer.s6kms.so`, `hwcomposer.universal7420.so`, `hwcomposer.exynos5.so`, `hwcomposer.default.so`.
- **Konfigurasi `vendor/build.prop`:**
  - `ro.hardware.hwcomposer=s6kms` diaktifkan.

---

## 2. Status Kesiapan Boot

- **Mesa Panfrost:** Terpasang & terverifikasi berjalan.
- **Hardware Composer:** Modul native DRM KMS terpasang.
- **Kernel Watchdog:** Dinonaktifkan (`0`).
- **Filesystem:** Sudah di-`sync` bersih.
