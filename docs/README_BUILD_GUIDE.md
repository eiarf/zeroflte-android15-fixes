# Panduan Build & Instalasi — RisingOS-Revived Android 17 (`zeroflte`)

Dokumen ini merupakan panduan resmi dan runbook eksekusi server Linux untuk membangun **RisingOS-Revived Android 17** (`seventeen`) pada **Samsung Galaxy S6 Flat (`zeroflte` / `SM-G920F`, Exynos 7420)** menggunakan kernel **Linux 6.6.144 LTS** (`eiarf/SM_G92X`) dengan **SukiSU-Nova** tertanam (*built-in*), tumpukan **HAL Pure-AIDL / Multi-HAL**, grafis **Mesa Panfrost 24.0.9 + `minigbm` + `hwcomposer.s6kms`**, serta **Dolby Atmos DAX v3.6**.

> [!IMPORTANT]
> Seluruh skrip di bundel ini dirancang untuk dijalankan di **build server Linux (Ubuntu 22.04 LTS / 24.04 LTS x86_64)**, **bukan** dikompilasi di Windows lokal.

---

## 1. Arsitektur Bundel & Daftar Isi (`zeroflte-rising17`)

Bundel kerja ini berada di `D:\zeroflte-rising17\` (atau direktori tujuan salinan di server Linux Anda) dengan struktur:

| Direktori / File | Peran & Isi |
| :--- | :--- |
| `manifests/zeroflte_rising17.xml` | Local manifest `.repo/local_manifests/zeroflte.xml` untuk vendor blob (`eiarf/proprietary_vendor_samsung` branch `lineage-24.0-KRNL6.18`), Exynos 7420 SLSI/Linaro HALs (`lineage-21` / `lineage-21.0`), `hardware/samsung`, dan `device/samsung_slsi/sepolicy` (`lineage-24.0`). |
| `local_repos/device_samsung_universal7420-common` | Common device tree Exynos 7420: HAL AIDL (Camera V3 + `SoftISP` + `gralloc_bridge.cpp`, GNSS V3, NFC V1, IR V1, Light, Vibrator, ChargingControl), Sensors Multi-HAL (`sensors.universal7420-iio.so` + `sensors.max86900.so`), `BoardConfigCommon.mk`, `device-common.mk`, Codec2 AIDL configs/seccomp/sepolicy + `mfc_fw.bin`, `init.power.rc`/`powerhint.json`, `system.prop` (`persist.sys.battery_bypass_supported=true`), `fstab` `/metadata` + ZRAM 2 GiB, `set_resolution.sh`, `releasetools.py`, dan `prebuilt/` (`Image`, `dt.img`, `modules/*.ko`). |
| `local_repos/device_samsung_zeroflte` | Device tree khusus Galaxy S6 Flat: `lineage_zeroflte.mk`, `BoardConfig.mk` (`WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true`), `configs/thermal/thermal_info_config.json`, `configs/nfc/*`, RRO `WifiOverlay`, serta `sepolicy/vendor/file_contexts`. |
| `local_repos/vendor_nk` | Komponen grafis, Wi-Fi, Bluetooth BCM4358 (branch `rising-17`): `hwcomposer.s6kms` (`s6_kms_hwcomposer.cpp` yang mendukung magic handle `0xABCDDCBAU` `CrosGrallocHandle` dan `0x3141592` `S6Gralloc`), prebuilt Mesa Panfrost 24.0.9, firmware `brcmfmac4358-pcie*`, serta `BCM4358A3*.hcd`. (`modem/` sengaja dihapus agar `init.nk-modem.rc` tidak duplikat dengan `universal7420-common/ramdisk/Android.mk`). |
| `local_repos/vendor_lunaris_dolby` | Repo **Dolby Atmos DAX v3.6** yang sudah dipangkas bersih (branch `rising-17`): hanya menyimpan komponen `_v3_6`, HIDL `vendor.dolby_v3_6.hardware.dms360@2.0-service` (`dms-v36-hal-2-0`), efek audio 32-bit (`libswdap_v3_6.so`, `libhwdap_v3_6.so`), `LunarisDolby` (`org.lunaris.dolby`), dan `XiaomiDolbyResCommon` (tanpa `_sp`, `media.c2`, `ddpdec`, ataupun `mediacodec.te`). |
| `local_repos/sukisu_nova_kernel` | Source `kernel/` **SukiSU-Nova** dengan `include/uapi/*.h` yang sudah di-materialize sebagai file asli (bukan symlink Windows rusak) dan `Kbuild` bertimeout `curl --connect-timeout 2 --max-time 3` serta dukungan mode offline `KSU_OFFLINE=1` (fallback versi `31200`). |
| `patches/` | Total **36 patch**: `19` patch kernel (`patches/kernel/0001`..`0019`), `14` patch platform/HAL/Settings/Codec2 (`bionic`, `build/make`, `build/soong`, `external/minigbm`, `external/v4l2_codec2`, `frameworks/{base,native,opt/telephony}`, `hardware/{interfaces,samsung_slsi-linaro/exynos}`, `packages/apps/Settings`, `system/{core,tools/mkbootimg}`), dan `3` patch vendor slimming (`vendor/{addons,gms,rising}`). |
| `scripts/` | Skrip otomasi server (`server_sync_and_setup.sh`, `apply_patches.sh`, `build_kernel_linux.sh`, `build_rom.sh`, `mkdtbh.py`, `patch_kernel_image.py`) dan verifikator pre-flight (`verify_setup.py`). |
| `docs/` | Dokumentasi panduan build (`README_BUILD_GUIDE.md`) dan verifikasi pasca-booting (`POST_BOOT_VERIFICATION.md`). |

---

## 2. Highlight Teknis & Optimasi Hardware (`SM-G920F`)

### 2.1 Kernel Linux 6.6.144 + SukiSU-Nova + Samsung S-Boot Header
- Kernel dibangun dari `eiarf/SM_G92X` (`lineage-22.2-android15-6.6`, base commit `bfc9ce877`) dengan penggabungan `gki_defconfig` + `zeroflte_gki.fragment` + seri patch `patches/kernel/0001`..`0019`.
- **SukiSU-Nova built-in**: `CONFIG_KSU=y`, `CONFIG_KSU_MANUAL_SU=y`, dan `CONFIG_KPM=y` (`0007`) bersama `slub_debug=FZP,kmalloc-16 stack_depot_disable=on` (`0012`).
- **Kompatibilitas Bootloader S-Boot**: `scripts/patch_kernel_image.py` memastikan `text_offset=0x205000` pada `Image` arm64, dan `scripts/mkdtbh.py` mengemas `exynos7420-zeroflte.dtb` menjadi `dt.img` berformat **Samsung `DTBH` v2**.
- **Aturan Kritis Watchdog (`dsh_esslog`)**:
  > [!CAUTION]
  > Tombol fisik (Power/Volume) pada unit uji Galaxy S6 keras/aus, sehingga mekanisme pemulihan otomatis kernel sangat penting selama proses booting.
  > **JANGAN PERNAH** menonaktifkan `dsh_esslog` di `on early-init`! Di `ramdisk/etc/init.samsungexynos7420.rc`:
  > - `on early-init` menyetel `write /sys/module/dsh_esslog/parameters/autorecovery 900` (batas tunggu 15 menit untuk booting awal + dexopt).
  > - Watchdog baru di-disarm (`write /sys/module/dsh_esslog/parameters/autorecovery 0`) pada blok `on property:sys.boot_completed=1`.

### 2.2 SELinux Status (`Permissive`)
- Untuk kompatibilitas penuh seluruh HAL/daemon `zeroflte` Android 17 (termasuk `v4l2_codec2` AIDL, `VFS7XXX` TZ fingerprint, dan `init.postboot.sh`), rilis aktif menyetel `Permissive` melalui `patches/system/core/0001-init-selinux-permissive-for-universal7420.patch`.

### 2.3 Grafis, Kamera AIDL V3 & Periferal Pure-AIDL
- **Display & GPU**: Mesa Panfrost 24.0.9 + `hwcomposer.s6kms` + `external/minigbm` (dengan backend `exynos` dumb + Allocator AIDL V3 & Mapper 5 + `gralloc.minigbm.so` 32/64-bit).
- **Kamera AIDL V3**: `android.hardware.camera.provider-V3-service` (`ICameraProvider/internal/0`) dengan `SoftISP` (sensor belakang `s5c73c2`, depan `s5k4e6`, AF `ak7375`, OIS `idg2030`) dan `gralloc_bridge.cpp` yang membaca `CrosGrallocHandle` (`0xABCDDCBAU`) `total_size`/`sizes[0]` dari `gralloc.minigbm.so` agar perekaman video / JPEG BLOB tidak crash.
- **HAL Lainnya**: GNSS AIDL V3 (`android.hardware.gnss-service.universal7420` + `gpsd`/`lhd`), NFC AIDL V1 (`android.hardware.nfc-service.exynos7420` atas `/dev/sec-nfc`), IR AIDL V1 (`android.hardware.ir-service.universal7420` atas `/sys/class/sensors/irled_remote/irled_send`), Sensors Multi-HAL (`sensors.universal7420-iio.so` + `sensors.max86900.so`), dan Pixel AIDL Thermal (`thermal_info_config.json` untuk `exynos-therm` & `battery`).
- **Resolusi Layar (`Settings -> Display -> Resolusi layar`)**: Secara bawaan (*default*) berjalan di resolusi fisik asli panel **QHD+ (`1440x2560 @ 560dpi`)**. Opsi **Screen resolution / Resolusi layar** (`DisplayResolution` di `system_ext/priv-app`) terintegrasi langsung di menu **Settings -> Display** sehingga pengguna dapat beralih kapan saja antara **QHD+ Bawaan (Default `1440x2560`)**, **FHD (`1080x1920 @ 420dpi`)**, atau **HD (`720x1280 @ 320dpi`)** secara instan (juga tersedia via CLI `/vendor/bin/set_resolution.sh` `qhd`/`fhd`/`hd`).

### 2.4 Dolby Atmos DAX v3.6
- Terpasang melalui `vendor/lunaris/dolby` (`dolby.mk`) dengan daemon HIDL `dms-v36-hal-2-0` (`/odm/bin/hw/vendor.dolby_v3_6.hardware.dms360@2.0-service`), konfigurasi XML `/odm/etc/dolby/multimedia_dolby_dax_default.xml`, dan aplikasi sistem `LunarisDolby` (`org.lunaris.dolby`).
- Dihubungkan ke `audio_effects.xml` lewat `<effectProxy name="dap">` (`libeffectproxy.so`) menuju `libswdap_v3_6.so` (`6ab06da4-c516-4611-8166-452799218539`) dan `libhwdap_v3_6.so` (`a0c30891-8246-4aef-b8ad-d53e26da0253`) dengan UUID tipe `9d4921da-8225-4f29-aefa-39537a04bcaa` sesuai `kDescriptor` biner asli.

### 2.5 Optimasi RAM 3 GiB, GApps Slim & Batas Partisi `/system` (`2979.3 MiB`)
- Partisi fisik `SYSTEM` pada `SM-G920F` berukuran **`3124019200` byte (`2979.3 MiB`)** (tanpa dynamic partitions).
- Agar build **dengan Core GApps** tetap muat:
  - `lineage_zeroflte.mk` mengaktifkan `WITH_GMS ?= true` + `TARGET_USES_PICO_GAPPS ?= true` (otomatis menyetel `RISING_PACKAGE_TYPE ?= CORE` di `vendor/rising/config/version.mk`), serta menonaktifkan blur (`TARGET_ENABLE_BLUR := false`) dan Pixel Launcher (`TARGET_DEFAULT_PIXEL_LAUNCHER := false` sehingga menggunakan `Launcher3QuickStep`).
  - `BoardConfig.mk` menyetel `WITH_DEXPREOPT_BOOT_IMG_AND_SYSTEM_SERVER_ONLY := true`.
  - `patches/vendor/gms/0001-gms-pico-slim-for-3gb-ram-and-small-system.patch` merampingkan aplikasi GApps berat yang tidak esensial pada target 3 GiB RAM & partisi `/system` kecil.
  - `patches/vendor/addons/0001-addons-drop-optional-heavy-emoji-fonts-for-small-system.patch` menghapus font emoji pihak ketiga opsional (~114 MiB) dan mempertahankan `NotoColorEmoji` standar.
- **ZRAM & LMKD**: `fstab.samsungexynos7420.zero` mengonfigurasi **2 GiB ZRAM** (`zramsize=2147483648`) dengan kompresi **`lz4`** (`CONFIG_ZRAM=m`, `CONFIG_CRYPTO_LZ4=y`), `swappiness=100`, `page-cluster=0`, `vfs_cache_pressure=60`, serta override `ro.lmk.*` di `PRODUCT_PRODUCT_PROPERTIES`.

---

## 3. Prasyarat Build Server Linux

### 3.1 Kebutuhan Hardware Server
- OS: **Ubuntu 22.04 LTS atau 24.04 LTS (x86_64)**
- RAM: Minimal **32 GB RAM** (+ disarankan minimal 16–32 GB swap agar proses LTO/LLVM & Soong tidak OOM)
- Penyimpanan: Minimal **350–450 GB ruang kosong SSD/NVMe** (source tree + `out/` + `ccache`)

### 3.2 Instalasi Paket Dependensi Ubuntu
Jalankan perintah berikut di build server Linux:
```bash
sudo apt-get update && sudo apt-get install -y \
    bc bison build-essential ccache curl flex g++-multilib gcc-multilib \
    git git-lfs gnupg gperf imagemagick lib32ncurses-dev lib32readline-dev \
    lib32z1-dev libelf-dev liblz4-tool libncurses-dev libsdl1.2-dev \
    libssl-dev libxml2 libxml2-utils lld llvm lzop pngcrush rsync \
    schedtool squashfs-tools xsltproc zip zlib1g-dev python3 \
    openjdk-17-jdk android-sdk-libsparse-utils
```
*(Catatan: pada Ubuntu 22.04 apabila `lib32ncurses-dev` atau `libncurses-dev` belum tersedia dengan nama tersebut, sesuaikan dengan `libncurses5 libncurses5-dev`; `android-sdk-libsparse-utils` menyediakan `simg2img` yang dipakai `build_rom.sh` untuk memverifikasi ukuran mentah ext4 `system.img`).*

Pastikan tools `repo` dan `git-lfs` siap serta identitas Git sudah dikonfigurasi:
```bash
git lfs install
git config --global user.name "Nama Anda"
git config --global user.email "email@example.com"
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+x ~/.bin/repo
export PATH="$HOME/.bin:$PATH"
```

---

## 4. Langkah Eksekusi Build di Server Linux

### Langkah 1: Transfer Bundel ke Server Linux
Salin folder `zeroflte-rising17` dari Windows (atau WSL `/mnt/d/zeroflte-rising17`) ke server Linux Anda, misalnya ke `~/zeroflte-rising17`:
```bash
# Contoh dari mesin lokal yang terhubung ke /mnt/d/zeroflte-rising17:
rsync -avhP --exclude '__pycache__' /mnt/d/zeroflte-rising17/ user@build-server:~/zeroflte-rising17/
```
Pastikan hak eksekusi skrip di server Linux aktif:
```bash
chmod +x ~/zeroflte-rising17/scripts/*.sh ~/zeroflte-rising17/scripts/*.py
```

### Langkah 2: Sync Source RisingOS 17, Install Local Repos & Apply Patches
Jalankan `server_sync_and_setup.sh` dengan target direktori build (default `$HOME/rising17`):
```bash
bash ~/zeroflte-rising17/scripts/server_sync_and_setup.sh ~/rising17
```
Skrip ini secara berurutan melakukan:
1. `repo init -u https://github.com/RisingOS-Revived/android.git -b seventeen --git-lfs`
2. Memasang `manifests/zeroflte_rising17.xml` ke `.repo/local_manifests/zeroflte.xml`
3. Menjalankan `repo sync -c -j$(nproc) --force-sync --no-clone-bundle --no-tags` + `git -C "$ROM_DIR/vendor/gms" lfs pull` (wajib agar APK GApps seperti `Phonesky.apk` tidak berupa file pointer 130 byte!)
4. Menyalin `device_samsung_universal7420-common`, `device_samsung_zeroflte`, `vendor_nk`, dan `vendor_lunaris_dolby` ke direktori source tree yang sesuai
5. Memanggil `scripts/apply_patches.sh` untuk memasang seluruh patch platform, `minigbm`, `mkbootimg`, `bionic`, `telephony`, `exynos`, `gms`, dan `addons`.

**Opsi Variabel Lingkungan untuk `server_sync_and_setup.sh`**:
- `JOBS=16` — atur jumlah thread `repo sync`.
- `INIT_ONLY=1` — lewati `repo sync` (hanya pasang ulang manifest, `local_repos`, dan patch di source tree yang sudah ada).
- `SKIP_GMS=1` — lewati `git lfs pull` dan patch pada `vendor/gms` (berguna bila hendak membangun edisi **Vanilla** tanpa GApps).

### Langkah 3: Build Kernel Linux 6.6.144 + SukiSU-Nova
Setelah source tree RisingOS selesai di-sync (atau minimal direktori `$ROM_DIR/device/samsung/universal7420-common/prebuilt` sudah terpasang oleh Langkah 2), bangun kernel beserta `dt.img` (`DTBH` v2) dan modul `.ko`:
```bash
bash ~/zeroflte-rising17/scripts/build_kernel_linux.sh ~/rising17
```
Yang dilakukan skrip ini:
1. Mendeteksi toolchain LLVM/Clang + `ld.lld` (prioritas: AOSP `prebuilts/clang`, lalu `$ANDROID_NDK_HOME`, lalu Clang sistem `/usr/lib/llvm-*/bin` atau `/usr/bin`).
2. Meng-clone `https://github.com/eiarf/SM_G92X.git` (`lineage-22.2-android15-6.6`) ke `$ROM_DIR/kernel_src/SM_G92X`.
3. Menyuntikkan `local_repos/sukisu_nova_kernel` ke `drivers/kernelsu` dan memasang seluruh `patches/kernel/0001`..`0019` (`build_kernel_linux.sh` memakai penanda SHA-256 idempoten di `.git/zeroflte_patches/`).
4. Menggabungkan `gki_defconfig` + `zeroflte_gki.fragment`, memverifikasi `CONFIG_KSU=y`, `CONFIG_KSU_MANUAL_SU=y`, `CONFIG_HEADERS_INSTALL=y`, `CONFIG_CRYPTO_LZ4=y`, lalu menjalankan `headers_install` dan menghasilkan `include/generated/dsh_build_id.h` (untuk `dsh_esslog`).
5. Mengompilasi `Image`, `exynos/exynos7420-zeroflte.dtb`, dan modul `.ko`.
6. Menjalankan `patch_kernel_image.py` (`text_offset=0x205000`), `mkdtbh.py` (`DTBH` v2), dan menyalin `Image`, `dt.img`, serta seluruh `.ko` ke `$ROM_DIR/device/samsung/universal7420-common/prebuilt/`.

