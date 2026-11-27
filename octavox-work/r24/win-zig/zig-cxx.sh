#!/bin/sh
# zig-cxx.sh — Zig 0.13.0 cross C++ compiler driver: x86_64-windows-gnu.
# See zig-cc.sh for the provenance note (R24-W method, recreated for R32-W).
ZIG="${ZIG_BIN:-$HOME/toolchains/zig-linux-x86_64-0.13.0/zig}"
if [ ! -x "$ZIG" ]; then
    echo "zig-cxx.sh: zig not found at $ZIG (set ZIG_BIN or install zig 0.13.0)" >&2
    exit 127
fi
exec "$ZIG" c++ -target x86_64-windows-gnu "$@"
