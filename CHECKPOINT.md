# CHECKPOINT — zeroflte (RisingOS 9 / Android 17)

> **Panduan Singkat:** Dokumen ini mencatat riwayat iterasi perbaikan kernel `Linux 6.6.144-4k`, Camera HAL (`SoftISP` + `ak7345` VCM), manajemen termal/performa (`ASV0/ASV2 UV` + `powerhint`), dan build full ROM untuk **Samsung Galaxy S6 Flat (`SM-G920F` / `zeroflte`)**. Setiap iterasi (`v0` → `v1` → `v2` → `v3` → `v4` → `full-rom-261126-1432`) diuji langsung di unit perangkat fisik (`SM-G920F`, ADB serial `06157df6085a9c3c`).

- **Tanggal/Waktu:** 2026-11-26 22:15 WIB
- **Build ID / Artifact Terakhir:** `RisingOS-9-261126-1432-CORE-COMMUNITY-zeroflte.zip` (`1,610,780,504` bytes, SHA-256: `2e13a366f5b3e9635b9300e4a916201c54b7f9ef0d8e6b05e2fecc1e17eb0653`) — *mencakup kernel `v1` + Camera HAL `v4` + semua refinement terintegrasi*
- **Boot Image Aktif (`v1..v4` & Full ROM):** `boot.img` / `boot_v1.img` (`26,060,816` bytes, SHA-256: `9e3031b98abee8dad2ce639465ffa8fd7ddf166d849d81dc47b202b7b3d53ed4`) | Camera HAL binary `v4`: `android.hardware.camera.provider-V3-service` (`243,608` bytes, SHA-256: `b9a5130e566bea457595b5379cb04e3aaa9997af2cf2ca0c147dfe5b302d59b7`)
- **Kernel:** `Linux 6.6.144-4k` (SukiSU-Nova v4.1.0) | **SELinux:** Permissive
- **Batas Partisi:** `/system` <= `3124019200` bytes (`2979.3 MiB / 2979.3 MiB max` verified) | `boot` <= `29360128` bytes (`28.0 MiB`) | `recovery` <= `35651584` bytes (`34.0 MiB`)

---

## 1. Do Not Break List (Fitur Terverifikasi Bekerja)
- [x] Display & Touch (`1440x2560 @ 60Hz`, `s6_kms_hwcomposer` + `s6_drm_gralloc`)
- [x] Audio Speaker & 3.5mm Jack (`zero_wm1840` jack clamp mode 0x4)
- [x] Deep Sleep (`[s2idle] deep` built-in via `init.samsung.rc` + `exynos7-pm` `sleep_watchdog = false` + `s3c2410_wdt` `WTCON` clear + `RisingOS-17-zeroflte-deepsleep-fix.zip`)
- [x] Rear Camera (`s5c73c2` / `S5K2P2`) — **Terverifikasi cold-boot** (`dumpsys media.camera` 2 cameras, `i2c_recover_bus` + 3x retry + `cam_i2c0/1` pull-none)
- [x] Front Camera (`s5k4e6` + `SoftISP`)
- [x] Wi-Fi / BT / RIL / GNSS / NFC / IR / Root (`SukiSU-Nova`)

---

## 2. Temuan Diagnostik Baseline (`v0-baseline` pada `06157df6085a9c3c`)
1. **Kamera Delay (~250–450 ms) & ~15 fps (`901/1211 frames dropped` di `exynos7-flite`):**
   - `s5c73c2.c` (`s5k2p2_modes[1]`, `2664x1500`) memakai `vts_def = 1586` -> **60.05 fps**, membatasi eksposur preview ke <=16.5 ms (gelap/noisy).
   - `v4l2_pipeline.cpp` mengalokasikan 4 buffer V4L2 FIFO dan `dequeueFrame()` hanya mengambil buffer **tertua** tanpa membuang frame usang (*stale*).
   - `soft_isp.cpp` melakukan `stageRaw()` single-threaded pada DMA-BUF uncached, 4 worker thread, dan alokasi `std::vector` per baris pada `chromaNrPtrRow()`.
2. **Kualitas Kamera:**
   - `sampleTapCell()` merata-rata kedua green diagonal (`(g0+g1)>>1`), menurunkan resolusi efektif di JPEG penuh; `writeRgba()` & `writeJpeg()` belum punya 4-neighbor DPC & Chroma NR; `V4l2Pipeline::closeNodes()` tidak menonaktifkan media link per-pipeline.
