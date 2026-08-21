#!/usr/bin/env bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
CONTRIB_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SOURCE_DIR="$CONTRIB_ROOT/libxml2-mips"
TOOLCHAIN_DIR="$CONTRIB_ROOT/build/ffmpeg-mips/toolchain/bin"
OUTPUT_DIR="$CONTRIB_ROOT/build/ffmpeg-mips/output"

export PATH="$TOOLCHAIN_DIR:$PATH"
export CC=mipsel-linux-android-gcc

cd "$SOURCE_DIR"

# libxml2's configure probes execute target binaries under MSYS2. Compile the
# same feature set used by the working x86 build without running those probes.
cp "$CONTRIB_ROOT/libxml2-x86/config.h" config.h
cp "$CONTRIB_ROOT/libxml2-x86/include/libxml/xmlversion.h" include/libxml/xmlversion.h

CFLAGS="-O2 -fPIC -DANDROID -DNDEBUG -mips32r2 -msoft-float -DHAVE_CONFIG_H -I. -Iinclude"
SOURCES=(
    SAX.c entities.c encoding.c error.c parserInternals.c parser.c tree.c
    hash.c list.c xmlIO.c xmlmemory.c uri.c valid.c xlink.c HTMLparser.c
    HTMLtree.c debugXML.c xpath.c xpointer.c xinclude.c nanohttp.c nanoftp.c
    catalog.c globals.c threads.c c14n.c xmlstring.c buf.c xmlregexp.c
    xmlschemas.c xmlschemastypes.c xmlunicode.c xmlreader.c relaxng.c dict.c
    SAX2.c xmlwriter.c legacy.c chvalid.c pattern.c xmlsave.c xmlmodule.c
    schematron.c xzlib.c
)

for source in "${SOURCES[@]}"; do
    echo "[mips-libxml2] $source"
    "$CC" $CFLAGS -c "$source" -o "${source%.c}.o"
done

mkdir -p "$OUTPUT_DIR/lib" "$OUTPUT_DIR/include/libxml2"
mipsel-linux-android-ar rcs "$OUTPUT_DIR/lib/libxml2.a" *.o
mipsel-linux-android-ranlib "$OUTPUT_DIR/lib/libxml2.a"
rm -rf "$OUTPUT_DIR/include/libxml2/libxml"
cp -r include/libxml "$OUTPUT_DIR/include/libxml2/"
cp config.h "$OUTPUT_DIR/include/libxml2/"
