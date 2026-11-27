# zig-win-toolchain.cmake — cross-compile Windows x64 with Zig 0.13.0
# (x86_64-windows-gnu: clang/LLVM 18.1.6 frontend, lld, mingw-w64 headers,
# static libc++). Recreated 2026-11-27 for R32-W after the sandbox resets
# lost the original R24-W toolchain file; the method is recorded in
# README_WINDOWS_BUILD.md §2 (that build is the accepted precedent for the
# R21/R22/R24-W Windows deliveries).
#
# Usage:
#   cmake -S source/vst3 -B build_win_r32 -G "Unix Makefiles" \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 \
#         -DOCTAVOX_VST3_DETECTOR=DECIMATED \
#         -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF \
#         -DCMAKE_TOOLCHAIN_FILE=<this file> \
#         -DVST3_SDK_ROOT=<abs>/vst3sdk
#
# The compiler wrappers resolve the zig binary from ZIG_BIN or the default
# $HOME/toolchains path — the toolchain itself never lives in the repo.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

get_filename_component(OCTAVOX_ZIG_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(CMAKE_C_COMPILER "${OCTAVOX_ZIG_DIR}/zig-cc.sh")
set(CMAKE_CXX_COMPILER "${OCTAVOX_ZIG_DIR}/zig-cxx.sh")

# Cross binaries cannot be executed on the Linux build host: compile-only
# compiler ID checks (link a static library, never run).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# lld (zig's bundled linker) handles PE import libraries directly; no
# ranlib/ar wrapper is needed for the static libs (zig drives llvm-ar).
