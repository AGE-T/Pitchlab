#!/bin/sh
# zig-cc.sh — Zig 0.13.0 cross C compiler driver: x86_64-windows-gnu
# (clang/LLVM 18.1.6 frontend + lld + mingw-w64 headers, static libc).
# Recreated 2026-11-27 for R32-W after the sandbox resets lost the
# original R24-W wrappers (method recorded in README_WINDOWS_BUILD.md §2).
#
# The Zig binary lives OUTSIDE the repository (never committed):
#   default: $HOME/toolchains/zig-linux-x86_64-0.13.0/zig
#   override: ZIG_BIN=/path/to/zig
ZIG="${ZIG_BIN:-$HOME/toolchains/zig-linux-x86_64-0.13.0/zig}"
if [ ! -x "$ZIG" ]; then
    echo "zig-cc.sh: zig not found at $ZIG (set ZIG_BIN or install zig 0.13.0)" >&2
    exit 127
fi
exec "$ZIG" cc -target x86_64-windows-gnu "$@"
