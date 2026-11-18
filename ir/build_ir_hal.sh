#!/bin/bash
set -euo pipefail
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

AOSP=/home/angger/aosp_cam
AIDL=/home/angger/tools/android-15/aidl
LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64
CXX="$LLVM/bin/aarch64-linux-android35-clang++"
AR="$LLVM/bin/llvm-ar"
STRIP="$LLVM/bin/llvm-strip"

IR_DIR="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/zeroflte-android15-fixes/ir"
GEN_DIR="$AOSP/gen_ir_ndk"
OBJ_DIR="$AOSP/obj_ir_vintf"
OUT_DIR="$AOSP/out_ir"
mkdir -p "$GEN_DIR/include" "$GEN_DIR/src" "$OBJ_DIR" "$OUT_DIR"

"$AIDL" --lang=ndk --structured --stability=vintf \
  -I"$AOSP/interfaces/ir/aidl" \
  -I"$AOSP/interfaces/common/aidl" \
  -h "$GEN_DIR/include" \
  -o "$GEN_DIR/src" \
  "$AOSP/interfaces/ir/aidl/android/hardware/ir/"*.aidl

INCS=(
  "-I$AOSP/shim_inc"
  "-I$GEN_DIR/include"
  "-I$AOSP/native/libs/binder/ndk/include_cpp"
  "-I$AOSP/native/libs/binder/ndk/include_ndk"
  "-I$AOSP/native/libs/binder/ndk/include_platform"
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libbase/include"
)

OBJS=()
for src in $(find "$GEN_DIR/src" -name '*.cpp' | sort); do
  obj="$OBJ_DIR/$(basename "$src").o"
  "$CXX" -std=c++20 -O2 -fPIC -Wall -Wno-unused-parameter -Wno-missing-field-initializers \
    -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT "${INCS[@]}" -c "$src" -o "$obj"
  OBJS+=("$obj")
done
"$AR" rcs "$OBJ_DIR/libir_aidl.a" "${OBJS[@]}"

"$CXX" -std=c++20 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
  -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT "${INCS[@]}" \
  "$IR_DIR/ir_service.cpp" \
  "$OBJ_DIR/libir_aidl.a" \
  "$AOSP/device_libs/libbinder_ndk.so" \
  -static-libstdc++ -llog -pthread \
  -Wl,-rpath-link,"$AOSP/device_libs" \
  -o "$OUT_DIR/android.hardware.ir-service.universal7420"

"$STRIP" --strip-unneeded "$OUT_DIR/android.hardware.ir-service.universal7420"
cp "$OUT_DIR/android.hardware.ir-service.universal7420" "$IR_DIR/android.hardware.ir-service.universal7420"
"$LLVM/bin/llvm-readelf" -d "$IR_DIR/android.hardware.ir-service.universal7420" | grep NEEDED
ls -lh "$IR_DIR/android.hardware.ir-service.universal7420"
