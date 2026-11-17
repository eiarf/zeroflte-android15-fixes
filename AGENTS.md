# AGENTS.md — Samsung Galaxy S6 (`zeroflte`) Android 15 & Kernel 6.6 Project Guide

> **Note for AI Agents**: Read this document thoroughly before proposing, generating, modifying, or applying any code, configuration, or kernel patch for this device. It contains critical operational rules, hardware quirks, and behavioral invariants that must never be violated.

---

## 1. Project & Hardware Specifications

| Component | Specification | Operational Implication for Agent |
| :--- | :--- | :--- |
| **Device Model** | Samsung Galaxy S6 (`zeroflte` / SM-G920F / SM-G920I / SM-G920*) | Primary target. Common board is `universal7420`. |
| **SoC** | Samsung Exynos 7420 (14nm FinFET) | big.LITTLE architecture (4x A57 Big + 4x A53 Little). |
| **CPU Clusters** | Little: `cpu0-cpu3` (max 1.5 GHz), Big: `cpu4-cpu7` (max 2.1 GHz) | Governors must maintain responsive floor frequencies. |
| **GPU** | ARM Mali-T760 MP8 | Governed via `/sys/devices/14ac0000.mali`. Min floor: 420 MHz. |
| **RAM** | 3 GB LPDDR4 PoP | **High Memory Pressure** on Android 15. Requires 2GB ZRAM. |
| **Storage** | UFS 2.0 (High speed) | Block path: `/dev/block/platform/15570000.ufs/by-name/` |
| **Display** | 5.1" QHD Super AMOLED (1440 × 2560) | Driven via DRM/KMS (`/dev/dri/card0`), HWComposer 2.x. |
| **Operating System** | LineageOS 22.2 (Android 15 / vanilla AOSP base) | Target SDK 35. High memory overhead. |
| **Kernel** | Mainline Linux Kernel 6.6 (`kernel/samsung/universal7420`) | Upstream driver models (V4L2, DRM, modern governors). |
| **Recovery** | TWRP 3.7+ | Used for recovery logs (`/sys/fs/pstore/console-ramoops`). |
| **User Physical Note** | **Hardware buttons are stiff/damaged** | **DO NOT disable watchdog during early boot** (see Rule #1). |

---

## 2. GitHub Repositories Under `@eiarf`

Always reference and branch from these forked repositories:

* **Companion Fixes & Tools (This Repo)**:
  `https://github.com/eiarf/zeroflte-android15-fixes` (Branch: `main`)
* **Common Device Tree**:
  `https://github.com/eiarf/android_device_samsung_universal7420-common` (Branch: `lineage-22.2-KRNL6.6`)
* **Device Tree (zeroflte)**:
  `https://github.com/eiarf/android_device_samsung_zeroflte` (Branch: `lineage-22.2-KRNL6.6`)
* **Proprietary Vendor Blobs**:
  `https://github.com/eiarf/proprietary_vendor_samsung` (Branch: `lineage-22.2-KRNL6.6`)

---

## 3. Current ROM Feature Matrix

| Feature | Status | Notes / Root Cause / Solution |
| :--- | :---: | :--- |
| **Boot to Launcher** | 🟢 **WORKING** | Clean boot into LineageOS 22.2 launcher and UI. |
| **Display / DRM / KMS** | 🟢 **WORKING** | DRM dumb buffer mmap & page-flipping functional at QHD. |
| **Touchscreen** | 🟢 **WORKING** | Inhibit state decoupled from early suspend; responsive on unlock. |
| **Memory / ZRAM** | 🟢 **WORKING** | `zram.ko` + `zsmalloc.ko` active, 2048 MB swap disk, ~700MB free RAM. |
| **Stability / Watchdog** | 🟢 **WORKING** | Autorecovery disarmed on `sys.boot_completed=1` (`autorecovery = 0`). |
| **UI Responsiveness** | 🟢 **WORKING** | Top-app uclamp boost 15, GPU min 420MHz, Little min 600MHz, Big min 1000MHz. |
| **ADB Root & Debugging** | 🟢 **WORKING** | Direct `adb root`, `adb remount` functional in development builds. |
| **Camera** | 🔴 **NOT WORKING** | Exynos FIMC-IS ISP needs modern V4L2/libcamera AIDL HAL provider. |
| **Sensors / Audio / Wi-Fi** | 🟡 **PARTIAL** | Upstream Kernel 6.6 driver staging. |

---

## 4. The 5 Golden Invariants for AI Agents (Strict Rules)

### ⚠️ Rule 1: NEVER Disable Kernel Watchdog Autorecovery During Early Boot
* **The Context**: The device's physical buttons (Power + Vol Down + Home) are worn out and difficult to press.
* **The Failure Mode**: If an agent disables the watchdog / `autorecovery` during early boot, and a kernel deadlock or panic occurs before ADB starts, the phone will remain stuck on a black/blank screen. The user cannot easily force it into TWRP via hardware buttons!
* **Correct Procedure**: Keep `dsh_esslog` autorecovery active during early boot. ONLY disarm it upon `sys.boot_completed=1`:
  ```bash
  on property:sys.boot_completed=1
      write /sys/module/dsh_esslog/parameters/autorecovery 0
  ```

### ⚠️ Rule 2: NEVER Couple Touchscreen Inhibit to Early Suspend
* **The Failure Mode**: If touchscreen inhibit (`/sys/class/input/inputX/inhibit` or similar) is toggled on display blank/sleep without a reliable resume hook, unlocking the device leaves the touch digitizer permanently frozen, creating an apparent system hang.
* **Correct Procedure**: Touchscreen listeners must remain decoupled from display state transitions as configured in `ramdisk/etc/init.power.rc`.

### ⚠️ Rule 3: NEVER Allow PowerHAL JSON Syntax Errors or Idle Underclocking
* **The Failure Mode**: `powerhint.json` must be strictly valid JSON. If PowerHAL crashes or is missing:
  - Exynos 7420 defaults to energy-saving floors: Mali GPU drops to 260 MHz, Little cluster drops to 400 MHz.
  - Result: Severe 10-15 FPS stutters, slow app launch, and input latency.
* **Correct Procedure**: Maintain the tuned floors and governors:
  - Governors: `schedutil` on all CPU policies (`policy0` & `policy1`).
  - GPU: `420000000` (420 MHz) minimum floor in `vendor.postboot`.
  - Little Cluster (`policy0`): `700000` (700 MHz) floor, `1500000` (1.5 GHz) max.
  - Big Cluster (`policy1`): `1000000` (1.0 GHz) floor, `2100000` (2.1 GHz) max.
  - UCLAMP top-app boost: `15.00` on touch / interaction.

### ⚠️ Rule 4: ALWAYS Enforce 2GB ZRAM Swap
* **The Context**: Exynos 7420 has 3GB physical RAM. Android 15 core services (system_server, surfaceflinger, launcher, media) consume ~2.2GB alone.
* **The Failure Mode**: Without ZRAM, available RAM drops to <350MB, causing aggressive LMKD thrashing and app closing.
* **Correct Procedure**: Ensure `/vendor/bin/init.postboot.sh` insmods `zsmalloc.ko` + `zram.ko`, sets `disksize` to `2147483648` (2048 MB), runs `mkswap /dev/block/zram0`, and executes `swapon /dev/block/zram0`.

### ⚠️ Rule 5: Camera HAL Requires Modern AIDL Bridge, NOT Legacy Blobs
* **The Context**: The proprietary Samsung Camera HAL (`camera.samsungexynos7420.so`) is a legacy 32/64-bit HAL1/HAL3 blob that communicates directly with Samsung's out-of-tree FIMC-IS driver ioctls.
* **The Failure Mode**: Mainline Kernel 6.6 uses modern Linux V4L2 and Media Controller subsystems. Simply copying proprietary camera blobs from stock Nougat/Oreo ROMs will crash `cameraserver` immediately.
* **Correct Procedure**: Camera support must be implemented either through `libcamera` with a custom Exynos 7420 pipeline handler or an open-source V4L2 AIDL camera provider wrapper.

### ⚠️ Rule 6: Display Resolution & Density Scaling (FHD 1080p vs Native QHD 1440p)
* **The Context**: Native Galaxy S6 display is 1440x2560 (3.7M pixels). The Mali-T760 MP8 struggles on Android 15 with software DRM/Mesa composition pushing this pixel load.
* **Correct Procedure**: Use display downscaling to FHD (1080x1920 @ 420 dpi) which cuts GPU render workload by ~45%, delivering buttery smooth 60 FPS transitions.
* **Control Script**: `/vendor/bin/set_resolution.sh [fhd|native|hd]` (controls `persist.vendor.display.resolution`). Default on boot is `fhd` (1080x1920 @ 420dpi). Switch to native QHD at any time via `set_resolution.sh native`.

---

## 5. Diagnostic & Quick Runbook for AI Agents

When interacting with the connected device via ADB, use these verified commands:

### A. Health & Uptime Check
```powershell
adb shell "uptime; free -m; getprop sys.boot_completed"
```
*Expected: `sys.boot_completed` returns `1`, swap total ~2047MB, free memory >600MB.*

### B. Verify Governor & Frequency Floors
```powershell
# Check GPU minimum frequency (expect 420000000 or similar governed floor)
adb shell "cat /sys/devices/14ac0000.mali/scaling_min_freq"

# Check CPU cluster floors
adb shell "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"  # Little: 600000
adb shell "cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq"  # Big: 1000000
```

### C. Verify ZRAM Swap Status
```powershell
adb shell "cat /proc/swaps"
# Expected output: /dev/block/zram0 partition 2097148 ...
```

### D. Pulling Crash / Panic Logs
If the device reboots into TWRP:
```powershell
# Read console ramoops from last crash
adb shell "cat /sys/fs/pstore/console-ramoops" > last_panic.txt
# Read TWRP recovery log
adb pull /tmp/recovery.log recovery.log
```
If the device booted into Android:
```powershell
adb shell "cat /proc/last_kmsg" > last_kmsg.txt
```

### E. Applying Configuration Hotfix Live (Without Rebuilding Full ROM)
```powershell
adb root
adb remount
# Push postboot script and fix permissions
adb push configs/init.postboot.sh /vendor/bin/init.postboot.sh
adb shell chmod 755 /vendor/bin/init.postboot.sh
adb shell chown root:shell /vendor/bin/init.postboot.sh

# Push powerhint
adb push configs/powerhint.json /vendor/etc/powerhint.json

# Execute postboot script
adb shell sh /vendor/bin/init.postboot.sh
```

---

## 6. Directory Map in this Repository

- `configs/`: Production-ready configurations (`init.samsungexynos7420.rc`, `init.power.rc`, `init.postboot.sh`, `powerhint.json`, `vendor_build.prop`).
- `hwc-gralloc/`: Hardware Composer and DRM gralloc reference source codes (`s6_kms_hwcomposer.cpp`, `s6_drm_gralloc.cpp`) and dumb buffer test programs.
- `scripts/`: Diagnostic, patching, and verification Python scripts.
- `logs/`: TWRP recovery logs, ramoops, and kernel crash diagnostics captured during boot debugging.
- `reports/`: In-depth engineering reports on boot recovery and performance optimization.