3. **Cepat Panas (`exynos-therm = 59.0°C` saat idle, lebih panas saat NewTube):**
   - `CONFIG_CMDLINE` masih menyalakan **`slub_debug=FZPU,kmalloc-16`** dan **`loglevel=8 ignore_loglevel`**.
   - `cpu_atlas_opp_table` (Cortex-A57) di `exynos7.dtsi` **overvolt +43.75 mV s/d +87.5 mV** di atas tabel resmi Samsung ASV0 v15 (`volt_table_big_asv_v15` di branch `tw70-stock`).
   - `powerhint.json` menyetel `TAUClampBoost DefaultIndex = 2` (`top-app/cpu.uclamp.min = 15%` permanen) & GPU `544 MHz` 5 detik per `LAUNCH`, mendorong decode video software (`c2.android.*` di NewTube) ke core A57.
   - Throttling thermal di `exynos7.dtsi` baru efektif di **80°C** (`map0` 75°C hanya membatasi 1704 MHz sementara `powerhint.json` sudah membatasi 1500 MHz).
   - `CONFIG_LTO_NONE=y` masih aktif.
4. **Glitch Layar Sesekali:**
   - `dmesg` mencatat berulang `panfrost 14ac0000.gpu: Unhandled Page fault` (`JOB_BUS_FAULT` / `DATA_INVALID_FAULT`) tepat setelah `panfrost_gem_shrinker_scan`: `panfrost_gem_purge()` melepas MMU mapping tanpa mengecek `dma_resv_test_signaled(shmem->base.resv, DMA_RESV_USAGE_BOOKKEEP)` maupun `obj->import_attach`.
   - `exynos-decon` mencatat `no TE for 50 ms, frame sent without it` dan `xfer_ms = 0,...,5377` sementara `bus-disp` terkunci di `100 MHz` (`powersave`) padahal `160–266.7 MHz` memakai tegangan `650 mV` yang sama.
5. **Bug ZRAM Boot Log:**
   - `init.samsungexynos7420.rc` memanggil `swapon_all` dua kali (memicu `zram: Cannot change disksize for initialized device` & `mkswap failed`).

---

## 3. Kernel & Camera Testing Iteration Log