**Opsi Variabel Lingkungan untuk `build_kernel_linux.sh`**:
- `KSU_OFFLINE=1` — paksa SukiSU-Nova `Kbuild` menggunakan versi offline fallback `31200` tanpa menghubungi GitHub sama sekali (penting untuk server tanpa internet / belakang firewall; skrip default sudah memasang timeout `curl` 2–3 detik sehingga tidak akan hang lama).
- `LLVM_BIN=/path/to/clang/bin` — tentukan path binari `clang` & `ld.lld` secara manual.
- `KERNEL_SRC=...` / `KERNEL_OUT=...` / `JOBS=...` — kustomisasi lokasi source/output/jumlah thread.

### Langkah 4: Kompilasi ROM RisingOS-Revived Android 17
Jalankan skrip build utama ROM:
```bash
# Default: Core GApps Slim (WITH_GMS=true, TARGET_USES_PICO_GAPPS=true)
bash ~/zeroflte-rising17/scripts/build_rom.sh ~/rising17
```

**Toggle Build Vanilla (Tanpa GApps)**:
```bash
WITH_GMS=false bash ~/zeroflte-rising17/scripts/build_rom.sh ~/rising17
```

**Opsi Variabel Lingkungan Tambahan (`build_rom.sh`)**:
- `BUILD_TYPE=userdebug` (default; dapat diubah ke `user` atau `eng`)
- `CLEAN_MODE=none` (default; ubah ke `installclean` saat mengganti `WITH_GMS=true`/`false`, atau `clean` untuk bersih total)
- `JOBS=$(nproc)` — atur konkurensi `mka bacon`.

