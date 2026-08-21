# Windows / MSYS2 build guide (Android armeabi-v7a)

This copy already contains the FFmpeg 4.0 DASH build and the cross-compiled
libxml2 dependency. Keep `android/contrib/build/` when creating the archive.
Do not run `compile-ffmpeg.sh clean` on a fresh unpacked archive: this removes
the prebuilt libxml2/toolchain outputs and there is no one-command libxml2
bootstrap in this old IJK tree.

## Required tools

- MSYS2, for example `E:\Software\msys2`
- Android NDK r10e
- Android SDK with platform 9 (the old NDK toolchain is used for the native build)
- Git is only needed if source files must be downloaded or changed

The paths may be different on another computer. Use MSYS2 paths (`/e/...`) in
the shell, not Windows paths with backslashes.

## Fast build from this archive

Open `E:\Software\msys2\usr\bin\bash.exe`, then run:

```sh
export ANDROID_NDK=/e/Software/Android/Sdkold/ndk/android-ndk-r10e
export ANDROID_SDK=/e/Software/Android/Sdkold
export http_proxy=http://127.0.0.1:7890
export https_proxy=http://127.0.0.1:7890

cd /e/path/to/ijkplayer/android/contrib
sh compile-ffmpeg.sh armv7a

cd ..
sh compile-ijk.sh armv7a
```

The scripts use `-j12` automatically under MSYS2. To override it, edit the
last `IJK_MAKE_FLAG` assignment in `android/contrib/tools/do-detect-env.sh` or
run a smaller value on machines with less memory.

The native libraries are produced at:

```text
android/ijkplayer/ijkplayer-armv7a/src/main/libs/armeabi-v7a/
```

Expected files are `libijkffmpeg.so`, `libijkplayer.so`, and `libijksdl.so`.
The FFmpeg configuration should contain `CONFIG_DASH_DEMUXER 1` and
`CONFIG_LIBXML2 1` in:

```text
android/contrib/build/ffmpeg-armv7a/output/include/libffmpeg/config.h
```

## Clean rebuild warning

Do not use `compile-ffmpeg.sh clean` unless libxml2 is rebuilt first. A clean
build requires the existing `libxml2-2.9.14.tar.xz`, a compatible autotools
setup, and the Android r10e cross toolchain. The packaged `build/` directory is
intentionally included to avoid that slow and fragile bootstrap step.

## Common errors

- `ANDROID_NDK is empty`: export `ANDROID_NDK` again in the same MSYS2 window.
- `You need the NDKr10e or later`: the path is not an NDK r10e directory.
- `libxml-2.0 not found using pkg-config`: `android/contrib/build/` was not
  included, or the build was cleaned. Restore that directory from the archive.
- `nproc: command not found`: this is harmless; the script still uses `-j12`
  on MSYS2. If the shell is Cygwin, use the Cygwin branch or set a lower job
  count.
- Windows paths containing spaces: use the MSYS2 form, such as
  `/e/Software/Android/Sdkold/ndk/android-ndk-r10e`.
