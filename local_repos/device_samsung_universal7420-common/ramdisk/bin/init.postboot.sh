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

# 3. VM tuning for 3GB RAM + ZRAM responsiveness
echo 100 > /proc/sys/vm/swappiness
echo 0 > /proc/sys/vm/page-cluster
echo 100 > /proc/sys/vm/vfs_cache_pressure

# 4. GPU Devfreq floor (350MHz for smooth UI without overheating)
if [ -f /sys/class/devfreq/14ac0000.gpu/min_freq ]; then
    echo 350000000 > /sys/class/devfreq/14ac0000.gpu/min_freq
fi

# 5. CPU Governor & Scaling Floors (6.6 schedutil OPPs)
for pol in /sys/devices/system/cpu/cpufreq/policy*; do
    if [ -f "$pol/scaling_governor" ]; then
        echo schedutil > "$pol/scaling_governor" 2>/dev/null || echo performance > "$pol/scaling_governor" 2>/dev/null
    fi
done

# Policy0 (Cortex-A53 LITTLE): Idle at 400MHz, max 1.5GHz
if [ -f /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq ]; then
    echo 400000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
fi
if [ -f /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq ]; then
    echo 1500000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq
fi

# Policy1 (Cortex-A57 big): Idle at 800MHz, capped at 1.5GHz to prevent 90°C thermal runaway
if [ -f /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq ]; then
    echo 800000 > /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq
fi
if [ -f /sys/devices/system/cpu/cpufreq/policy1/scaling_max_freq ]; then
    echo 1500000 > /sys/devices/system/cpu/cpufreq/policy1/scaling_max_freq
fi
