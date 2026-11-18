# Samsung Galaxy S6 (zeroflte) - Android 15 & Kernel 6.6 Fixes & Optimizations

This repository contains diagnostic tools, patched configuration files, kernel and hardware abstraction fixes, performance enhancements (ZRAM, CPU/GPU scaling), logs, and comprehensive technical documentation developed for running **Android 15 (LineageOS 22.2)** on the **Samsung Galaxy S6 (`zeroflte` / SM-G920F / Exynos 7420)** powered by the **Mainline Linux Kernel 6.6**.

---

## 🔗 Official Forked Repositories

All source modifications have been applied and pushed to the following repositories under `@eiarf` (branch: `lineage-22.2-KRNL6.6`):

1. **Kernel (`SM_G92X`)**:
   [https://github.com/eiarf/SM_G92X](https://github.com/eiarf/SM_G92X) (`lineage-22.2-android15-6.6`, `camera-v4l2-bringup`)
   - V4L2/MediaController rear (`s5c73c2`/`s5k2p2`) + front (`s5k4e6`) camera pipeline, `ak7345`/`ak7375` AF VCM, `idg2030` OIS, `s2mpb02` FLED flash/torch, `exynos7-flite` SysMMU_CAM0 pass-through, `clk-exynos7` CAM/ISP PLL retention, and `cam_cma` (160 MiB reusable CMA).
   - Panfrost Mali-T760 fixes (`panfrost_dump`, `panfrost_mmu`, 2000 ms timeout).
   - Hardware bring-up: `max86900` heart-rate IIO, `sec_nfc` S3FWRN5 NFC (`/dev/sec-nfc`), BCM4773 SSP IR blaster, and `hsi2c_6` secure-pins recovery.

2. **Common Device Tree (`universal7420-common`)**:
   [https://github.com/eiarf/android_device_samsung_universal7420-common](https://github.com/eiarf/android_device_samsung_universal7420-common) (`lineage-22.2-KRNL6.6`, `camera-v4l2-aidl-hal`)
   - AIDL V3 V4L2/SoftISP Camera HAL (`hardware/camera/`), `NK_HAS_CAMERA/NFC/GPS/IR=true`.
   - Patched `powerhint.json`, `task_profiles.json` (ADPF), `init.power.rc`, `init.samsungexynos7420.rc`, `init.gps.rc`, `ueventd.samsungexynos7420.rc`, `init.postboot.sh`, `set_resolution.sh`, and SELinux policies.

3. **Device Tree (`zeroflte`)**:
   [https://github.com/eiarf/android_device_samsung_zeroflte](https://github.com/eiarf/android_device_samsung_zeroflte) (`lineage-22.2-KRNL6.6`)
   - `WifiOverlay` BCM4358 compatibility overrides (disable suspend optimizations, FW roaming HAL, BT coex, connected MAC randomization).

4. **Proprietary Vendor Blobs (`proprietary_vendor_samsung`)**:
   [https://github.com/eiarf/proprietary_vendor_samsung](https://github.com/eiarf/proprietary_vendor_samsung)
   - Vendor proprietary blobs, firmware files, and display/audio HAL dependencies for Exynos 7420 on Android 15.

---

## 🚀 Key Issues Resolved

### 1. Watchdog Bootloop & Autorecovery Disarm (`dsh_esslog`)
* **Problem**: When booting or loading heavy Android 15 framework components, the kernel watchdog (`dsh_esslog`) autorecovery mechanism triggered unexpected hardware reboots back to TWRP recovery.
* **Fix**:
  - Disarmed kernel autorecovery on `sys.boot_completed=1` via `/sys/module/dsh_esslog/parameters/autorecovery = 0`.
  - Allowed uninterrupted booting into the Android 15 user interface.

### 2. Screen Blanking & Touchscreen Lockup
* **Problem**: Unlocking the device resulted in frozen touch response or blank screen due to touchscreen inhibit states being aggressively tied to early suspend/resume cycles.
* **Fix**:
  - Decoupled touch inhibit triggers in `init.power.rc` and `init.touch.sh`.
  - Configured input device listeners to remain persistent across screen transitions.

### 3. Sluggish Performance & Low Free Memory
* **Problem**: Exynos 7420 has 3GB LPDDR4 RAM, but Android 15 framework + system processes consumed nearly ~2.65GB, leaving only ~350MB free RAM. Additionally, CPU/GPU frequencies were idling at deep energy-saving states (Little 400MHz, Big 800MHz, Mali GPU 260MHz), causing severe micro-stutters during UI interactions.
* **Fix**:
  - **ZRAM 2GB Swap**: Dynamically insmodded `zsmalloc.ko` and `zram.ko`, configured a 2048 MB swap disk on `/dev/block/zram0`, and reinitialized `lmkd` (low memory killer daemon). Free RAM increased to **~740 MB+**.
  - **GPU Floor Elevate**: Set Mali-T760 MP8 scaling floor to **420 MHz** (`scaling_min_freq = 420000000`).
  - **CPU Cluster Floors**: Set LITTLE cluster floor to **600 MHz** (`cpu0`) and BIG cluster floor to **1000 MHz** (`cpu4`).
  - **UCLAMP & PowerHAL Tuning**: Elevated top-app uclamp boost to `15.00` during user touch interactions in `powerhint.json`, delivering responsive 60 FPS transitions.

### 4. Camera, Graphics, Wi-Fi, GNSS, NFC, IR & Heart Rate Bring-Up
* **Camera (`ICameraProvider/internal/0`)**: **WORKING** — AIDL V3 V4L2/SoftISP HAL + mainline 6.6 V4L2/MediaController pipeline (Rear 16MP `s5c73c2`/`s5k2p2`, Front 5MP `s5k4e6`, `ak7345` contrast AF, `idg2030` OIS, `s2mpb02` Flash/Torch, 1080p video).
* **DRM/KMS + Panfrost GPU**: **WORKING** — `s6_kms_hwcomposer.cpp` (`drmModeAddFB2` XBGR8888), thread-safe 32/64-bit `s6_drm_gralloc.cpp`, Mesa 24.0.9 Panfrost, and kernel MMU/devcoredump/timeout fixes.
* **GNSS, NFC, IR Blaster & MAX86900 Heart Rate**: **WORKING** — AIDL GNSS HAL + `gpsd`/`lhd` (`BCM4773`), AIDL NFC HAL + `S3FWRN5P` (`/dev/sec-nfc`), AIDL IR HAL (`irled_remote`), and MAX86900 optical IIO sub-HAL, all running under SELinux **Enforcing**.

---

## 📁 Repository Directory Structure

```text
├── README.md                           # Main documentation and guide
├── configs/                            # System and ramdisk configuration files
│   ├── init.postboot.sh                # Post-boot tuning script (ZRAM, CPU/GPU floors)
│   ├── init.power.rc                   # Power & touch init configuration
│   ├── init.samsungexynos7420.rc       # Main board init script
│   ├── init.touch.sh                   # Touchscreen controller initialization
│   ├── permissive_rules.cil            # SELinux debug permissive policy
│   ├── powerhint.json                  # Tuned PowerHAL configuration
│   ├── system_init.rc                  # Cleaned system init configuration
│   └── vendor_build.prop               # Vendor build properties
├── hwc-gralloc/                        # Hardware composer and graphics buffer sources
│   ├── s6_drm_gralloc.cpp              # DRM/KMS gralloc implementation
│   ├── s6_kms_hwcomposer.cpp           # Hardware composer KMS backend
│   ├── test_destroy_dumb.c             # DRM dumb buffer lifecycle unit test
│   └── test_dma_mmap.c                 # DMA-BUF mmap validation test
├── scripts/                            # Automation and patch scripts
│   ├── add_postboot_service.py         # Injects vendor.postboot init service
│   ├── analyze_last_kmsg.py            # Kernel crash log analyzer
│   ├── patch_build_prop.py             # Vendor build property modifier
│   ├── patch_powerhint.py              # Powerhint JSON updater
│   ├── patch_touch_sh.py               # Touch driver helper script
│   ├── restore_powerhint.py            # Restores baseline PowerHAL config
│   └── update_touch_rules.py           # Updates touch event handlers
├── logs/                               # Diagnostic and recovery logs
│   ├── last_kmsg.txt                   # Kernel crash log capture
│   ├── last_kmsg_blank.txt             # Blank screen kernel trace
│   └── recovery.log                    # TWRP recovery debugging log
└── reports/                            # Technical reports and screenshots
    ├── screen.png                      # Screenshot of Android 15 booting on S6
    ├── zeroflte_boot_recovery_plan.md  # Step-by-step boot debugging plan
    ├── zeroflte_boot_success_report.md # Initial boot verification report
    └── zeroflte_performance_and_bugfix_report.md # Performance and stability analysis
```

---

## 🛠️ How to Deploy Fixes Manually via ADB

If testing on a live device connected via ADB root:

```bash
# 1. Remount vendor and system as read-write
adb root
adb remount

# 2. Push post-boot script and give execution permissions
adb push configs/init.postboot.sh /vendor/bin/init.postboot.sh
adb shell chmod 755 /vendor/bin/init.postboot.sh
adb shell chown root:shell /vendor/bin/init.postboot.sh

# 3. Push tuned powerhint.json
adb push configs/powerhint.json /vendor/etc/powerhint.json

# 4. Trigger postboot script immediately
adb shell sh /vendor/bin/init.postboot.sh

# 5. Verify ZRAM and Governor states
adb shell "swapon -s"
adb shell "cat /sys/devices/14ac0000.mali/scaling_min_freq"
adb shell "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"
```

---

## 👥 Credits & Upstream Acknowledgments

- Upstream Device Trees & Kernel Port: [farovitus](https://github.com/farovitus) & LineageOS Exynos 7420 contributors.
- Android 15 LineageOS 22.2 Maintainers.
- Debugging, stability fixes, ZRAM & performance optimization by **eiarf**.
