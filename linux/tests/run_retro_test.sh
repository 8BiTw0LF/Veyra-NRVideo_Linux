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
# a core that crashes after 40 frames: in the child process it must end the game, not the test
SRC=$(cd "$(dirname "$0")" && pwd)
cc -shared -fPIC -O1 -o "$WORK/crash_libretro.so" "$SRC/crash_core.c"
# and one that draws with OpenGL (as N64 / Dreamcast / PSP emulators do): Mesa's software OpenGL on CI
cc -shared -fPIC -O1 -o "$WORK/gl_libretro.so" "$SRC/gl_core.c"
"$BUILD/veyamux-retro-test" "$WORK/gambatte_libretro.so" "$WORK/dmg-acid2.gb" "$WORK/crash_libretro.so" "$WORK/gl_libretro.so"
# again with an X display (Xvfb): the OpenGL core must then get GLX, as PPSSPP's GLEW needs
if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a -s "-screen 0 640x480x24" "$BUILD/veyamux-retro-test" "$WORK/gambatte_libretro.so" "$WORK/dmg-acid2.gb" \
        "$WORK/crash_libretro.so" "$WORK/gl_libretro.so"
fi

# Genesis Plus GX (non-commercial licence; built here for the test only, never shipped) with a tiny Mega Drive
# program made on the spot: it passes no data for "no disc control" on cartridges, which crashed VeyAmux once.
GPGX=51348f65738b0c75f4b8b4488fbc67645e3769af
if [ ! -f "$WORK/genesis_plus_gx_libretro.so" ]; then
    rm -rf "$WORK/gpgx"
    git clone -q https://github.com/libretro/Genesis-Plus-GX "$WORK/gpgx"
    git -C "$WORK/gpgx" checkout -q "$GPGX"
    make -s -C "$WORK/gpgx" -f Makefile.libretro -j"$(nproc)"
    cp "$WORK/gpgx/genesis_plus_gx_libretro.so" "$WORK/"
fi
python3 -c "
r = bytearray(0x80000)
r[0:8] = bytes([0, 0xff, 0, 0, 0, 0, 2, 0])         # stack, entry 0x200
r[0x100:0x110] = b'SEGA MEGA DRIVE '
r[0x200:0x202] = bytes([0x60, 0xfe])                # bra.s * (a loop)
open('$WORK/test.md', 'wb').write(r)"
"$BUILD/veyamux-retro-test" "$WORK/genesis_plus_gx_libretro.so" "$WORK/test.md"