| Versi | Tanggal/Waktu | Perubahan Utama | `boot.img` Size & SHA-256 | Hasil Uji Perangkat (`06157df6085a9c3c`) | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`v0-baseline`** | 2026-11-25 21:16 | Kernel `6.6.144-4k` + `0001..0006` + `SoftIsp` (`totalDgainMax=768`, `writeYuv` Chroma NR) | `24,936,464` B (`3874a400...`) | Idle `59.0°C`, Panfrost fault berulang setelah shrinker, DECON `xfer_ms>=18ms=5377`, cam `901/1211` dropped, `slub_debug=FZPU` aktif | **BASELINE** |
| **`v1-refinement`** | 2026-11-26 19:45 | Kernel `0007..0010` (`ThinLTO` + `-O2`, `CONFIG_KPM=y`, cmdline bersih, `BBR`+`FQ`+`IPSET`+`IP6_NF_NAT`+`TMPFS_XATTR`+`OVERLAY_FS`+`LZ4HC`, ASV0 Big/ASV2 GPU UV, thermal `64..95°C`, Panfrost shrinker guard, `s5c73c2` 30fps+10s) + Camera HAL (`SoftIsp` NEON parallel `stageRaw`, 6 threads, 4-neighbor DPC & Chroma NR di preview/YUV/JPEG, edge-directed demosaic + USM, `dequeueFrame` drain stale frames, per-pipeline link teardown) + `powerhint.json`/`thermal_info_config.json`/`init.postboot.sh`/`init.samsungexynos7420.rc` | `26,060,816` B (`9e3031b98abee8dad2ce639465ffa8fd7ddf166d849d81dc47b202b7b3d53ed4`) + `zeroflte-kernel66-camera-refinement-v1.zip` (`1937a63b...`) | **Kernel/Thermal/DECON/Panfrost/JPEG PASS:** `stack_depot_disable=on`, `loglevel=4`, `bbr`+`ecn=1`+`fq`, `zram=[lz4]`, `vdd_atlas=818.75mV @ 1GHz`, `vdd_g3d=750mV @ 266MHz`, `bus-disp=160MHz`, 0 Panfrost fault pasca `shrinker_scan`, DECON `xfer_ms=0,0,0,0,0,0,0,660,4,799` (`flip_forced=0`), `mem_sleep=[s2idle] deep`, JPEG `4000x3000` (`9.1M`) + AF `pos=433`. **Preview bottleneck ditemukan:** `SoftIsp` preview `5.0 fps` (`isp=191.2ms`, CPU `492%`, 82°C) akibat `sampleTapCellDpc()` + YCbCr per-piksel full-res di `writeRgba()` | **PARTIAL PASS (Optimizing v2)** |
| **`v2-camera-hal`** | 2026-11-26 20:09 | Optimasi `SoftIsp` preview (`writeRgba()` & `writeYuv()`): 2-tahap half-res Bayer cell demosaic (`demosaicCropToHalfResRgb`, horizontal 2-neighbor DPC + highlight soft-knee zero-div + WB/CCM/Gamma sekali per sel `2x2` + smoothing chroma 3-tap low-light) + integer Q8 bilinear scaling; `writeJpeg()` tetap full-res edge-directed + 4-neighbor DPC + Chroma NR + USM | `boot_v1.img` (`26,060,816` B, `9e3031b9...`) + `zeroflte-kernel66-camera-refinement-v2.zip` (`15,303,293` B, SHA-256: `24b4300f46660ead8d96e19692ba9953760597e6935126de846ccc9065758683`) | **Photo/Static PASS; Video/Color/AF Revisi Ditemukan:** Preview photo rear `24.6–25.2 fps` (`isp=29.1–29.5ms`), front `18.8 fps` (`isp=41.6ms`). **Stress test 5 menit kamera (`v2_rear.mp4` 151.7s + `v2_front.mp4` 151.9s):** stabil tanpa crash/tombstone, namun HAL drop ke `8.9 fps` (`isp=100ms`, MP4 tercatat `4.49 fps`/`3.42 fps`, `exynos-therm` `88–90°C`, `cpu4` throttle `800MHz`) karena 2 stream `RGBA_8888` menjalankan demosaic 2x per frame. **Stress test playback 3 menit (`VideoBench` `c2.android.avc.decoder`):** `4.92 fps`, `0` late, `exynos-therm=55–57°C` (`swcodec=22.5%`). **Issue warna & AF:** cast kuning-hijau (`v2_front` `G/B=1.686`, `v2_rear` `G/B=1.426`) akibat filter AWB `maxC*4 <= minC*9` pada domain raw + CCM hijau `1.22`; tap-to-focus sering miss akibat `settleHold=1` tidak sinkron dengan antrean 3 buffer V4L2 + `finePass` hanya 2 titik. | **BASELINE STRESS TESTED** |
| **`v3-cam-color-af-video`** | 2026-11-26 20:54 | Perbaikan Camera HAL (`soft_isp.{h,cpp}`, `camera_device.{h,cpp}`): AWB neutral white-balanced + CCM netral; `AfState` sinkron 65ms (`settleHold=2`), 9 coarse + 5 fine; `ensureHalfResRgb()` cache 1x per frame + `writeRgba()` NEON | `boot_v1.img` (`26,060,816` B, `9e3031b9...`) + `zeroflte-kernel66-camera-refinement-v3.zip` (`15,303,589` B, SHA-256: `1f2af1450e416ec6242ed44332981b563571fe220f6cbb25e0cb4695e88f06b6`) | **Terverifikasi naik hampir 2x di Video & Warna lebih netral:** Rekaman video dual-stream `cam0` naik dari `8.9 fps` (`v2`) ke **`15.4–16.0 fps` (`isp=51–53ms`)**; dinding putih pada `v3_rear.jpg` membaik dari `G/B=1.426` -> `G/R=1.032, G/B=1.158`. **Catatan:** `bGain` mentok clamp atas **`3.20`** dan foto masih menyentuh `af=511` di `iso=4800` -> disempurnakan langsung ke `v4`. | **ITERATED -> v4** |
| **`v4-color-af-tuned`** | 2026-11-26 21:18 | Penyempurnaan dari hasil uji `v3`: naikkan clamp AWB `rGain/bGain` `[0.75..4.2]`, perketat filter netral WB (`1.45x`) & kurangi green CCM (`1.12`); naikkan noise gate AF ke `10` (mencegah ISO grain menang di `pos=511`), settle VCM `75ms`, serta log tiap kurva `(pos, score)` | `boot_v1.img` (`26,060,816` B, `9e3031b9...`) + `zeroflte-kernel66-camera-refinement-v4.zip` (`15,303,643` B, SHA-256: `68c69e5494cdd55f573a732be16623dd0df4726d21c10e98270ed58e6975ea1a`) | **VERIFIED PASS (Photo + Color + AF + 5-Min Video + 3-Min Playback):** **Warna:** `v4_front.jpg` RGB `[104.98, 106.93, 102.52]` (**`G/R=1.019`, `G/B=1.043`**, membaik drastis dari `G/B=1.686` di `v2`); `v4_rear.jpg` area dinding putih `[143.39, 154.88, 150.89]` (**`G/R=1.080`, `G/B=1.026`**, vs `1.426` di `v2`). **AF:** Kurva `cam0` membentuk puncak optik bersih di `pos=467` (`0.8722`) vs `0.0000` di `pos=20` dan responsif pada tap-to-focus ROI baru. **Stress test 5 menit video (`v4`):** `0` crash, `0` tombstone, `0` Panfrost fault, `0` DECON `no TE`, `battery` stabil `31.1°C`, `exynos-therm` `84->90°C` (puncak `91°C` sebentar di front, `cpu4` throttle aman ke `800MHz`); HAL dual-stream FPS naik ke **`13.7–14.2 fps` normal / `10.5–11.3 fps` throttled** (rear, vs `8.8–9.0 fps` di `v2`) & **`9.7–11.7 fps`** (front, vs `7.8–8.0 fps` di `v2`), dengan frame drop `exynos7-flite` rear anjlok 6.3x (`751` -> **`119` dropped / 4839 frames**). Catatan: output file MP4 `1080p` masih terbatas **`~4.4 fps` rear (`21.98 MB`, 667 frames)** / **`~3.3 fps` front (`22.11 MB`, 502 frames)** karena bottleneck encoder CPU `c2.android.avc.encoder` pada resolusi 1080p. **Stress test playback 3 menit (`VideoBench` `c2.android.avc.decoder`):** `888` frames (`4.93 fps` mengikuti timestamp MP4), `0` late frames, `exynos-therm` turun stabil `67 -> 62°C` (cooldown `59°C`), `battery=31.1°C`, `0` glitch/fault. | **PASS (ACTIVE)** |
| **`full-rom-261126-1432`** | 2026-11-26 22:15 | Full ROM build (`BUILD_TYPE=user`, `WITH_GMS=true`, `CLEAN_MODE=installclean`, `mka bacon`, `38m37s`) menyatukan `v1` kernel/thermal/UV/Panfrost + `v4` Camera HAL + `powerhint.json` + `thermal_info_config.json` + `init.samsungexynos7420.rc` + `init.postboot.sh` + `TARGET_SLIM_SYSTEM_EXT=true` + GMS slim patch | `boot.img` (`26,060,816` B, `9e3031b98abee8dad2ce639465ffa8fd7ddf166d849d81dc47b202b7b3d53ed4`) | **FULL ROM BUILT & DOWNLOADED:** `RisingOS-9-261126-1432-CORE-COMMUNITY-zeroflte.zip` (`1,610,780,504` B, SHA-256: `2e13a366f5b3e9635b9300e4a916201c54b7f9ef0d8e6b05e2fecc1e17eb0653`). `/system` tepat `2979.3 MiB / 2979.3 MiB max` (`3,124,019,200` B raw / `3,032,355,708` B sparse), `recovery.img` `35,051,536` B (`<= 35,651,584` B). Seluruh binary (`boot.img`, `android.hardware.camera.provider-V3-service`, `powerhint.json`, `init.samsungexynos7420.rc`, `init.postboot.sh`, `kernel`, `dt.img`) terverifikasi SHA-256 bit-for-bit identik dengan `v1`/`v4`; unduhan lokal lulus cek SHA-256. | **RELEASE READY (LOCAL VERIFIED)** |

