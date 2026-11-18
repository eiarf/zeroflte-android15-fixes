#!/bin/bash
set -euo pipefail
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

AOSP=/home/angger/aosp_cam
LLVM=/home/angger/toolchains/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64
CXX="$LLVM/bin/aarch64-linux-android35-clang++"
STRIP="$LLVM/bin/llvm-strip"

NFC_DIR="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/zeroflte-android15-fixes/nfc"
HALIMPL="/mnt/c/Users/Angger/.gemini/antigravity/scratch/repos/android_device_samsung_universal7420-common/hardware/nfc/halimpl"
OUT_DIR="$AOSP/out_nfc"
mkdir -p "$OUT_DIR"

# Ensure libcrypto.so is pulled from device if needed
if [ ! -f "$AOSP/device_libs/libcrypto.so" ]; then
  adb.exe -s 06157df6085a9c3c pull /vendor/lib64/libcrypto.so "$OUT_DIR/libcrypto.so" >/dev/null 2>&1 || true
  if [ -f "$OUT_DIR/libcrypto.so" ]; then
    cp "$OUT_DIR/libcrypto.so" "$AOSP/device_libs/libcrypto.so"
  fi
fi

INCS=(
  "-I$NFC_DIR/shim"
  "-I$HALIMPL/include"
  "-I$HALIMPL/osi"
  "-I$AOSP/shim_inc"
  "-I$AOSP/gen_nfc_ndk/include"
  "-I$AOSP/native/libs/binder/ndk/include_cpp"
  "-I$AOSP/native/libs/binder/ndk/include_ndk"
  "-I$AOSP/native/libs/binder/ndk/include_platform"
  "-I$AOSP/core/libcutils/include"
  "-I$AOSP/core/libsystem/include"
  "-I$AOSP/core/include"
  "-I$AOSP/libhardware/include_all"
  "-I$AOSP/libbase/include"
)

HAL_CFLAGS="-std=c++17 -O2 -fPIC -DBUILDCFG=1 -DANDROID -DNFC_HAL_TARGET=TRUE -DNFC_RW_ONLY=TRUE -DNFC_SEC_NOT_OPEN_INCLUDED=TRUE -Wno-unused-parameter -Wno-unused-variable -Wno-write-strings -Wno-format -Wno-sign-compare -Wno-missing-field-initializers -Wno-deprecated-declarations -Wno-c++11-narrowing -Wno-pointer-bool-conversion"

OBJS=()
for s in "$HALIMPL"/osi/*.cc "$HALIMPL"/src/*.cc "$HALIMPL"/src/*.cpp; do
  bn=$(basename "$s")
  obj="$OUT_DIR/${bn}.o"
  "$CXX" $HAL_CFLAGS "${INCS[@]}" -c "$s" -o "$obj"
  OBJS+=("$obj")
done

"$CXX" -std=c++20 -O2 -fPIC -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
  -D__ANDROID_VNDK__ -DBINDER_STABILITY_SUPPORT \
  "${INCS[@]}" -c "$NFC_DIR/nfc_service.cpp" -o "$OUT_DIR/nfc_service.o"
OBJS+=("$OUT_DIR/nfc_service.o")

# Provide property_get from libcutils or tiny shim
cat > "$OUT_DIR/prop_shim.c" << 'EOF'
#include <sys/system_properties.h>
#include <string.h>
int property_get(const char* key, char* value, const char* default_value) {
    int len = __system_property_get(key, value);
    if (len > 0) return len;
    if (default_value) {
        size_t dlen = strlen(default_value);
        if (dlen > 91) dlen = 91;
        memcpy(value, default_value, dlen);
        value[dlen] = '\0';
        return (int)dlen;
    }
    value[0] = '\0';
    return 0;
}
int property_set(const char* key, const char* value) {
    (void)key; (void)value;
    return 0;
}
EOF
"$LLVM/bin/aarch64-linux-android35-clang" -O2 -fPIC -c "$OUT_DIR/prop_shim.c" -o "$OUT_DIR/prop_shim.o"
OBJS+=("$OUT_DIR/prop_shim.o")
"$LLVM/bin/aarch64-linux-android35-clang" -O2 -fPIC -I"$NFC_DIR/shim" -c "$NFC_DIR/sha_impl.c" -o "$OUT_DIR/sha_impl.o"
OBJS+=("$OUT_DIR/sha_impl.o")

"$CXX" -O2 \
  "${OBJS[@]}" \
  "$AOSP/obj_nfc_vintf/libnfc_aidl.a" \
  "$AOSP/device_libs/libbinder_ndk.so" \
  -static-libstdc++ -llog -ldl -lm -pthread \
  -Wl,-rpath-link,"$AOSP/device_libs" \
  -o "$OUT_DIR/android.hardware.nfc-service.exynos7420"

"$STRIP" --strip-unneeded "$OUT_DIR/android.hardware.nfc-service.exynos7420"
cp "$OUT_DIR/android.hardware.nfc-service.exynos7420" "$NFC_DIR/android.hardware.nfc-service.exynos7420"

"$LLVM/bin/llvm-readelf" -d "$OUT_DIR/android.hardware.nfc-service.exynos7420" | grep NEEDED
ls -lh "$NFC_DIR/android.hardware.nfc-service.exynos7420"
