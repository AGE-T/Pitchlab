#!/bin/bash
# R22 VST3 wrapper test build (no cmake -- direct g++ against the pinned SDK).
# Builds the in-process wrapper test suite:
#   octavox_vst3_wrapper_tests  (vst3/octavox_vst3_tests.cpp)
# and the R22 state/mapper integration battery:
#   octavox_r22_state           (vst3/../cli path is NOT used; the tests
#                                live with the wrapper sources)
# Requires the VST3 SDK at ../../../vst3sdk (pinned SHA; NOT redistributed).
set -e
cd "$(dirname "$0")/.."
NATIVE=$(pwd)            # native/
VST3=$NATIVE/vst3
SDK=${VST3_SDK_ROOT:-$NATIVE/../../../vst3sdk}
BUILD=$NATIVE/../build
mkdir -p "$BUILD"

CXXFLAGS="-std=c++17 -O2 -DRELEASE -Wall -Wextra \
 -I$VST3 -I$NATIVE/include -I$NATIVE/src -I$SDK -I$SDK/pluginterfaces \
 -DOCTAVOX_VST3_INTERP_LAGRANGE6=1 -DOCTAVOX_VST3_DET_DECIMATED=1"

# --- the minimal SDK source set (single-component effect + parameters) ---
SDK_SRC="
$SDK/base/source/fobject.cpp
$SDK/base/source/fstreamer.cpp
$SDK/base/source/fbuffer.cpp
$SDK/base/thread/source/flock.cpp
$SDK/pluginterfaces/base/funknown.cpp
$SDK/base/source/updatehandler.cpp
$SDK/base/source/ustring.cpp
$SDK/public.sdk/source/vst/vstsinglecomponenteffect.cpp
$SDK/public.sdk/source/vst/vstparameters.cpp
$SDK/public.sdk/source/vst/vstbus.cpp
$SDK/public.sdk/source/vst/vstcomponentbase.cpp
$SDK/base/source/baseiids.cpp
$SDK/pluginterfaces/gui/guiiids.cpp
$SDK/pluginterfaces/base/coreiids.cpp
$SDK/public.sdk/source/vst/vstinitiids.cpp
$SDK/pluginterfaces/base/ustring.cpp
$SDK/base/source/fstring.cpp
$SDK/public.sdk/source/common/memorystream.cpp
$SDK/public.sdk/source/common/pluginview.cpp
$SDK/public.sdk/source/common/commoniids.cpp
$SDK/public.sdk/source/vst/hosting/parameterchanges.cpp
"
SDK_OBJS=""
for f in $SDK_SRC; do
    [ -f "$f" ] || continue
    o="$BUILD/$(basename "${f%.cpp}").sdk.o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        echo "== sdk: $(basename $f)"
        g++ $CXXFLAGS -c "$f" -o "$o" 2>/dev/null
    fi
    SDK_OBJS="$SDK_OBJS $o"
done

# --- the core + harmony + wrapper objects ---
echo "== core: dsp.cpp harmony.cpp"
g++ -std=c++17 -O2 -I$NATIVE/include -I$NATIVE/src -c $NATIVE/src/dsp.cpp -o "$BUILD/dsp.o"
g++ -std=c++17 -O2 -I$NATIVE/include -I$NATIVE/src -c $NATIVE/src/harmony.cpp -o "$BUILD/harmony.o"

echo "== wrapper: params/processor"
g++ $CXXFLAGS -c $VST3/octavox_params.cpp -o "$BUILD/w_params.o"
g++ $CXXFLAGS -c $VST3/octavox_native_processor.cpp -o "$BUILD/w_proc.o"
g++ $CXXFLAGS -c $VST3/factory.cpp -o "$BUILD/w_factory.o"
g++ -std=c++17 -O2 -I$NATIVE/include -c $NATIVE/tests/alloc_counter.cpp -o "$BUILD/w_alloc.o"

echo "== tests: octavox_vst3_wrapper_tests"
g++ $CXXFLAGS $VST3/octavox_vst3_tests.cpp \
    "$BUILD/w_params.o" "$BUILD/w_proc.o" "$BUILD/w_factory.o" \
    "$BUILD/dsp.o" "$BUILD/harmony.o" "$BUILD/w_alloc.o" \
    $SDK_OBJS -o "$BUILD/octavox_vst3_wrapper_tests"

if [ -f "$VST3/octavox_r22_state.cpp" ]; then
    echo "== tests: octavox_r22_state"
    g++ $CXXFLAGS $VST3/octavox_r22_state.cpp \
        "$BUILD/w_params.o" "$BUILD/w_proc.o" "$BUILD/dsp.o" "$BUILD/harmony.o" \
        "$BUILD/w_alloc.o" \
        $SDK_OBJS -o "$BUILD/octavox_r22_state"
fi

echo "== wrapper build complete: $BUILD"
