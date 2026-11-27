#!/bin/sh
# moduleinfotool-host.sh — R24-W host-side bridge for the Windows cross
# build (recreated 2026-11-27 for R32-W; method per README_WINDOWS_BUILD
# §2 "Host-side bridges").
#
# The SDK post-build step invokes $<TARGET_FILE:moduleinfotool> — on the
# cross build that is a PE .exe the Linux host cannot run. Plant THIS
# script at that path after building the moduleinfotool target:
#
#   cmake --build build_win_r32 --target moduleinfotool
#   cp win-zig/moduleinfotool-host.sh <build>/bin/Release/moduleinfotool.exe
#   chmod +x <build>/bin/Release/moduleinfotool.exe
#   PATH="<abs>/win-zig/host-shims:$PATH" cmake --build build_win_r32 -j8
#
# Linux execve honors the shebang even for a .exe name, so the post-build
# runs this script instead of the PE. The moduleinfo.json is copied from
# the IDENTICAL-VARIANT LINUX TWIN build (compile-time factory metadata:
# same class IDs, same compatibility, same version — only the binary
# differs), which must be built first.
#
# Arguments: -create -version <v> -path <bundle> -output <json>
#   (exactly the invocation in SMTG_AddVST3Library.cmake; -compat optional)
TWIN="${OCTAVOX_TWIN_MODULEINFO:-/home/z/my-project/octavox-work/r26/source/vst3/build_r32/VST3/Release/PitchLab_Lagrange6_Balanced.vst3/Contents/Resources/moduleinfo.json}"

OUTPUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        -output) OUTPUT="$2"; shift 2 ;;
        -compat) shift 2 ;;   # accepted + ignored (twin already validated)
        *) shift ;;
    esac
done

if [ -z "$OUTPUT" ]; then
    echo "moduleinfotool-host.sh: no -output argument" >&2
    exit 2
fi
if [ ! -f "$TWIN" ]; then
    echo "moduleinfotool-host.sh: twin moduleinfo.json not found: $TWIN" >&2
    echo "  (build the identical-variant Linux twin first, or set OCTAVOX_TWIN_MODULEINFO)" >&2
    exit 3
fi
mkdir -p "$(dirname "$OUTPUT")"
cp "$TWIN" "$OUTPUT"
echo "[moduleinfotool-host] copied twin moduleinfo.json -> $OUTPUT"
exit 0
