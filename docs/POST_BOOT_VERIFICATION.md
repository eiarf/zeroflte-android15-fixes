# Panduan Verifikasi Pasca-Booting — RisingOS-Revived Android 17 (`zeroflte`)

Gunakan daftar perintah `adb shell` berikut setelah perangkat **Samsung Galaxy S6 Flat (`SM-G920F`)** selesai melakukan booting dan **USB Debugging** diaktifkan, untuk memastikan kernel Linux 6.6.144 (`0001..0019`), SukiSU-Nova, SELinux, `deep` sleep (`mem_sleep`), watchdog `dsh_esslog`, seluruh HAL AIDL (termasuk `c2.v4l2.*` MFC v9), ZRAM 2 GiB, dan grafis Panfrost + `minigbm` bekerja normal 100%.

---

## 1. Status Boot, SELinux & Watchdog (`dsh_esslog`)

```bash
adb shell getprop sys.boot_completed
adb shell getenforce
adb shell cat /sys/module/dsh_esslog/parameters/autorecovery
```
**Output sehat yang diharapkan:**
- `sys.boot_completed`: **`1`**
- `getenforce`: **`Permissive`** (melalui `patches/system/core/0001-init-selinux-permissive-for-universal7420.patch` pada rilis `rising-17` aktif)
- `autorecovery`: **`0`** — menandakan watchdog `dsh_esslog` diset `900` di `early-init` dan baru berhasil di-disarm **setelah** `sys.boot_completed=1`.

*(Opsional)* Periksa apakah ada `avc: denied` kritis yang menghambat layanan HAL:
```bash
adb shell dmesg | grep -iE "avc:.*denied|selinux" | tail -n 30
```

---

## 2. Kernel Linux 6.6.144 & SukiSU-Nova

```bash
adb shell uname -a
adb shell dmesg | grep -iE "ksu|sukisu|dsh_esslog" | head -n 30
adb shell lsmod
```
**Output sehat yang diharapkan:**
- `uname -a`: menampilkan **`Linux ... 6.6.144-... aarch64`** (atau versi `6.6.x` sesuai `SM_G92X`).
- `lsmod`: memperlihatkan modul hardware termuat antara lain **`zram`**, **`zsmalloc`**, modul kamera (`s5c73c2`, `s5k4e6`, `ak7375`, `idg2030`, `imx_mipi_csis`, `exynos7_flite`, `spi_exynos_gpio`), audio (`snd_soc_cs47l85`, `snd_soc_zero_wm1840`, `snd_soc_max98925`, `madera*`, `arizona_micsupp`), Bluetooth (`bluetooth`, `btbcm`, `hci_uart`), serta `max86900` dan `sec_nfc`.
- Jika ingin memverifikasi konfigurasi build kernel di server build atau lewat `procfs` (jika `CONFIG_IKCONFIG_PROC` aktif):
  ```bash
  adb shell '[ -r /proc/config.gz ] && zcat /proc/config.gz | grep -E "CONFIG_KSU|CONFIG_KPM|CONFIG_CRYPTO_LZ4|CONFIG_ZRAM|CONFIG_BPF"'
  ```
  (Harus memuat `CONFIG_KSU=y`, `CONFIG_KSU_MANUAL_SU=y`, `CONFIG_KPM=y`, `CONFIG_CRYPTO_LZ4=y`, plus `slub_debug=FZP,kmalloc-16 stack_depot_disable=on` di `/proc/cmdline` dan `s2idle [deep]` di `/sys/power/mem_sleep`).
- Untuk menguji akses root **SukiSU-Nova** (`CONFIG_KSU_MANUAL_SU=y`), install APK Manager SukiSU-Nova resmi dan uji izin *su* dari aplikasi terminal / ADB.

---

## 3. Partisi `/system`, `/metadata` & Memori (RAM 3 GiB + ZRAM 2 GiB LZ4)

