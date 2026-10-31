// ---------------------------------------------------------------------------
// OCTAVOX R18-WDB -- combined MinGW/libc++ force-include shim (SDK targets)
// ============================================================================
// Force-included (a SINGLE -include flag per target -- CMake deduplicates
// repeated '-include' tokens, which would orphan a second file argument)
// into the pinned VST3 SDK's build targets when compiling with llvm-mingw
// (x86_64-w64-mingw32, libc++). The SDK tree itself stays BYTE-UNTOUCHED.
//
//   1) <exception>: public.sdk/source/common/threadchecker_win32.cpp uses
//      std::terminate without including <exception> (MSVC/libstdc++ provide
//      it transitively; libc++ does not).
//   2) mingw_compat_aligned_alloc.h: provides ::aligned_alloc for
//      std::aligned_alloc (see that file's header comment for the full
//      analysis of the SDK's _MSC_VER guard and the dormancy record).
// ---------------------------------------------------------------------------
#pragma once

#include <exception>

#include "mingw_compat_aligned_alloc.h"