Di akhir kompilasi, `build_rom.sh` otomatis memeriksa:
1. Validitas header `prebuilt/Image` (`text_offset=0x205000`) dan `prebuilt/dt.img` (`DTBH` v2).
2. Jika `WITH_GMS=true`, memastikan `Phonesky.apk` di `vendor/gms` bukan pointer Git LFS (`>= 10000` byte).
3. Ukuran raw ext4 `system.img` tidak melewati batas fisik partisi **`3124019200` byte (`2979.3 MiB`)** — jika melebihi, skrip menghentikan proses dengan pesan error.

Output flashable OTA ZIP serta image boot/recovery tersimpan di:
- `~/rising17/out/target/product/zeroflte/RisingOS*.zip`
- `~/rising17/out/target/product/zeroflte/boot.img`
- `~/rising17/out/target/product/zeroflte/recovery.img`

---

## 5. Panduan Flashing ke `zeroflte` (`SM-G920F`)

### 5.1 Pemetaan Partisi Kritis (`/metadata` pada `CACHE`)
Perhatikan konfigurasi pada `fstab.samsungexynos7420.zero`:
- `/system` -> `/dev/block/platform/15570000.ufs/by-name/SYSTEM` (`ext4`, `first_stage_mount`, maksimal `2979.3 MiB`)
- `/metadata` -> `/dev/block/platform/15570000.ufs/by-name/CACHE` (`ext4`, `wait,check,formattable,first_stage_mount`)
- `/data` -> `/dev/block/platform/15570000.ufs/by-name/USERDATA` (`f2fs` atau fallback `ext4`)
- `/efs` -> `/dev/block/platform/15570000.ufs/by-name/EFS` (**jangan pernah di-wipe!**)

