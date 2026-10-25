#!/usr/bin/env bash
# Builds libtabsopt.so for arm64-v8a.
# Needs Android NDK r26 or newer.
#
#   NDK=/path/to/ndk ./native/build.sh
#
set -e

NDK="${NDK:-$ANDROID_NDK_HOME}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "set NDK first, e.g. NDK=~/android-ndk-r27 ./native/build.sh" >&2
    exit 1
fi

API="${API:-26}"
OUT="${1:-build}"
mkdir -p "$OUT"

HOST_TAG=$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)
BIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
CC="$BIN/aarch64-linux-android${API}-clang"
STRIP="$BIN/llvm-strip"

SRC="$(dirname "$0")/libtabsopt.c"
echo "CC     = $CC"
echo "SRC    = $SRC"

"$CC" -O2 -fPIC -shared \
    -Wl,-soname,libtabsopt.so \
    -Wl,-z,max-page-size=16384 \
    -o "$OUT/libtabsopt.so" "$SRC" \
    -llog -ldl -landroid

"$STRIP" --strip-unneeded "$OUT/libtabsopt.so"
ls -l "$OUT/libtabsopt.so"
echo
echo "exported symbols:"
"$BIN/llvm-readelf" --dyn-syms -W "$OUT/libtabsopt.so" | grep -E 'libmain_(set|get)_override' || true