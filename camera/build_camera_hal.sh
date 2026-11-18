#!/bin/bash
set -euo pipefail
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

AOSP=/home/angger/aosp_cam
LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64
CXX="$LLVM/bin/aarch64-linux-android35-clang++"
STRIP="$LLVM/bin/llvm-strip"
AR="$LLVM/bin/llvm-ar"

CAM_DIR="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/zeroflte-android15-fixes/camera"
OUT_DIR="$AOSP/out_hal"
mkdir -p "$OUT_DIR"

# Recompile AIDL/metadata/FMQ static lib with -DBINDER_STABILITY_SUPPORT if missing stamp
STAMP="$AOSP/obj_vintf/.built"
if [ ! -f "$STAMP" ]; then
  rm -rf "$AOSP/obj_vintf" && mkdir -p "$AOSP/obj_vintf"
  INCS_BASE=(
    "-I$AOSP/shim_inc"
    "-I$AOSP/gen_ndk/include"
    "-I$AOSP/native/libs/binder/ndk/include_cpp"
    "-I$AOSP/native/libs/binder/ndk/include_ndk"
    "-I$AOSP/native/libs/binder/ndk/include_platform"
    "-I$AOSP/native/libs/nativebase/include"
    "-I$AOSP/native/libs/arect/include"
    "-I$AOSP/media/camera/include"
    "-I$AOSP/media/private/camera/include"
    "-I$AOSP/core/libcutils/include"
    "-I$AOSP/core/libsystem/include"
    "-I$AOSP/core/include"
    "-I$AOSP/libhardware/include_all"
    "-I$AOSP/libfmq/include"
    "-I$AOSP/libfmq/base"
    "-I$AOSP/libbase/include"
  )
  "$LLVM/bin/aarch64-linux-android35-clang" -std=c11 -O2 -fPIC -Wno-unused-parameter -Wno-unused-variable -include stdlib.h \
    "${INCS_BASE[@]}" -c "$AOSP/media/camera/src/camera_metadata.c" -o "$AOSP/obj_vintf/camera_metadata.o"
  "$CXX" -std=c++20 -O2 -fPIC -Wall -Wno-unused-parameter -Wno-missing-field-initializers -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
    "${INCS_BASE[@]}" -c "$AOSP/libfmq/EventFlag.cpp" -o "$AOSP/obj_vintf/EventFlag.o"
  "$CXX" -std=c++20 -O2 -fPIC -Wall -Wno-unused-parameter -Wno-missing-field-initializers -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
    "${INCS_BASE[@]}" -c "$AOSP/core/libcutils/native_handle.cpp" -o "$AOSP/obj_vintf/native_handle.o"
  "$CXX" -std=c++20 -O2 -fPIC -Wall -Wno-unused-parameter -Wno-missing-field-initializers -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
    "${INCS_BASE[@]}" -c "$AOSP/fmq_support.cpp" -o "$AOSP/obj_vintf/fmq_support.o"

  find "$AOSP/gen_ndk/src" -name '*.cpp' -print0 | xargs -0 -P"$(nproc)" -I{} bash -c '
    src="{}"
    rel="${src#'"$AOSP"'/gen_ndk/src/}"
    out="'"$AOSP/obj_vintf"'/aidl_${rel//\//_}.o"
    "'"$CXX"'" -std=c++20 -O2 -fPIC -Wall -Wno-unused-parameter -Wno-missing-field-initializers -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
      '"${INCS_BASE[*]}"' -c "$src" -o "$out"
  '
  "$AR" rcs "$AOSP/obj_vintf/libcam_aidl.a" "$AOSP/obj_vintf"/*.o
  touch "$STAMP"
fi

INCS=(
  "-I$CAM_DIR"
  "-I$AOSP/shim_inc"
  "-I$AOSP/gen_ndk/include"
  "-I$AOSP/native/libs/binder/ndk/include_cpp"
  "-I$AOSP/native/libs/binder/ndk/include_ndk"
  "-I$AOSP/native/libs/binder/ndk/include_platform"
  "-I$AOSP/native/libs/nativebase/include"
  "-I$AOSP/native/libs/arect/include"
  "-I$AOSP/media/camera/include"
  "-I$AOSP/media/private/camera/include"
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/libsystem/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libhardware/include_all"
  "-I$AOSP/libfmq/include"
  "-I$AOSP/libfmq/base"
  "-I$AOSP/libbase/include"
  "-I$AOSP/build_libjpeg/install/include"
  "-idirafter"
  "/home/angger/SM_G92X/include/uapi"
)

CXXFLAGS="-std=c++20 -O3 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-copy-with-user-provided-copy -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT"

SRCS=(
  "$CAM_DIR/camera_metadata_helper.cpp"
  "$CAM_DIR/gralloc_bridge.cpp"
  "$CAM_DIR/soft_isp.cpp"
  "$CAM_DIR/v4l2_pipeline.cpp"
  "$CAM_DIR/camera_device.cpp"
  "$CAM_DIR/camera_provider.cpp"
  "$CAM_DIR/main.cpp"
)

for s in "${SRCS[@]}"; do
  bn=$(basename "$s" .cpp)
  "$CXX" $CXXFLAGS "${INCS[@]}" -c "$s" -o "$OUT_DIR/${bn}.o"
done

"$CXX" -O3 \
  "$OUT_DIR"/*.o \
  "$AOSP/obj_vintf/libcam_aidl.a" \
  "$AOSP/build_libjpeg/install/lib/libjpeg.a" \
  "$AOSP/device_libs/libbinder_ndk.so" \
  -static-libstdc++ -llog -ldl -lm -pthread \
  -Wl,-rpath-link,"$AOSP/device_libs" \
  -o "$OUT_DIR/android.hardware.camera.provider-V3-service.unstripped"

cp "$OUT_DIR/android.hardware.camera.provider-V3-service.unstripped" \
   "$OUT_DIR/android.hardware.camera.provider-V3-service"
"$STRIP" --strip-unneeded "$OUT_DIR/android.hardware.camera.provider-V3-service"
cp "$OUT_DIR/android.hardware.camera.provider-V3-service" \
   "$CAM_DIR/android.hardware.camera.provider-V3-service"

"$LLVM/bin/llvm-readelf" -d "$OUT_DIR/android.hardware.camera.provider-V3-service" | grep -E 'NEEDED|SONAME'
ls -lh "$OUT_DIR/android.hardware.camera.provider-V3-service"