> [!WARNING]
> Android 17 memakai partisi fisik **`CACHE`** UFS sebagai **`/metadata`** untuk penyimpanan state enkripsi & checkpointing Android. Pastikan partisi `Cache` bersih (di-wipe/format sebagai ext4 bersih saat Clean Flash via TWRP) agar `first_stage_mount` berhasil melakukan mount `/metadata` saat boot pertama.

### 5.2 Langkah Flash via TWRP
1. *(Opsional jika perlu update recovery / boot image terpisah via Download Mode)*:
   ```bash
   heimdall flash --BOOT boot.img --RECOVERY recovery.img --no-reboot
   ```
   Lalu masuk langsung ke Recovery (TWRP).
2. Pada **Clean Flash** (sangat disarankan saat pertama berpindah ke RisingOS 17):
   - Di TWRP: masuk ke **Wipe -> Advanced Wipe**, centang **Dalvik / ART Cache**, **Cache**, **System**, dan **Data** lalu geser untuk wipe.
   - Jika berpindah dari ROM dengan tipe enkripsi `/data` berbeda atau gagal mount `/data`: masuk **Wipe -> Format Data**, ketik `yes`, lalu **Reboot -> Recovery**.
3. Flash ROM ZIP:
   - Melalui ADB Sideload di TWRP (**Advanced -> ADB Sideload**):
     ```bash
     adb sideload RisingOS*.zip
     ```
   - Atau salin file ZIP ke penyimpanan internal dan install lewat menu **Install**.
4. **Reboot System**.
   > [!NOTE]
   > Pada boot pertama (*first boot*), biarkan perangkat berjalan tanpa diganggu selama beberapa menit (hingga optimasi ART & setup pertama selesai). Watchdog kernel `dsh_esslog` menjaga batas waktu hingga **900 detik (15 menit)** selama proses early boot sebelum otomatis dimatikan setelah `sys.boot_completed=1`.

Setelah masuk ke homescreen dan mengaktifkan **USB Debugging**, lanjutkan pemeriksaan kesehatan seluruh subsistem di [POST_BOOT_VERIFICATION.md](./POST_BOOT_VERIFICATION.md).
