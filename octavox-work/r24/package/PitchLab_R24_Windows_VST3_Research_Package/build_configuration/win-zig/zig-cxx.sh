#!/bin/sh
# R24-Windows cross wrapper: zig c++ -> x86_64-windows-gnu (PE32+, MS x64 ABI).
# The pinned zig 0.13.0 -- the same toolchain that produced the accepted
# R21/R22 Windows x64 deliveries (recorded toolchain deviation).
exec /home/z/tools/zig/zig c++ -target x86_64-windows-gnu "$@"