```bash
adb shell df -h / /system /vendor /metadata /data
adb shell cat /proc/swaps
adb shell cat /sys/block/zram0/comp_algorithm
adb shell free -m
adb shell cat /proc/sys/vm/swappiness /proc/sys/vm/page-cluster /proc/sys/vm/vfs_cache_pressure
adb shell getprop | grep ro.lmk
```
**Output sehat yang diharapkan:**
- `df -h`: `/system` ter-mount dan pemakaian masih berada di bawah batas fisik **`2979.3 MiB`** (`3124019200` byte); `/metadata` ter-mount rapi pada blok partisi `by-name/CACHE`.
- `/proc/swaps`: menampilkan `/dev/block/zram0` dengan ukuran sekitar **`2097148`** KB (~**2 GiB**, dari `zramsize=2147483648`).
- `/sys/block/zram0/comp_algorithm`: menunjukkan **`[lz4]`** yang aktif.
- Tuning `vm` sesuai `init.samsungexynos7420.rc`: `swappiness` = **`100`**, `page-cluster` = **`0`**, `vfs_cache_pressure` = **`60`**.
- `getprop | grep ro.lmk`: memunculkan nilai properti LMKD dari `device-common.mk` (bukan nilai bawaan generic ber-konflik).

---

## 4. Grafis (`s6kms` HWC, Mesa Panfrost, `minigbm`) & Resolusi Layar

```bash
adb shell ls -l /dev/dri
adb shell dumpsys SurfaceFlinger | head -n 45
adb shell ps -ef | grep -iE "composer|allocator|surfaceflinger"
adb shell wm size
adb shell wm density
```
**Output sehat yang diharapkan:**
- `/dev/dri`: terdapat `card0` dan `renderD128`.
- `dumpsys SurfaceFlinger`: GLES memakai renderer **Panfrost / Mali-T760** dan kompositor memakai `hwcomposer.s6kms` (`s6_kms_hwcomposer`).
- `wm size` & `wm density` default bawaan: **`Physical size: 1440x2560`** dan **`Physical density: 560`** tanpa *override* paksa.
- Di UI **Settings -> Display -> Screen resolution / Resolusi layar** (`com.samsung.exynos7420.displayresolution`), tersedia pilihan langsung **QHD+ Native (Default `1440 × 2560`)**, **FHD (`1080 × 1920 · 420 dpi`)**, dan **HD (`720 × 1280 · 320 dpi`)**.

**Uji Toggle Resolusi Dinamis (`/vendor/bin/set_resolution.sh` atau Settings UI):**
```bash
# Mode hemat daya HD 720p (720x1280 @ 320dpi):
adb shell /vendor/bin/set_resolution.sh hd

# Mode seimbang FHD 1080p (1080x1920 @ 420dpi):
adb shell /vendor/bin/set_resolution.sh fhd

# Kembali ke resolusi native QHD 1440p (1440x2560 @ 560dpi):
adb shell /vendor/bin/set_resolution.sh qhd
```

---

## 5. Audio & Dolby Atmos DAX v3.6

```bash
adb shell ps -ef | grep -iE "dms|audio"
adb shell ls -l /odm/etc/dolby/multimedia_dolby_dax_default.xml /vendor/lib/soundfx/libswdap_v3_6.so /vendor/lib/soundfx/libhwdap_v3_6.so
adb shell dumpsys media.audio_flinger | grep -iE "dap|dolby|6ab06da4|a0c30891|9d4921da"
```
**Output sehat yang diharapkan:**
- Proses **`vendor.dolby_v3_6.hardware.dms360@2.0-service`** (`dms-v36-hal-2-0`) dan **`android.hardware.audio.service`** berjalan aktif tanpa *crash loop*.
- File XML `/odm/etc/dolby/multimedia_dolby_dax_default.xml` dan kedua pustaka efek 32-bit `libswdap_v3_6.so` / `libhwdap_v3_6.so` tersedia.
- Pada `dumpsys media.audio_flinger` atau saat musik diputar dan profil **Dolby Atmos (`LunarisDolby` / `org.lunaris.dolby`)** diaktifkan dari Settings/Tile: efek proxy `dap` (`libeffectproxy.so`) memuat UUID `9d4921da-8225-4f29-aefa-39537a04bcaa` dan mengubah karakter suara speaker/headset tanpa error *AudioEffect* di `logcat`.

---

## 6. Verifikasi Seluruh HAL Hardware (Camera V3, GNSS V3, NFC, IR, Sensors, Thermal)

### 6.1 Ringkasan Layanan AIDL & HIDL Terdaftar
```bash
adb shell service list | grep -iE "camera|gnss|nfc|ir|sensors|thermal|vibrator|light|allocator"
adb shell lshal | grep -iE "dms360|audio|composer"
```

