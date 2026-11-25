#!/bin/sh
# The libretro frontend with a real emulator core: Gambatte (GPLv2, built here, pinned) running dmg-acid2 (MIT, Matt
# Currie's Game Boy PPU test). Neither is shipped with VeyAmux; they are fetched for the test only.
#   tests/run_retro_test.sh <build dir>
set -eu
BUILD=$(cd "$1" && pwd)
WORK=${TMPDIR:-/tmp}/veyamux-retro-core
GAMBATTE=138bcbbf311300a271896bf93a76370c72cab339
mkdir -p "$WORK"
if [ ! -f "$WORK/gambatte_libretro.so" ]; then
    rm -rf "$WORK/gambatte"
    git clone -q https://github.com/libretro/gambatte-libretro "$WORK/gambatte"
    git -C "$WORK/gambatte" checkout -q "$GAMBATTE"
    make -s -C "$WORK/gambatte" -j"$(nproc)"
    cp "$WORK/gambatte/gambatte_libretro.so" "$WORK/"
fi
if [ ! -f "$WORK/dmg-acid2.gb" ]; then
    curl -sSfL -o "$WORK/dmg-acid2.gb" https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb
fi
echo "464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1  $WORK/dmg-acid2.gb" | sha256sum -c -
"$BUILD/veyamux-retro-test" "$WORK/gambatte_libretro.so" "$WORK/dmg-acid2.gb"
