#!/bin/bash
set -euo pipefail
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

AOSP=/home/angger/aosp_cam
LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64
CXX="$LLVM/bin/aarch64-linux-android35-clang++"
STRIP="$LLVM/bin/llvm-strip"
NDK_CXX="$LLVM/sysroot/usr/include/c++/v1"
SENSORS_DIR="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/zeroflte-android15-fixes/sensors"
LIBS="$AOSP/device_libs_sensors"
OUT="$AOSP/out_sensors"
mkdir -p "$OUT"

# Check ABI namespace override for device libc++.so (std::__1)
mkdir -p "$AOSP/shim_sensors_cxx"
sed 's/#define _LIBCPP_ABI_NAMESPACE __ndk1/#define _LIBCPP_ABI_NAMESPACE __1/' \
  "$NDK_CXX/__config_site" > "$AOSP/shim_sensors_cxx/__config_site"

INCS=(
  "-isystem" "$AOSP/shim_sensors_cxx"
  "-isystem" "$NDK_CXX"
  "-I$AOSP/shim_sensors"
  "-I$AOSP/interfaces/sensors/common/default/2.X/multihal/include"
  "-I$AOSP/libhidl/base/include"
  "-I$AOSP/libfmq/base"
  "-I$AOSP/core/libutils/binder/include"
  "-I$AOSP/core/libutils/include"
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libbase/include"
)

"$CXX" -std=c++20 -O2 -fPIC -shared \
  -fno-rtti -fno-exceptions \
  -Wall -Wextra -Werror -Wno-unused-parameter -Wno-overloaded-virtual \
  -D__ANDROID_VNDK__ \
  "${INCS[@]}" \
  "$SENSORS_DIR/max86900_subhal.cpp" \
  -nostdlib++ \
  "$LIBS/android.hardware.sensors@1.0.so" \
  "$LIBS/android.hardware.sensors@2.0.so" \
  "$LIBS/android.hardware.sensors@2.0-ScopedWakelock.so" \
  "$LIBS/android.hardware.sensors@2.1.so" \
  "$LIBS/libbase.so" \
  "$LIBS/libhidlbase.so" \
  "$LIBS/libutils.so" \
  "$LIBS/libc++.so" \
  -llog \
  -Wl,-soname,sensors.max86900.so \
  -Wl,-z,defs \
  -pthread \
  -o "$OUT/sensors.max86900.so"

"$STRIP" --strip-unneeded "$OUT/sensors.max86900.so"
cp "$OUT/sensors.max86900.so" "$SENSORS_DIR/sensors.max86900.so"

echo "=== Readelf NEEDED ==="
"$LLVM/bin/llvm-readelf" -d "$SENSORS_DIR/sensors.max86900.so" | grep -E "NEEDED|SONAME"
echo "=== Exported sensorsHalGetSubHal_2_1 ==="
"$LLVM/bin/llvm-nm" -D --defined-only "$SENSORS_DIR/sensors.max86900.so" | grep -E "sensorsHal|Max86900|_ZT"
ls -lh "$SENSORS_DIR/sensors.max86900.so"