### 6.2 Kamera AIDL V3 + `SoftISP` + `gralloc_bridge`
```bash
adb shell ps -ef | grep -i camera
adb shell dumpsys media.camera | grep -E "Camera ID:|android\.hardware\.camera"
```
- Pastikan proses `android.hardware.camera.provider-V3-service` berjalan.
- Uji **foto belakang (autofokus `ak7375` + flash), foto depan (`s5k4e6`), serta perekaman video (1080p)**: pastikan pratinjau tidak beku dan hasil foto/video tersimpan tanpa crash `C2AllocatorGralloc` / `gralloc_bridge` di `adb logcat -b all -d | grep -iE "gralloc|camera"`.

### 6.3 GNSS AIDL V3 (`gpsd` / `lhd`)
```bash
adb shell ps -ef | grep -iE "gnss|gpsd|lhd"
adb shell dumpsys android.hardware.gnss.IGnss/default
adb shell dumpsys location | head -n 40
```
- Pastikan `android.hardware.gnss-service.universal7420`, `gpsd`, dan `lhd` hidup serta layanan lokasi mendeteksi satelit di ruang terbuka.

### 6.4 NFC AIDL V1, IR LED AIDL V1, Sensors Multi-HAL & Pixel Thermal
```bash
# NFC (s3fwrn5)
adb shell ps -ef | grep -i nfc
adb shell ls -l /dev/sec-nfc
adb shell dumpsys nfc | head -n 30

# IR LED Blaster
adb shell ps -ef | grep -i "vendor.ir-hal\|hardware.ir"
adb shell ls -l /sys/class/sensors/irled_remote/irled_send

# Sensors Multi-HAL (IIO + MAX86900 Heart Rate / SpO2)
adb shell cat /vendor/etc/sensors/hals.conf 2>/dev/null || adb shell find /vendor -name hals.conf
adb shell dumpsys sensorservice | head -n 60

# Pixel AIDL Thermal (exynos-therm + battery)
adb shell dumpsys thermalservice
```
**Output sehat yang diharapkan:**
- NFC aktif di Settings dan `android.hardware.nfc-service.exynos7420` berkomunikasi dengan `/dev/sec-nfc`.
- IR HAL `android.hardware.ir-service.universal7420` berjalan dan node `/sys/class/sensors/irled_remote/irled_send` memiliki izin tulis untuk `system`.
- `dumpsys sensorservice` mendaftarkan akselerometer/giroskop/magnetometer/proksimitas/cahaya IIO sekaligus sensor `max86900`.
- `dumpsys thermalservice` melaporkan pembacaan suhu valid untuk sensor `exynos-therm` (`CPU`) dan `battery` (`BATTERY`).

---

## 7. Konektivitas (Wi-Fi BCM4358, Bluetooth, Modem RIL) & CPU/GPU Governors

```bash
# Wi-Fi BCM4358
adb shell getprop wifi.interface
adb shell ip addr show wlan0
adb shell ls -l /vendor/firmware/brcm/brcmfmac4358* /vendor/firmware/brcm/BCM4358A3*

# Bluetooth & Modem/Teleponi
adb shell dumpsys bluetooth_manager | head -n 25
adb shell getprop gsm.version.baseband
adb shell dumpsys telephony.registry | head -n 30

# CPU/GPU Governors pasca-boot
adb shell cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor /sys/devices/system/cpu/cpufreq/policy1/scaling_governor
adb shell cat /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq
adb shell cat /sys/class/devfreq/14ac0000.gpu/min_freq
```
**Output sehat yang diharapkan:**
- `wifi.interface`: **`wlan0`**, terhubung stabil ke jaringan 2.4 GHz & 5 GHz.
- Bluetooth menyala dan mampu memindai/terhubung ke perangkat audio BT (`bluetooth.audio-impl:32`).
- Baseband `gsm.version.baseband` terbaca (tidak kosong/`unknown`), IMEI dan sinyal SIM terbaca (`1` SIM card siap panggilan/data seluler).
- `policy0` & `policy1` governor = **`schedutil`** dengan `scaling_min_freq` masing-masing **`700000`** dan **`1000000`**, serta GPU `min_freq` = **`420000000`**.
