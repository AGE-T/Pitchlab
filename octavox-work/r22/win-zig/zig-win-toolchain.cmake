# R22 Windows x64 cross-compilation toolchain (ZIG) -- the R21 method,
# paths updated. See the R21 toolchain file's honesty note (recorded
# deviation: llvm-mingw Linux-hosted builds unavailable; zig produces
# genuine PE32+ x64 modules with the MS x64 ABI).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(ZIG_TOOLS_DIR "/home/z/my-project/octavox-work/r22/win-zig")
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
