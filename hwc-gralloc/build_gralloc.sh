#!/bin/bash
set -euo pipefail

LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin
AOSP=/home/angger/aosp_cam
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/s6_drm_gralloc.cpp"

INCS=(
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/libsystem/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libhardware/include_all"
  "-idirafter" "/home/angger/SM_G92X/include/uapi"
)

CXXFLAGS=(
  -O2 -fPIC -shared
  -Wall -Wextra -Werror
  -Wno-unused-parameter -Wno-missing-field-initializers
  -static-libstdc++
)

"$LLVM/armv7a-linux-androideabi35-clang++" "${CXXFLAGS[@]}" "${INCS[@]}" \
  -Wl,-soname,gralloc.default.so "$SRC" -llog -ldl -lm \
  -o "$DIR/gralloc.default.32.so"
"$LLVM/llvm-strip" --strip-unneeded "$DIR/gralloc.default.32.so"

"$LLVM/aarch64-linux-android35-clang++" "${CXXFLAGS[@]}" "${INCS[@]}" \
  -Wl,-soname,gralloc.samsungexynos7420.so "$SRC" -llog -ldl -lm \
  -o "$DIR/gralloc.samsungexynos7420.so"
"$LLVM/llvm-strip" --strip-unneeded "$DIR/gralloc.samsungexynos7420.so"

echo "=== 32-bit gralloc.default.32.so ==="
file "$DIR/gralloc.default.32.so"
"$LLVM/llvm-readelf" -d "$DIR/gralloc.default.32.so" | grep -E 'NEEDED|SONAME'
"$LLVM/llvm-nm" -D "$DIR/gralloc.default.32.so" | grep HMI

echo "=== 64-bit gralloc.samsungexynos7420.so ==="
file "$DIR/gralloc.samsungexynos7420.so"
"$LLVM/llvm-readelf" -d "$DIR/gralloc.samsungexynos7420.so" | grep -E 'NEEDED|SONAME'
"$LLVM/llvm-nm" -D "$DIR/gralloc.samsungexynos7420.so" | grep HMI

ls -lh "$DIR/gralloc.default.32.so" "$DIR/gralloc.samsungexynos7420.so"
