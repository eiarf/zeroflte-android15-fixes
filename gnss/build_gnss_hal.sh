#!/bin/bash
set -euo pipefail
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

AOSP=/home/angger/aosp_cam
LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64
CC="$LLVM/bin/aarch64-linux-android35-clang"
CXX="$LLVM/bin/aarch64-linux-android35-clang++"
STRIP="$LLVM/bin/llvm-strip"

GNSS_DIR="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/zeroflte-android15-fixes/gnss"
OUT_DIR="$AOSP/out_gnss"
mkdir -p "$OUT_DIR"

# 1. Build 64-bit libsecril-client.so
"$CC" -O2 -fPIC -shared -Wall -Wextra -Werror \
  "$GNSS_DIR/libsecril_client.c" \
  -Wl,-soname,libsecril-client.so \
  -o "$OUT_DIR/libsecril-client.so"
"$STRIP" --strip-unneeded "$OUT_DIR/libsecril-client.so"
cp "$OUT_DIR/libsecril-client.so" "$GNSS_DIR/libsecril-client.so"

# 2. Build AIDL GNSS HAL service
INCS=(
  "-I$AOSP/shim_inc"
  "-I$AOSP/gen_gnss_ndk/include"
  "-I$AOSP/native/libs/binder/ndk/include_cpp"
  "-I$AOSP/native/libs/binder/ndk/include_ndk"
  "-I$AOSP/native/libs/binder/ndk/include_platform"
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/libsystem/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libhardware/include_all"
  "-I$AOSP/libbase/include"
)

"$CXX" -std=c++20 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
  -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
  "${INCS[@]}" \
  "$GNSS_DIR/gnss_service.cpp" \
  "$AOSP/obj_gnss_vintf/libgnss_aidl.a" \
  "$AOSP/device_libs/libbinder_ndk.so" \
  -static-libstdc++ -llog -ldl -lm -pthread \
  -Wl,-rpath-link,"$AOSP/device_libs" \
  -o "$OUT_DIR/android.hardware.gnss-service.universal7420"

"$STRIP" --strip-unneeded "$OUT_DIR/android.hardware.gnss-service.universal7420"
cp "$OUT_DIR/android.hardware.gnss-service.universal7420" "$GNSS_DIR/android.hardware.gnss-service.universal7420"

"$LLVM/bin/llvm-readelf" -d "$OUT_DIR/android.hardware.gnss-service.universal7420" | grep NEEDED
ls -lh "$GNSS_DIR/libsecril-client.so" "$GNSS_DIR/android.hardware.gnss-service.universal7420"
