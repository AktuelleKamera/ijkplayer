#!/usr/bin/env bash
# NDK r10e detects MSYS2 as Windows and emits cmd.exe-only file commands.
# Rewrite only those host file operations; compiler/toolchain selection stays
# unchanged. The original file is kept beside the NDK as a one-time backup.
set -e

case "$(uname -s)" in
    MSYS*|MINGW*) ;;
    *) exit 0 ;;
esac

if [ -z "$ANDROID_NDK" ]; then
    exit 0
fi

core="$ANDROID_NDK/build/core/definitions-host.mk"
backup="$core.ijkplayer-msys2.bak"
marker="# ijkplayer MSYS2 host command compatibility"

if [ ! -f "$core" ] || grep -q "$marker" "$core"; then
    exit 0
fi

cp -f "$core" "$backup"
perl -0pi -e 's{ifeq \(\$\(HOST_OS\),windows\)\nhost-rm = \\\n.*?\nelse\nhost-rm = rm -f \$1\nendif}{host-rm = rm -f \$1}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-rmdir = \\\n.*?\nelse\nhost-rmdir = rm -rf \$1\nendif}{host-rmdir = rm -rf \$1}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-mkdir = md .*?\nelse\nhost-mkdir = mkdir -p \$1\nendif}{host-mkdir = mkdir -p \$1}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-cp = copy .*?\nelse\nhost-cp = cp -f \$1 \$2\nendif}{host-cp = cp -f \$1 \$2}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-mv = move .*?\nelse\nhost-mv = mv -f \$1 \$2\nendif}{host-mv = mv -f \$1 \$2}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-install = copy .*?\nelse\nhost-install = install -p \$1 \$2\nendif}{host-install = install -p \$1 \$2}sg; s{ifeq \(\$\(HOST_OS\),windows\)\nhost-copy-if-differ = .*?\nelse\nhost-copy-if-differ = \$\(HOST_CMP\) -s \$1 \$2 > /dev/null 2>&1 \|\| cp -f \$1 \$2\nendif}{host-copy-if-differ = \$\(HOST_CMP\) -s \$1 \$2 > /dev/null 2>&1 || cp -f \$1 \$2}sg' "$core"

printf '%s\n' "$marker" >> "$core"
