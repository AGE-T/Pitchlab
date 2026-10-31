// ---------------------------------------------------------------------------
// OCTAVOX R18-WDB -- MinGW/libc++ compatibility shim: ::aligned_alloc
// ============================================================================
// FORCE-INCLUDED (via -include) into the pinned VST3 SDK's build targets
// when compiling with llvm-mingw (x86_64-w64-mingw32, libc++). The SDK tree
// itself stays BYTE-UNTOUCHED (the pinned-SHA verification still passes);
// this shim is applied from OUR build scope only.
//
// WHY: public.sdk/source/vst/utility/alignedalloc.h selects its
// implementation with `#elif defined(_MSC_VER)`, so a MinGW build takes the
// POSIX branch and calls std::aligned_alloc -- which llvm-mingw's libc++
// does not provide (Windows CRT has no C11 aligned_alloc; UCRT offers
// _aligned_malloc/_aligned_free). libc++ declares
// `using ::aligned_alloc _LIBCPP_USING_IF_EXISTS;` in <cstdlib>, so a GLOBAL
// declaration made BEFORE <cstdlib> is first parsed is picked up by
// std::aligned_alloc exactly. This shim provides that declaration.
//
// CAVEAT (documented, dormant): the SDK's aligned_free() non-MSVC branch
// frees the block with std::free(), while _aligned_malloc blocks must be
// freed with _aligned_free(). The pairing is therefore only sound if the
// aligned path is never exercised. Analysis for THIS build:
//   * the only SDK callers of Steinberg::Vst::aligned_alloc/aligned_free are
//     public.sdk/source/vst/utility/dataexchange.cpp and
//     public.sdk/source/vst/hosting/hostdataexchangehandler.cpp -- the
//     OPTIONAL VST3 data-exchange feature;
//   * our plugin module never implements/opts into IDataExchangeHandler, so
//     the module never calls it;
//   * our wrapper tests and the compiled-module integration harness drive
//     the plain component/audio/controller interfaces only;
//   * the Steinberg validator's test suites (general/bus/state/processing/
//     noteexpression/unit) contain no data-exchange test, and the feature
//     could only activate for a plugin that advertises it.
// The code therefore compiles to correct, DORMANT symbols in every binary
// delivered this round. Recorded in WINDOWS_DUAL_BUILD_REPORT.md.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <malloc.h>  // _aligned_malloc (UCRT / mingw-w64)

#ifdef __cplusplus
extern "C" {
#endif

/* C11 aligned_alloc semantics over the Windows UCRT aligned allocator.
   Declared at GLOBAL scope so that libc++'s
   `using ::aligned_alloc _LIBCPP_USING_IF_EXISTS;` (<cstdlib>) resolves
   std::aligned_alloc to this definition. `static __inline` gives every
   including TU its own copy (no cross-TU linkage issues, C and C++). */
static __inline void* aligned_alloc(size_t alignment, size_t size)
{
    if (alignment == 0)  /* defensive: C11 requires a power of two > 0 */
        alignment = sizeof(void*);
    /* _aligned_malloc's parameter order is (size, alignment). */
    return _aligned_malloc(size, alignment);
}

#ifdef __cplusplus
}
#endif
