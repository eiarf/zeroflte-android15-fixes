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
| **Boot to Launcher** | 🟢 **WORKING** | Clean boot (~34s) into Android 15 launcher and UI. |
| **Display / DRM / KMS** | 🟢 **WORKING** | Zero-copy DRM PRIME scanout (`s6_kms_hwcomposer.cpp`) + YV12/NV12 DMA-BUF offset handling (`s6_drm_gralloc.cpp`) for HEVC/AVC video playback; ~0.4% HWC CPU. |
| **Touchscreen** | 🟢 **WORKING** | Inhibit state decoupled from early suspend; responsive on unlock. |
| **Memory / ZRAM** | 🟢 **WORKING** | `zram.ko` + `zsmalloc.ko` active, 2048 MB swap disk, ~700MB free RAM. |
| **Stability / Watchdog** | 🟢 **WORKING** | Autorecovery disarmed on `sys.boot_completed=1` (`autorecovery = 0`). |
| **UI Responsiveness** | 🟢 **WORKING** | 60 FPS UI (10-15 ms median frame time), ADPF `SCHED_QOS_*` in `task_profiles.json`, FHD 1080x1920 @ 420 dpi, GPU min 420MHz. |
| **Audio** | 🟢 **WORKING** | `madera` / `cs47l85` / `max98925` / `zero-wm1840` modules modprobed in `early-init`; ALSA card 0 (`pcmC0D0p`) & Speaker active. |
| **Wi-Fi & Bluetooth** | 🟢 **WORKING** | `BCM4358A3` (`brcmfmac4358-pcie.*` & `BCM4358A3.hcd`) staged in `/vendor/firmware/brcm/`; 2.4GHz/5GHz Wi-Fi & BT active. |
| **ADB Debugging** | 🟢 **WORKING** | ADB functional; `/vendor` modification via TWRP (`/system_root/system/vendor`). |
| **Camera** | 🔴 **NOT WORKING** | Exynos FIMC-IS ISP needs modern V4L2/libcamera AIDL HAL provider (`NK_HAS_CAMERA=false`). |

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
  - Big Cluster (`policy1`): `1000000` (1.0 GHz) floor, `1800000` (1.8 GHz) max (Linux 6.6 `scaling_available_frequencies` max).
  - UCLAMP top-app boost: `15.00` on touch / interaction; `configs/task_profiles.json` defines `SCHED_QOS_SENSITIVE_STANDARD_{SET,CLEAR}` & `EXTREME` for PowerHAL ADPF.

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
* **Note on Treble Namespace**: Because `vendor.postboot` executes in a vendor SELinux domain (`init_touch_sh`), Java CLI commands (`wm`/`settings`) cannot cross Treble's `/vendor` <-> `/system` linker namespace. After a fresh `/data` wipe, apply once via `adb shell sh /vendor/bin/set_resolution.sh fhd` (persists across normal reboots).

### ⚠️ Rule 7: Zero-Copy HWC, `DRM_FORMAT_XBGR8888` Scanout & Gralloc YUV Semantics
* **The Failure Mode**:
  1. Copying 1440x2560x4 (14.7 MB/frame) via CPU `memcpy` in `s6_hwc_set()` chokes CPU (~35% load, ~150ms/frame latency -> 5 FPS UI).
  2. Calling legacy `drmModeAddFB(..., 24, 32, ...)` registers Exynos 7420 DECON with `DRM_FORMAT_XRGB8888` (`[B,G,R,X]`), while Android `RGBA_8888` / `RGBX_8888` is `[R,G,B,A/X]` — swapping Red & Blue across the entire screen and making videos/YouTube look color-inverted.
  3. Mesa `u_gralloc_fallback.c` calls `gralloc->lock_ycbcr(..., usage=0, l=0, t=0, w=0, h=0, &ycbcr)` expecting **0-based byte offsets** from NULL and full buffer height (and expects `-EINVAL` for non-YUV formats like `IMPLEMENTATION_DEFINED`). Returning mapped virtual pointers or `ystride * h` (0) crashes `surfaceflinger` (`RenderEngine`: `Failed to create a valid texture ... format:842094169` / `YV12`) on video playback.
