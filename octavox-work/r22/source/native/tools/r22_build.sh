#!/bin/bash
# R22 native build (no cmake in this environment -- direct g++).
# Mirrors the CMakeLists targets exactly:
#   octavox_dsp (static lib), octavox_tests, the r21 CLIs and the r22 CLIs.
set -e
cd "$(dirname "$0")/.."
NATIVE=$(pwd)
BUILD=$NATIVE/../build
mkdir -p "$BUILD"

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Iinclude -Isrc"

echo "== building octavox_dsp (src/dsp.cpp + src/harmony.cpp) =="
g++ $CXXFLAGS -c src/dsp.cpp -o "$BUILD/dsp.o"
g++ $CXXFLAGS -c src/harmony.cpp -o "$BUILD/harmony.o"
ar rcs "$BUILD/liboctavox_dsp.a" "$BUILD/dsp.o" "$BUILD/harmony.o"

echo "== building octavox_tests =="
g++ $CXXFLAGS -DOCTAVOX_BUILD_TYPE=\"R22-manual\" \
    tests/test_main.cpp tests/wav_io.cpp tests/alloc_counter.cpp \
    "$BUILD/liboctavox_dsp.a" -o "$BUILD/octavox_tests"

# CLIs: r21_realaudio / r21_stereo / r22_realaudio need wav_io too
for cli in r21_fmin r21_harmony r21_realaudio r21_stereo; do
    echo "== building octavox_$cli =="
    extra=""
    case $cli in r21_realaudio|r21_stereo) extra="tests/wav_io.cpp";; esac
    g++ $CXXFLAGS cli/$cli.cpp $extra "$BUILD/liboctavox_dsp.a" -o "$BUILD/octavox_$cli"
done

for cli in r22_minpitch r22_panfb r22_realaudio r22_state; do
    if [ -f cli/$cli.cpp ]; then
        echo "== building octavox_$cli =="
        extra=""
        case $cli in r22_realaudio) extra="tests/wav_io.cpp";; esac
        g++ $CXXFLAGS cli/$cli.cpp $extra "$BUILD/liboctavox_dsp.a" -o "$BUILD/octavox_$cli"
    fi
done

echo "== build complete: $BUILD =="
