#!/usr/bin/env bash
# scripts/setup-xcb-vstgui-deps.sh — reproducible setup of the xcb helper
# libraries required by VSTGUI's Linux X11 platform, when the host lacks
# the xcb-util / xcb-image / xcb-renderutil / xcb-cursor development
# packages and no root is available.
#
# Builds (from the official freedesktop.org sources, MIT-licensed):
#   libxcb-util (aux), libxcb-image, libxcb-renderutil, libxcb-cursor
# into the user prefix /home/z/.local/xcb with pkg-config entries.
#
# shape_to_id.c is a hand-generated equivalent of libxcb-cursor's
# shape_to_id.gperf (gperf is unavailable here; identical mapping).
# Run once; idempotent (rebuilds from scratch).

set -euo pipefail

PREFIX="${XCB_PREFIX:-/home/z/.local/xcb}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Building xcb helper libraries into $PREFIX ..."
mkdir -p "$PREFIX/include/xcb" "$PREFIX/lib/pkgconfig" "$PREFIX/obj"

cd "$WORK"
for repo in libxcb-util libxcb-image libxcb-render-util libxcb-cursor; do
  git clone --depth 1 "https://gitlab.freedesktop.org/xorg/lib/$repo.git" "$repo"
done

# headers
cp libxcb-util/src/xcb_util.h libxcb-util/src/xcb_aux.h "$PREFIX/include/xcb/"
cp libxcb-image/image/xcb_image.h libxcb-image/image/xcb_bitops.h libxcb-image/image/xcb_pixel.h "$PREFIX/include/xcb/"
cp libxcb-render-util/renderutil/xcb_renderutil.h "$PREFIX/include/xcb/"
cp libxcb-cursor/cursor/xcb_cursor.h "$PREFIX/include/xcb/"

CC="gcc"
CF="-O2 -fPIC -I$PREFIX/include -I/usr/include"
C="libxcb-cursor/cursor"

$CC -c $CF libxcb-util/src/xcb_aux.c -o "$PREFIX/obj/xcb_aux.o"
$CC -c $CF libxcb-image/image/xcb_image.c -o "$PREFIX/obj/xcb_image.o"
$CC -c $CF libxcb-render-util/renderutil/cache.c -o "$PREFIX/obj/ru_cache.o"
$CC -c $CF libxcb-render-util/renderutil/glyph.c -o "$PREFIX/obj/ru_glyph.o"
$CC -c $CF libxcb-render-util/renderutil/util.c -o "$PREFIX/obj/ru_util.o"
$CC -c $CF -I"$C" "$C/cursor.c" -o "$PREFIX/obj/cursor_cursor.o"
$CC -c $CF -I"$C" -DXCURSORPATH="\"~/.icons:/usr/share/icons:/usr/share/pixmaps:/usr/X11R6/lib/X11/icons\"" "$C/load_cursor.c" -o "$PREFIX/obj/cursor_load_cursor.o"
$CC -c $CF -I"$C" "$C/parse_cursor_file.c" -o "$PREFIX/obj/cursor_parse_cursor_file.o"
$CC -c $CF -I"$C" "$(dirname "$0")/xcb-libs/shape_to_id.c" -o "$PREFIX/obj/shape_to_id.o"

ar rcs "$PREFIX/lib/libxcb-util.a" "$PREFIX/obj/xcb_aux.o"
ar rcs "$PREFIX/lib/libxcb-image.a" "$PREFIX/obj/xcb_image.o"
ar rcs "$PREFIX/lib/libxcb-renderutil.a" "$PREFIX/obj/ru_cache.o" "$PREFIX/obj/ru_glyph.o" "$PREFIX/obj/ru_util.o"
ar rcs "$PREFIX/lib/libxcb-cursor.a" "$PREFIX/obj/cursor_cursor.o" "$PREFIX/obj/cursor_load_cursor.o" "$PREFIX/obj/cursor_parse_cursor_file.o" "$PREFIX/obj/shape_to_id.o"

write_pc() {
  local name="$1" ver="$2" reqs="$3"
  cat > "$PREFIX/lib/pkgconfig/$name.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: $name
Description: $name (user-prefix static build for VSTGUI)
Version: $ver
Requires: $reqs
Libs: -L\${libdir} -l$(echo "$name" | tr - _)
Cflags: -I\${includedir}
EOF
}
write_pc xcb-util 0.4.1 xcb
write_pc xcb-image 0.4.1 xcb xcb-shm xcb-util
write_pc xcb-renderutil 0.3.10 xcb xcb-render
write_pc xcb-cursor 0.1.4 xcb xcb-render xcb-renderutil xcb-image

echo "Done. Export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig before configuring the VST3 build."
