# Laporan Boot Sukses Android 15 (zeroflte - Samsung Galaxy S6 Flat)

Target Perangkat: **Samsung Galaxy S6 Flat (`zeroflte`, SM-G920I, Exynos 7420, Mali-T760 MP8)**  
OS: **Android 15 (Project Infinity-X 2.9)**  
Status Akhir: **`sys.boot_completed = 1` (BERHASIL BOOT KE LAUNCHER / GUI)**

---

## 1. Bukti Boot Selesai (`sys.boot_completed: 1`)

```text
06157df6085a9c3c:~$ getprop sys.boot_completed
1
```

### Proses Utama yang Aktif & Stabil:
| PID | Pengguna | Proses | Status |
|---|---|---|---|
| 245 | `system` | `android.hardware.graphics.composer@2.1-service` | Aktif (KMS HWC) |
| 247 | `system` | `/system/bin/surfaceflinger` | Aktif (Mesa Panfrost) |
| 434 | `root` | `zygote64` | Aktif |
| 435 | `root` | `zygote` | Aktif |
| 611 | `system` | `system_server` | Aktif |
| 841 | `u0_a555` | `com.android.systemui` | Aktif & Rendering Status Bar |
| 1008 | `system` | `com.android.settings` | Aktif |
| 1052 | `u0_a561` | `com.android.launcher3` | Aktif (Home Screen / Quickstep) |

---

## 2. Layer Tampilan Aktif (SurfaceFlinger WQXGA 1440x2560)

```text
RequestedLayerState{Display 0 name="Built-in Screen"}
RequestedLayerState{ActivityRecord{u0 com.android.launcher3/.uioverrides.QuickstepLauncher}}
RequestedLayerState{54bf47e com.android.launcher3/com.android.launcher3.uioverrides.QuickstepLauncher}
RequestedLayerState{StatusBar}
RequestedLayerState{NotificationShade}
RequestedLayerState{com.android.systemui.wallpapers.ImageWallpaper}
RequestedLayerState{[Gesture Monitor] swipe-up}
```

---

## 3. Screenshot UI Live (1440 x 2560)

Tangkapan layar asli dari framebuffer perangkat:

![Android 15 Launcher S6](file:///C:/Users/Angger/.gemini/antigravity/brain/39b01967-7542-4843-8c8c-39e380a9b6d8/android15_s6_boot_screen.png)

---

## 4. Solusi Teknis Inti yang Diterapkan

1. **Gralloc DRM Dumb DMA-BUF Kustom (`gralloc.samsungexynos7420.so` & `gralloc.default.so`)**:
   - Menghapus ketergantungan `ashmem` yang menyebabkan Panfrost GPU menolak mengimpor tekstur Skia (`Failed to create a valid texture`).
   - Mengalokasikan DRM Dumb buffer pada `/dev/dri/card0` dan mengekspornya menjadi genuine Linux DMA-BUF PRIME FD (`DRM_IOCTL_PRIME_HANDLE_TO_FD`).
   - Menambahkan mekanisme dynamic multi-process memory mapping (`hnd->pid != getpid()`) agar buffer yang dikirim via Binder dapat dipetakan secara valid di address space `composer_service`.

2. **KMS Hardware Composer Kustom (`hwcomposer.samsungexynos7420.so`)**:
   - Mengatasi symlink crash `hw_get_module` dengan dynamic runtime symbol resolution (`dlopen`/`dlsym`).
   - Menerapkan bounded memory copy (`copy_size = min(hnd->size, buf->size)`) untuk mencegah memory segmentation fault (`SEGV_MAPERR`) saat menerima layer berukuran di bawah resolusi penuh layar.
   - Mengontrol sinkronisasi buffer dan page flip langsung ke Samsung display panel via DRM KMS (`drmModePageFlip`).

3. **Kompilasi Ulang SELinux Policy (`precompiled_sepolicy`)**:
   - Memasukkan aturan `(typepermissive ...)` untuk seluruh domain esensial (`zygote`, `system_server`, `priv_app`, `platform_app`, `system_app`, `untrusted_app`, `shell`, `composer`, `surfaceflinger`, `vendor_init`).
   - Memperbaiki label file context `/vendor/odm/etc/selinux/odm_seapp_contexts` ke `seapp_contexts_file:s0` sehingga Zygote tidak lagi mengalami `JNI FatalError` saat melakukan fork pada proses Settings dan SystemUI.

4. **Integritas Watchdog**:
   - Watchdog kernel tetap aktif penuh pada batas waktu 900 detik (15 menit) tanpa modifikasi/patching `boot.img`.