> **Catatan Fitur Referensi:** `Hmbird/Windspeed` (`SCHED_PATCH` WALT-only), `BBR Brutal`, dan `SUSFS4KSU` patch yang tidak kompatibel dengan `SukiSU-Nova v4.1.0` tidak dipasang. `Baseband-guard` dilewati karena memerlukan patch invasif pada `security/selinux/` dan `CONFIG_LSM`.

---

## 4. Daftar Patch Aktif (Synced & Idempotent)
- `patches/kernel/0001-sm_g92x-sukisu-nova-zeroflte-config.patch` — SukiSU-Nova config
- `patches/kernel/0002-drm-exynos-defer-probe-until-component-drivers-bind.patch` — DRM probe defer
- `patches/kernel/0003-dma-pl330-guard-tasklet-stop-when-inactive.patch` — PL330 DMA safety
- `patches/kernel/0004-asoc-zero-wm1840-jack-clamp-mode-0x4.patch` — 3.5mm jack clamp
- `patches/kernel/0005-camera-s5c73c2-cold-boot-probe-and-i2c-pinctrl-fix.patch` — `cam_i2c0/1` pull-none pinctrl + 3x retry/`i2c_recover_bus`/POR delay in `s5c73c2` (`probe` & `runtime_resume`)
- `patches/kernel/0006-soc-samsung-disable-sleep-watchdog-during-suspend.patch` — Disable `sleep_watchdog` in `exynos7-pm.c` and clear `WTCON` in `s3c2410_wdt.c` on suspend
- `patches/kernel/0007-zeroflte-perf-cmdline-lto-net-zram-mountify.patch` — Cmdline (`stack_depot_disable=on`, `loglevel=4`), `ThinLTO` + `-O2`, `CONFIG_KPM=y`, `BBR` + `FQ`, `IPSET` + `IP6_NF_NAT`, `TMPFS_XATTR`/`OVERLAY_FS`, `LZ4HC` *(terverifikasi idempotent pada clean `SM_G92X` worktree)*
- `patches/kernel/0008-arm64-dts-exynos7420-universal-safe-uv-and-thermal.patch` — Universal-safe ASV0 Big (`1.8 GHz` max, -43.75 s/d -87.5 mV) & ASV2 GPU (-12.5 mV) UV + thermal trip points `64/70/76/82/88/95/105°C` *(terverifikasi idempotent pada clean `SM_G92X` worktree)*
- `patches/kernel/0009-drm-panfrost-guard-shrinker-purge-against-active-fences.patch` — Guard `panfrost_gem_purge()` terhadap `obj->import_attach` dan active `dma_resv` fences sebelum melepas MMU mapping *(terverifikasi idempotent pada clean `SM_G92X` worktree)*
- `patches/kernel/0010-media-s5c73c2-30fps-preview-and-autosuspend.patch` — Preview 30 fps (`vts_def` 3172/3168) + autosuspend `10000 ms` *(terverifikasi idempotent pada clean `SM_G92X` worktree)*
- `patches/vendor/rising/0001-rising-slim-optional-prebuilts-for-small-system.patch` — Gate `OmniStyle`, `OmniJaws`, `AxQuickLook`, and 4 extra bootanimation zips behind `TARGET_SLIM_SYSTEM_EXT=true` (~268.7 MB saved)
- `local_repos/device_samsung_universal7420-common/` — Updated `prebuilt/{Image,dt.img,modules/*.ko}`, `hardware/camera/{soft_isp,v4l2_pipeline,camera_device}.{h,cpp}`, `configs/power/powerhint.json`, `ramdisk/bin/init.postboot.sh`, `ramdisk/etc/{init.samsung.rc,init.samsungexynos7420.rc,init.samsungexynos7420.usb.rc}`
- `local_repos/device_samsung_zeroflte/` — Updated `configs/thermal/thermal_info_config.json` (`64..105°C`)

