#!/vendor/bin/sh
# Post-boot optimizations for Samsung Galaxy S6 (zeroflte) Android 15
sleep 1

# 1. Disarm kernel autorecovery watchdog now that boot is complete
if [ -f /sys/module/dsh_esslog/parameters/autorecovery ]; then
    echo 0 > /sys/module/dsh_esslog/parameters/autorecovery
fi

# 2. Setup ZRAM Swap (2 GB) if not already active
if ! grep -q zram /proc/swaps; then
    insmod /vendor/lib/modules/zsmalloc.ko 2>/dev/null
    insmod /vendor/lib/modules/zram.ko num_devices=1 2>/dev/null
    if [ -f /sys/block/zram0/comp_algorithm ]; then
        echo lzo-rle > /sys/block/zram0/comp_algorithm
    fi
    echo 2147483648 > /sys/block/zram0/disksize
    mkswap /dev/block/zram0
    swapon /dev/block/zram0
fi

# VM tuning for zram & responsiveness
echo 100 > /proc/sys/vm/swappiness
echo 0 > /proc/sys/vm/page-cluster
echo 60 > /proc/sys/vm/vfs_cache_pressure

# 3. GPU Devfreq Tuning (Boost min clock from 266MHz to 420MHz for 1440x2560 WQXGA)
if [ -f /sys/class/devfreq/14ac0000.gpu/min_freq ]; then
    echo 420000000 > /sys/class/devfreq/14ac0000.gpu/min_freq
fi

# 4. CPU Scaling Frequency Floors (Reduce UI latency and ramp-up stutter)
# Little cluster (Cortex-A53: cpu0, cpu5-7) -> 700 MHz floor
if [ -f /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq ]; then
    echo 700000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
fi
# Big cluster (Cortex-A57: cpu1-4) -> 1000 MHz floor
if [ -f /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq ]; then
    echo 1000000 > /sys/devices/system/cpu/cpufreq/policy1/scaling_min_freq
fi

# 5. UI Animations & Responsiveness
settings put global window_animation_scale 0.75
settings put global transition_animation_scale 0.75
settings put global animator_duration_scale 0.75

# Disable lockscreen so phone boots directly to desktop
settings put secure lockscreen.disabled 1

# Disable lockscreen sound and haptic to prevent AudioService/Vibrator hangs
settings put system lockscreen_sounds_enabled 0
settings put system sound_effects_enabled 0

# Set comfortable screen timeout (5 minutes)
settings put system screen_off_timeout 300000

# Ensure touchscreen is uninhibited
for dev in /sys/devices/platform/soc@0/14e10000.i2c/i2c-*/*-0049/input/input*; do
    if [ -f "$dev/inhibited" ]; then
        echo 0 > "$dev/inhibited"
    fi
done
