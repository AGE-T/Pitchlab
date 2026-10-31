# ============================================================================
# OCTAVOX R18-WDB: Windows x64 cross-compilation toolchain (llvm-mingw)
# ============================================================================
# Host: Linux (Debian 13 x86_64). Target: Windows x64 (x86_64-w64-mingw32,
# PE32+ / COFF x86-64). Toolchain: llvm-mingw (clang 23.1.4, LLD, libc++).
#
# IMPORTANT HONESTY NOTE (recorded in WINDOWS_DUAL_BUILD_REPORT.md): the task
# text asks for "MSVC x64 Release". MSVC does not exist on this Linux host
# and cannot run here (no Windows runtime; wine is blocked by the sandbox's
# seccomp policy -- see the report). The builds therefore use LLVM/Clang
# targeting x86_64-w64-mingw32, which produces GENUINE native Windows PE32+
# x64 modules with the Microsoft x64 ABI (the mingw-w64 headers/imports and
# the MS x64 calling convention; VST3's COM-style interfaces are
# ABI-compatible with MSVC-built hosts). The toolchain deviation is recorded
# and does not weaken any gate: the same source is validated end-to-end on
# Linux builds of both variants, and the Windows binaries are structurally
# verified (PE32+ x86-64, exports, identity, dependencies).
#
# The C++ runtime (libc++ + libc++abi + winpthread) is linked STATICALLY so
# the module depends only on Windows system DLLs (no libstdc++-6.dll /
# libc++.dll / libwinpthread-1.dll runtime requirements on DAW machines).
# LLD is reproducible-by-default (PE timestamp = 0).
# ============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX "/home/z/tools/llvm-mingw/bin/x86_64-w64-mingw32")

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-clang)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-clang++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-llvm-ar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}-llvm-ranlib CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH /home/z/tools/llvm-mingw/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Fully static runtimes: modules/exes import only Windows system DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
