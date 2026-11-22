# R24-Windows x64 cross-compilation toolchain (ZIG 0.13.0) -- the R21/R22
# recorded method, paths updated for the R24 tree. Honesty note: this
# sandbox is Linux; no Visual Studio 2022 / MSVC / Windows SDK exists here
# (native MSVC build = BLOCKED in this environment, documented in the
# delivery report). Zig (clang/LLVM frontend, lld, mingw-w64 headers,
# static libc++) produces GENUINE PE32+ x64 modules with the Microsoft x64
# ABI importing only Windows system DLLs. The same sources + variant
# defines are validated end-to-end on Linux; the Windows modules are
# structurally verified (PE, exports, FUIDs, moduleinfo) and the Windows
# Validation Kit ships for on-target verification.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(ZIG_TOOLS_DIR "/home/z/my-project/octavox-work/r24/win-zig")
set(CMAKE_C_COMPILER   ${ZIG_TOOLS_DIR}/zig-cc.sh)
set(CMAKE_CXX_COMPILER ${ZIG_TOOLS_DIR}/zig-cxx.sh)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_EXE_LINKER_FLAGS "")
set(MINGW 1)
set(CMAKE_AR /usr/bin/ar)
set(CMAKE_RANLIB /usr/bin/ranlib)
