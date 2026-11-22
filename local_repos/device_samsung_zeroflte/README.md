Device configuration for Samsung Galaxy S6 (SM-G920F, SM-G920I, SM-G920FD).
========================================

Basic   | Specification List
-------:|:-------------------------
CPU     | 4x2.1 GHz Cortex-A57 & 4x1.5 GHz Cortex-A53)
Chipset | Samsung Exynos 7420
GPU     | Mali-T760 MP8
Memory  | 3 GB
Shipped Android Version | 5.0.2
Storage | 32/64/128 GB
MicroSD | n/A
Battery | Li-Ion 2600 mAh
Dimensions | 143.4 mm x 70.5 mm x 6.8 mm
Display | 2560 x 1440 pixel, 5.1"
Rear Camera  | 16 MP, f/1.9, 28mm (wide), 1/2.6", 1.12µm, AF, OIS
Front Camera | 5 MP, f/1.9, 22mm (wide), 1/4.1", 1.34µm
Release Date | April 2015

![Galaxy S6](https://fdn2.gsmarena.com/vv/pics/samsung/samsung-galaxy-s6-3.jpg "Galaxy S6")

## Android 15 (Kernel 6.6) Status & Fixes

> **Note:** The Android 15 / Linux 6.6 bring-up modifications in this `@eiarf` fork were **developed, ported, and debugged by an LLM (Large Language Model)**, verified on `SM-G920F` (`zeroflte`).

### Working
- Boot to Desktop & System UI (Project Infinity-X 2.9 / Android 15, SELinux Enforcing)
- Display / DRM / KMS (`s6_kms_hwcomposer`, `s6_drm_gralloc`, Mesa 24.0.9 Panfrost GPU)
- Touchscreen & Capacitive Touchkeys (with screen-off inhibition decoupling)
- 2 GB ZRAM Swap with kernel module auto-load on boot completion
- Kernel Watchdog (`dsh_esslog`) with automatic disarm upon successful boot
- Tuned CPU (`schedutil`) & GPU (`420 MHz` min floor) DVFS, ADPF `task_profiles.json`
- Wi-Fi (`BCM4358A3` + `WifiOverlay` BCM4358 overrides), Bluetooth, Audio (`zero-wm1840`), ADB
- **Camera** (`ICameraProvider/internal/0` AIDL V3 V4L2/SoftISP HAL + rear 16MP `s5c73c2`/`s5k2p2`, front 5MP `s5k4e6`, `ak7345` AF, `idg2030` OIS, `s2mpb02` Flash/Torch, 1080p video)
- **GNSS (`BCM4773`), NFC (`S3FWRN5P`), IR Blaster (`irled_remote`), & Heart Rate (`MAX86900`)**

Companion fixes & flashable release ZIP: https://github.com/eiarf/zeroflte-android15-fixes
