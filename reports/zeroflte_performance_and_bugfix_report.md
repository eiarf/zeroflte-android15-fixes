# Laporan Optimalisasi Performa & Perbaikan Bug - Galaxy S6 (Android 15)

## 1. Masalah Utama yang Ditemukan & Solusinya

### A. Alasan HP Restart ke TWRP Setelah 15 Menit (Kernel Autorecovery)
* **Penyebab**: Modul kernel `dsh_esslog` memiliki fitur `autorecovery` yang diatur ke **900 detik (15 menit)** saat boot. Namun, sebelumnya tidak ada sinyal balik saat Android selesai booting, sehingga timer kernel terus menghitung mundur hingga 900 detik lalu otomatis memaksa restart ke TWRP (`dsh: restarting to recovery`).
* **Solusi**: 
  - Selama fase boot, watchdog tetap aktif di **900 detik** sebagai pengaman jika terjadi bootloop/hang (sesuai permintaan user agar tidak perlu menekan tombol fisik).
  - Ketika Android berhasil booting (`sys.boot_completed=1`), parameter `/sys/module/dsh_esslog/parameters/autorecovery` otomatis diset ke **`0`** (disarmed). Hal ini mencegah HP restart sendiri saat sedang dipakai.

---

### B. Penyebab Sistem Terasa Lemot & Solusi ZRAM (Swap 2 GB)
* **Penyebab**: 
  - RAM fisik Galaxy S6 adalah ~3 GB. Android 15 Vanilla memakan ~2.4 - 2.5 GB saat idle, menyisakan hanya ~350 - 400 MB RAM bebas.
  - Modul kernel ZRAM (`zsmalloc.ko` dan `zram.ko`) tidak termuat secara otomatis saat boot, sehingga swap berstatus kosong (`0 MB`). Akibatnya, Linux terpaksa terus-menerus membuang cache disk (`page faults` berulang) yang menyebabkan UI tersendat parah (*stuttering*).
* **Solusi**:
  - Mengaktifkan pemuatan modul `zsmalloc.ko` dan `zram.ko` via `init.samsungexynos7420.rc`.
  - Menginisialisasi **2 GB ZRAM** (`/dev/block/zram0`) dengan algoritma kompresi kernel bawaan `lzo-rle`.
  - Mengatur VM tuning: `swappiness=100`, `page-cluster=0`, `vfs_cache_pressure=60`.
  - **Hasil**: RAM bebas meningkat drastis menjadi **~740 MB** dan cache memori mencapai ~900 MB - 1.2 GB, menghilangkan stutter akibat I/O disk.

---

### C. Optimalisasi GPU & CPU Governor / Clocks
* **Resolusi Layar S6**: Galaxy S6 memiliki resolusi bawaan sangat tinggi (**1440 x 2560 WQXGA**, 3.68 juta piksel per frame, ~14.7 MB per buffer).
* **Perbaikan GPU (Mali-T760 MP8)**:
  - Floor clock GPU devfreq bawaan berada di batas paling rendah (**266 MHz**), tidak cukup cepat untuk render resolusi 2K secara mulus pada 60 FPS.
  - Melalui `powerhint.json` dan `init.rc`, floor GPU dinaikkan ke **420 MHz** (peningkatan performa rendering dasar +58%) dengan boost interaksi sentuhan hingga **544 MHz**.
* **Perbaikan CPU (Exynos 7420)**:
  - Little Cluster (Cortex-A53): Floor dinaikkan dari 400 MHz ke **600 MHz**.
  - Big Cluster (Cortex-A57): Floor dinaikkan dari 800 MHz ke **1000 MHz**.
  - `TAUClampBoost` (top-app): Baseline clamp diset ke **15** untuk memastikan UI aplikasi foreground langsung mendapat prioritas penjadwalan CPU tanpa jeda ramp-up.
* **SurfaceFlinger & UI Transitions**:
  - Mengaktifkan hardware acceleration `debug.sf.hw=1` dan `debug.sf.latch_unsignaled=1`.
  - Mematikan efek Gaussian blur SystemUI yang membebani shader GPU (`ro.surface_flinger.supports_background_blur=0`).
  - Mengatur skala animasi jendela dan transisi ke **0.75x** untuk responsivitas instan.

---

### D. Mengapa Kamera Tidak Berfungsi?
* **Investigasi Sistem & HAL**:
  - Ditemukan bahwa service Camera Provider (`android.hardware.camera.provider`) **sama sekali tidak disertakan** di direktori `/vendor/bin/hw/` maupun di `/vendor/etc/vintf/manifest.xml`.
  - Hasil logcat/dumpsys: `Number of camera devices: 0`.
* **Akar Masalah**:
  - ROM porting Android 15 (dan LineageOS upstream) ini berjalan di kernel Linux mainline 6.6.
  - Driver kamera Samsung Exynos 7420 (FIMC-IS) merupakan subsistem proprietary lama (kernel 3.10) dengan library 32-bit yang belum di-porting oleh developer upstream ke subsistem V4L2/MC Linux 6.6.
  - Oleh karena itu, kamera memang tidak berfungsi dari source aslinya (baik di LineageOS maupun Infinity-X).
