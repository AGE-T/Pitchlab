#!/bin/sh
# R22 Windows cross wrapper: zig c++ -> x86_64-windows-gnu (PE32+, MS ABI).
exec /home/z/zig/zig c++ -target x86_64-windows-gnu "$@"
