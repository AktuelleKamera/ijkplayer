#! /usr/bin/env bash
#
# Copyright (C) 2013-2014 Bilibili
# Copyright (C) 2013-2014 Zhang Rui <bbcallen@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

if [ -z "$ANDROID_NDK" -o -z "$ANDROID_NDK" ]; then
    echo "You must define ANDROID_NDK, ANDROID_SDK before starting."
    echo "They must point to your NDK and SDK directories.\n"
    exit 1
fi

REQUEST_TARGET=$1
REQUEST_SUB_CMD=$2
ACT_ABI_32="armv5 armv7a x86 mips"
ACT_ABI_64="armv5 armv7a arm64 x86 x86_64 mips mips64"
ACT_ABI_ALL=$ACT_ABI_64
UNAME_S=$(uname -s)

FF_MAKEFLAGS=
NDK_BUILD_EXTRA_ARGS=
if [ -n "$IJK_ANDROID_PLATFORM" ]; then
    NDK_BUILD_EXTRA_ARGS="APP_PLATFORM=$IJK_ANDROID_PLATFORM"
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
export PATH="$SCRIPT_DIR/tools:$PATH"
"$SCRIPT_DIR/tools/prepare-msys2-ndk.sh"
if which nproc >/dev/null
then
    FF_MAKEFLAGS=-j`nproc`
elif [ "$UNAME_S" = "Darwin" ] && which sysctl >/dev/null
then
    FF_MAKEFLAGS=-j`sysctl -n machdep.cpu.thread_count`
fi

do_sub_cmd () {
    SUB_CMD=$1
    if [ -e "./android-ndk-prof" ] || [ -L "./android-ndk-prof" ]; then
        rm -rf android-ndk-prof
    fi

    if [ "$PARAM_SUB_CMD" = 'prof' ]; then
        echo 'profiler build: YES';
        ln -s ../../../../../../ijkprof/android-ndk-profiler/jni android-ndk-prof
    else
        echo 'profiler build: NO';
        ln -s ../../../../../../ijkprof/android-ndk-profiler-dummy/jni android-ndk-prof
    fi

    case $SUB_CMD in
        prof)
            $ANDROID_NDK/ndk-build $FF_MAKEFLAGS $NDK_BUILD_EXTRA_ARGS
        ;;
        clean)
            $ANDROID_NDK/ndk-build clean $NDK_BUILD_EXTRA_ARGS
        ;;
        rebuild)
            $ANDROID_NDK/ndk-build clean $NDK_BUILD_EXTRA_ARGS
            $ANDROID_NDK/ndk-build $FF_MAKEFLAGS $NDK_BUILD_EXTRA_ARGS
        ;;
        *)
            $ANDROID_NDK/ndk-build $FF_MAKEFLAGS $NDK_BUILD_EXTRA_ARGS
        ;;
    esac
}

do_ndk_build () {
    PARAM_TARGET=$1
    PARAM_SUB_CMD=$2
    case "$PARAM_TARGET" in
        armv5|armv7a)
            cd "ijkplayer/ijkplayer-$PARAM_TARGET/src/main/jni"
            do_sub_cmd $PARAM_SUB_CMD
            cd -
        ;;
        arm64|x86|x86_64|mips|mips64)
            if [ "$PARAM_TARGET" = "mips" ]; then
                # Reuse the common JNI sources; APP_ABI selects the MIPS ABI.
                cd "ijkplayer/ijkplayer-armv7a/src/main/jni"
                NDK_BUILD_EXTRA_ARGS="APP_ABI=mips"
            elif [ "$PARAM_TARGET" = "mips64" ]; then
                # 同理复用通用 JNI 源；mips64 需要 android-21 平台
                cd "ijkplayer/ijkplayer-armv7a/src/main/jni"
                NDK_BUILD_EXTRA_ARGS="APP_ABI=mips64 APP_PLATFORM=android-21"
            else
                cd "ijkplayer/ijkplayer-$PARAM_TARGET/src/main/jni"
            fi
            if [ "$PARAM_SUB_CMD" = 'prof' ]; then PARAM_SUB_CMD=''; fi
            do_sub_cmd $PARAM_SUB_CMD
            NDK_BUILD_EXTRA_ARGS=
            cd -
        ;;
    esac
}


case "$REQUEST_TARGET" in
    "")
        do_ndk_build armv7a;
    ;;
    armv5|armv7a|arm64|x86|x86_64|mips|mips64)
        do_ndk_build $REQUEST_TARGET $REQUEST_SUB_CMD;
    ;;
    all32)
        for ABI in $ACT_ABI_32
        do
            do_ndk_build "$ABI" $REQUEST_SUB_CMD;
        done
    ;;
    all|all64)
        for ABI in $ACT_ABI_64
        do
            do_ndk_build "$ABI" $REQUEST_SUB_CMD;
        done
    ;;
    clean)
        for ABI in $ACT_ABI_ALL
        do
            do_ndk_build "$ABI" clean;
        done
    ;;
    *)
        echo "Usage:"
        echo "  compile-ijk.sh armv5|armv7a|arm64|x86|x86_64|mips|mips64"
        echo "  compile-ijk.sh all|all32"
        echo "  compile-ijk.sh all64"
        echo "  compile-ijk.sh clean"
    ;;
esac