* **Correct Procedure**:
  - `s6_kms_hwcomposer.cpp` maps `RGBA_8888`/`RGBX_8888` -> `DRM_FORMAT_XBGR8888` (24BPP_xBGR on DECON Window 0) and `BGRA_8888` -> `DRM_FORMAT_XRGB8888` using `drmModeAddFB2`, imports `private_handle_t->fd` via `drmPrimeFDToHandle` (16-slot `gem_handle` LRU cache keyed with format), and waits on `acquireFenceFd`.
  - `s6_drm_gralloc.cpp` rejects non-YUV formats in `lock_ycbcr()`, returns byte offsets from `0` when `SW_READ/WRITE` is `0`, uses `hnd->height`, aligns YUV `alloc_width` to 32 bytes, `alloc_height` to 16-line macroblock padding, and 16-byte aligns `YV12` `cstride`.

### ⚠️ Rule 8: Kernel Module Loading & `BCM4358A3` Firmware Staging
* **The Context**: Clean ROM builds without `vendor/nk` do not ship `/vendor/etc/init/nk-modules.rc` or `/vendor/firmware/brcm/`, leaving Audio (`zero-wm1840`), Bluetooth (`hci0`), and Wi-Fi (`brcmfmac`) inactive.
* **Correct Procedure**:
  - `configs/init.samsungexynos7420.rc` (`on early-init`) runs `vendor_modprobe` on audio (`arizona-micsupp`, `madera*`, `snd-soc-cs47l85`, `snd-soc-i2s`, `snd-soc-max98925`, `snd-soc-zero-wm1840`) and BT (`bluetooth`, `btbcm`, `hci_uart`, `rfcomm`, `hidp`).
  - `/vendor/firmware/brcm/` must contain `brcmfmac4358-pcie.{samsung,zeroflte.,}{bin,txt}` (from `/vendor/etc/wifi/bcmdhd_sta.bin` & `nvram_net.txt`) and `BCM4358A3.{samsung,zeroflte.,}hcd` (from `/vendor/firmware/bcm4358A3_V0083.0138.hcd`) labeled `u:object_r:vendor_firmware_file:s0` (automated in `scripts/apply_brcm_firmware.sh`).

### ⚠️ Rule 9: Wi-Fi HAL Compatibility Overlays & MAX77843 MUIC Wakeup Sepolicy
* **The Failure Mode**:
  1. AOSP 15 Wi-Fi framework repeatedly invokes `setBtCoexistenceScanModeEnabled`, `setSuspendModeEnabled`, and roaming mode on `ISupplicantStaIface`/`IWifiStaIface`. The BCM4358 `bcmdhd`/`brcmfmac` driver and legacy supplicant reject these calls (`ServiceSpecificException` code 1 / 4), causing 800+ error logs per session.
  2. The MAX77843 micro-USB MUIC wakeup node (`/devices/platform/i2c-max77843/i2c-3/3-0066/max77843-muic/wakeup/wakeup1`) defaults to generic `sysfs`, which blocks `system_suspend` daemon (`Permission denied` on `wakeup1`).
* **Correct Procedure**:
  - In `rro_overlays/WifiOverlay/res/values/config.xml`: explicitly set `config_wifiSuspendOptimizationsEnabled=false`, `config_wifiUseHalApiToDisableFwRoaming=false`, `config_wifiDisableFirmwareRoamingInIdleMode=true`, `config_wifiDefaultCoexAlgorithmEnabled=false`, and `config_wifi_connected_mac_randomization_supported=false`. At runtime, set `settings put global wifi_suspend_optimizations_enabled 0`.
  - In `sepolicy/vendor/genfs_contexts`: add `genfscon sysfs /devices/platform/i2c-max77843/i2c-3/3-0066/max77843-muic/wakeup u:object_r:sysfs_wakeup:s0`.

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

- `configs/`: Production-ready configurations (`init.samsungexynos7420.rc`, `init.power.rc`, `init.postboot.sh`, `powerhint.json`, `task_profiles.json`, `permissive_rules.cil`, `vendor_build.prop`).
- `hwc-gralloc/`: Hardware Composer (`s6_kms_hwcomposer.cpp`, `hwcomposer.samsungexynos7420.so`) and DRM gralloc (`s6_drm_gralloc.cpp`, `gralloc.samsungexynos7420.so`) sources and prebuilt binaries.
- `mesa-panfrost/`: Compiled Mesa 24.0.9 Panfrost & DRI runtime libraries (`libEGL.so`, `libGLES*.so`, `panfrost_dri.so`, etc.).
- `scripts/`: Diagnostic, patching, and verification scripts (`apply_brcm_firmware.sh`, etc.).
- `logs/`: TWRP recovery logs, ramoops, and kernel crash diagnostics captured during boot debugging.
- `reports/`: In-depth engineering reports on boot recovery and performance optimization.
