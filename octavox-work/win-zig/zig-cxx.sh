#!/bin/sh
# R21 Windows cross wrapper: zig c++ -> x86_64-windows-gnu (PE32+, MS ABI).
exec /home/z/tools/zig/zig c++ -target x86_64-windows-gnu "$@"