## 5. Build Errors Encountered & Patched (Build Error Log)
- **`s5c73c2.c` (`-Werror,-Wunused-variable` on `ret` in `s5c73c2_runtime_resume`)**: Cleaned up directly in `patches/kernel/0005-camera-s5c73c2-cold-boot-probe-and-i2c-pinctrl-fix.patch`.
- **`e2fsdroid: Could not allocate block in ext2 filesystem` (`ThemePicker.apk`, `system/` tree `3167 MB > 2979 MB`)**: Fixed via `patches/vendor/rising/0001-rising-slim-optional-prebuilts-for-small-system.patch` + registered in `scripts/apply_patches.sh`.

## 6. Hipotesis yang Sudah Digugurkan (Ruled-Out)
- Bukan bug di aplikasi kamera (Aperture) atau `ANDROID_LENS_FACING` di Camera HAL; `dumpsys media.camera` membuktikan Camera 0 hilang saat cold boot karena `s5c73c2 10-003d` gagal I2C (`-ENXIO`).

## 7. Reminder Wajib Saat Release
- **REQUIRED COMPANION ZIP: Deep Sleep Fix (`s2idle`)**: Sertakan `RisingOS-17-zeroflte-deepsleep-fix.zip` di release dan jelaskan urutan flash di TWRP:
  1. Flash `RisingOS-9-261126-1432-CORE-COMMUNITY-zeroflte.zip`
  2. Flash `RisingOS-17-zeroflte-deepsleep-fix.zip` *(opsional/jika perlu bila verifikasi pasca-flash belum menunjukkan `mem_sleep=[s2idle] deep`, karena `init.samsung.rc` sudah dibundel di dalam build `261126-1432`)*
