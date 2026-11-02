#!/usr/bin/env bash
# ARMv5TE/no-VFP libraries for Android 1.5, isolated from newer ABI builds.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${ANDROID_NDK:?Set ANDROID_NDK to NDK r10e}"
export IJK_ANDROID_PLATFORM=android-3
export IJK_ARMV5_NO_VFP=1
export IJK_BUILD_SUFFIX=-api3
unset IJK_ARMV6_DEFAULT

cd "$ROOT/android/contrib"
if [ ! -d ffmpeg-armv5-api3 ]; then
    git clone --shared ffmpeg-armv5 ffmpeg-armv5-api3
fi
bash compile-ffmpeg.sh armv5

# Cupcake's linker does not match a preloaded absolute path to a DT_NEEDED
# basename. Link the three IJK components into one library for this platform.
FF_SOURCE="$ROOT/android/contrib/ffmpeg-armv5-api3"
FF_OUTPUT="$ROOT/android/contrib/build/ffmpeg-armv5-api3/output"
AR="$ANDROID_NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin/arm-linux-androideabi-ar"
case "$(uname -s)" in
    Linux*) AR="$ANDROID_NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-ar" ;;
    Darwin*) AR="$ANDROID_NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-ar" ;;
esac
# MRI preserves equal object basenames (e.g. each module's utils.o).
native_path() {
    case "$(uname -s)" in MSYS*|MINGW*|CYGWIN*) cygpath -am "$1" ;; *) printf '%s\n' "$1" ;; esac
}
{
    printf 'CREATE %s\n' "$(native_path "$FF_OUTPUT/libijkffmpeg.a")"
    for module in libavcodec libavformat libavfilter libswscale libswresample libavutil; do
        printf 'ADDLIB %s\n' "$(native_path "$FF_SOURCE/$module/$module.a")"
    done
    printf 'SAVE\nEND\n'
} | "$AR" -M

BUILD="$ROOT/build/api3"
mkdir -p "$BUILD/jni"
# Headers only for optional APIs resolved by dlopen; keep libc from API 3.
mkdir -p "$BUILD/compat-include/android"
for headers in EGL GLES GLES2 SLES KHR; do
    if [ -d "$ANDROID_NDK/platforms/android-9/arch-arm/usr/include/$headers" ]; then
        cp -R "$ANDROID_NDK/platforms/android-9/arch-arm/usr/include/$headers" "$BUILD/compat-include/"
    fi
done
cp "$ANDROID_NDK/platforms/android-9/arch-arm/usr/include/android/native_window"*.h "$BUILD/compat-include/android/"
cp "$ANDROID_NDK/platforms/android-9/arch-arm/usr/include/android/rect.h" "$BUILD/compat-include/android/"
cat > "$ROOT/ijkmedia/ijkplayer/ijkversion.h" <<'EOF'
#define IJKPLAYER_VERSION "0.8.8-api3"
#define IJKPLAYER_VERSION_INT 0x000808
EOF
cat > "$BUILD/jni/Application.mk" <<EOF
APP_OPTIM := release
APP_PLATFORM := android-3
APP_ABI := armeabi
APP_STL := stlport_static
NDK_TOOLCHAIN_VERSION := 4.9
APP_PIE := false
APP_CFLAGS := -O3 -Wall -pipe -DANDROID -DNDEBUG -I$BUILD/compat-include
EOF
cat > "$BUILD/jni/Android.mk" <<EOF
IJK_ANDROID_MONOLITHIC := 1
MY_APP_FFMPEG_OUTPUT_PATH := $ROOT/android/contrib/build/ffmpeg-armv5-api3/output
MY_APP_FFMPEG_INCLUDE_PATH := \$(MY_APP_FFMPEG_OUTPUT_PATH)/include
include \$(CLEAR_VARS)
LOCAL_MODULE := ijkffmpeg_static
LOCAL_SRC_FILES := \$(MY_APP_FFMPEG_OUTPUT_PATH)/libijkffmpeg.a
include \$(PREBUILT_STATIC_LIBRARY)
include $ROOT/ijkprof/android-ndk-profiler-dummy/jni/Android.mk
include $ROOT/ijkmedia/Android.mk
EOF
bash "$ROOT/android/tools/prepare-msys2-ndk.sh"
export NDK_HOST_AWK=awk
export GNUMAKE=make
"$ANDROID_NDK/ndk-build" -C "$BUILD" -j12
echo "API 3 libraries: $BUILD/libs/armeabi"
