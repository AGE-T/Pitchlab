# ============================================================================
# PITCHLAB R21: Windows x64 cross-compilation toolchain (ZIG)
# ============================================================================
# Host: Linux (Debian 13 x86_64). Target: Windows x64 (x86_64-w64-mingw32,
# PE32+ / COFF x86-64, MS x64 ABI, ucrt).
#
# HONESTY NOTE (the recorded toolchain deviation): R18-R20 used llvm-mingw
# (release 20260922, clang 23.1.x, libc++). That project's Linux-hosted
# (ubuntu) builds are no longer published (only Windows-hosted assets
# remain as of this round), and no wine/PE execution is possible in the
# sandbox. R21 therefore cross-compiles with the Zig toolchain
# (zig c++ -target x86_64-windows-gnu: clang/LLVM frontend, lld, mingw-w64
# headers, STATIC libc++), which produces GENUINE native PE32+ x64 modules
# with the Microsoft x64 ABI. The deviation is recorded in
# PITCHLAB_ROUND21_FINDINGS.md; it does not weaken any gate: the same
# source is validated end-to-end on Linux builds of every variant, and the
# Windows binaries are structurally verified (PE32+ x86-64, exports,
# identity records).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(ZIG_TOOLS_DIR "/home/z/my-project/octavox-work/r21/win-zig")
set(CMAKE_C_COMPILER   ${ZIG_TOOLS_DIR}/zig-cc.sh)
set(CMAKE_CXX_COMPILER ${ZIG_TOOLS_DIR}/zig-cxx.sh)

# zig's mingw target headers/libs are built-in; no FIND_ROOT needed, but
# keep the roots constrained to the toolchain like the R18 file did.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# the SDK's Win32 platform setup expects MSVC-ish paths; keep it minimal.
set(CMAKE_EXE_LINKER_FLAGS "")

# The zig wrapper is not auto-detected as MinGW by CMake's probe (it reads
# as a GNU-ish compiler); force the flags so our SDK shim logic engages
# (the SDK's alignedalloc.h Windows path is _MSC_VER-guarded -- MinGW
# builds need the mingw_compat_aligned_alloc.h force-include, which zig's
# libc++ target requires exactly like llvm-mingw's did).
set(MINGW 1)
# GNU binutils ar/ranlib handle COFF objects fine (verified: zig COFF obj
# -> GNU ar archive -> zig link = PE32+ exe).
set(CMAKE_AR /usr/bin/ar)
set(CMAKE_RANLIB /usr/bin/ranlib)
