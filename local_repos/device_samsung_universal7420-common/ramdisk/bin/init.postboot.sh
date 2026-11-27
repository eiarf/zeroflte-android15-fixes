#!/vendor/bin/sh
# Post-boot optimizations for Samsung Galaxy S6 (zeroflte) RisingOS Android 17
sleep 1

# 1. Disarm kernel autorecovery watchdog now that boot is complete
if [ -f /sys/module/dsh_esslog/parameters/autorecovery ]; then
    echo 0 > /sys/module/dsh_esslog/parameters/autorecovery
fi

# 2. Audio LPASS stability: prevent PL330 DMA race panic during audio standby/stop
if [ -f /sys/bus/platform/devices/11400000.audio-subsystem/power/control ]; then
    echo on > /sys/bus/platform/devices/11400000.audio-subsystem/power/control
fi
for dmac in /sys/bus/amba/devices/*.dma-controller/power/control; do
    [ -f "$dmac" ] && echo on > "$dmac"
done

# 3. Networking: BBR + FQ + ECN
echo fq > /proc/sys/net/core/default_qdisc 2>/dev/null || true
echo bbr > /proc/sys/net/ipv4/tcp_congestion_control 2>/dev/null || true
echo 1 > /proc/sys/net/ipv4/tcp_ecn 2>/dev/null || true

# 4. VM tuning for 3GB RAM + ZRAM responsiveness
echo 100 > /proc/sys/vm/swappiness
echo 0 > /proc/sys/vm/page-cluster
echo 80 > /proc/sys/vm/vfs_cache_pressure
if [ -f /proc/sys/vm/watermark_boost_factor ]; then
    echo 0 > /proc/sys/vm/watermark_boost_factor
fi

# 5. GPU Devfreq floor (266MHz idle to avoid heat) & Display bus floor (160MHz at same 650mV)
if [ -f /sys/class/devfreq/14ac0000.gpu/min_freq ]; then
    echo 266000000 > /sys/class/devfreq/14ac0000.gpu/min_freq
fi
if [ -f /sys/class/devfreq/bus-disp/min_freq ]; then
    echo 160000000 > /sys/class/devfreq/bus-disp/min_freq
fi

# 6. CPU Governor & Scaling Floors (6.6 schedutil OPPs)
for pol in /sys/devices/system/cpu/cpufreq/policy*; do
    if [ -f "$pol/scaling_governor" ]; then
        echo schedutil > "$pol/scaling_governor" 2>/dev/null || echo performance > "$pol/scaling_governor" 2>/dev/null
    fi
    if [ -f "$pol/schedutil/rate_limit_us" ]; then
        echo 10000 > "$pol/schedutil/rate_limit_us" 2>/dev/null || true
    fi
done
if [ -f /sys/devices/system/cpu/cpufreq/schedutil/rate_limit_us ]; then
    echo 10000 > /sys/devices/system/cpu/cpufreq/schedutil/rate_limit_us 2>/dev/null || true
fi

# Policy0 (Cortex-A53 LITTLE): Idle at 400MHz, max 1.5GHz
if [ -f /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq ]; then
    echo 400000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
fi
if [ -f /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq ]; then
    echo 1500000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq
fi

# Policy1 (Cortex-A57 big): Idle at 800MHz, burst up to 1.6GHz (ASV0 968.75mV)
if [ -f /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq ]; then
    echo 800000 > /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq
fi
if [ -f /sys/devices/system/cpu/cpufreq/policy1/scaling_max_freq ]; then
    echo 1600000 > /sys/devices/system/cpu/cpufreq/policy1/scaling_max_freq
fi

# Keep Exynos7420 s2idle deep sleep active
echo deep > /sys/power/mem_sleep 2>/dev/null || true

# 7. Deep-sleep & idle power: runtime PM auto for PCIe root/Wi-Fi, camera, and power domains
#    (Keep 11400000.audio-subsystem / PL330 DMACs on for audio stability)
for dev in \
    /sys/bus/pci/devices/* \
    /sys/bus/i2c/devices/12-000c \
    /sys/bus/platform/devices/14000000.csis \
    /sys/bus/platform/devices/14010000.csis \
    /sys/bus/platform/devices/14020000.fimc-lite \
    /sys/bus/platform/devices/14030000.fimc-lite \
    /sys/bus/platform/devices/105c4040.power-domain \
    /sys/bus/platform/devices/105c4140.power-domain \
    /sys/bus/platform/devices/105c4180.power-domain; do
    if [ -w "$dev/power/control" ]; then
        echo auto > "$dev/power/control" 2>/dev/null || true
    fi
done

# 8. Idle power & Doze tuning built into ROM (eliminates external Magisk workaround):
#    disable background BLE/Wi-Fi always-scan, skip Doze motion polling (spi1.0 IRQs),
#    enable cached app freezer, and restrict alarm-waking partnersetup helper.
(
    w=0
    while [ "$w" -lt 25 ]; do
        if /system/bin/service check settings 2>/dev/null | grep -q "found"; then
            break
        fi
        sleep 1
        w=$((w + 1))
    done
    sleep 2
    /system/bin/settings put global wifi_scan_always_enabled 0 2>/dev/null || true
    /system/bin/settings put global ble_scan_always_enabled 0 2>/dev/null || true
    /system/bin/settings put global device_idle_constants \
        "light_after_inactive_to=60000,inactive_to=15000,sensing_to=0,locating_to=0,motion_inactive_to=0,idle_after_inactive_to=15000,idle_pending_to=30000,max_idle_pending_to=60000,quick_doze_delay_to=15000" \
        2>/dev/null || true
    /system/bin/device_config put activity_manager_native_boot use_freezer true 2>/dev/null || true
    /system/bin/am set-standby-bucket com.google.android.partnersetup restricted 2>/dev/null || true
) >/dev/null 2>&1 &

