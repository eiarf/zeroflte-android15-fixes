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
