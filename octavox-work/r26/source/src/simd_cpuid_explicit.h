// OCTAVOX native core -- R24-W EXPLICIT CPUID DETECTION (internal header)
// ============================================================================
// PROVENANCE: our own implementation (round 24, Windows delivery fix). The
// canonical Intel SDM feature-detection sequence, checking AVX2 (leaf 7.0
// EBX bit 5) and FMA (leaf 1 ECX bit 12) INDEPENDENTLY, BOTH gated on
// OSXSAVE (leaf 1 ECX bit 27) plus XGETBV XCR0 bits 1:2 (XMM + YMM state):
// using YMM registers requires OPERATING-SYSTEM support, not just the CPUID
// leaf bits.
//
// R24-W DEFECT RECORD (the narrowly scoped Windows portability fix): zig
// 0.13.0 (clang frontend) targeting x86_64-windows-gnu does NOT provide
// the compiler-rt cpu_model runtime -- `__builtin_cpu_supports` compiles
// but FAILS TO LINK (undefined __cpu_indicator_init / __cpu_model;
// reproduced with a minimal probe, 2026-11-09). The dispatch translation
// unit therefore uses THIS explicit sequence on every Windows compiler
// (MSVC: <intrin.h> __cpuidex/_xgetbv; clang/zig/mingw: GNU inline asm --
// the zig target's <intrin.h> provides __cpuid/_xgetbv but NOT
// __cpuidex), while the Linux builds keep the R24-VALIDATED
// __builtin_cpu_supports path UNCHANGED (bit-exact record preserved).
//
// The original R24 MSVC branch gated only FMA on OSXSAVE/XCR0; this header
// additionally gates AVX2 (both levels use YMM state) -- recorded as the
// R24-W detection fix. Single source of truth: src/simd_dispatch.cpp
// includes this header for its explicit branch, and the
// octavox_r24w_cpuid_check target (native/CMakeLists.txt +
// cli/r24w_cpuid_check.cpp) compiles the dispatch TU with
// -DOCTAVOX_FORCE_EXPLICIT_CPUID and validates the algorithm on real Linux
// hardware against the compiler's own __builtin_cpu_supports: they must
// AGREE on both features (exit 0).
// ---------------------------------------------------------------------------
#pragma once

#include "octavox/simd.hpp"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>  // __cpuidex, _xgetbv (genuine MSVC)
#define OCTAVOX_CPUID_INTRIN 1
#endif

namespace octavox {
namespace simd {
namespace cpuid_explicit {

#if defined(OCTAVOX_CPUID_INTRIN)

inline void cpuidLeaf(int regs[4], int leafId, int subleaf) {
    __cpuidex(regs, leafId, subleaf);
}
inline unsigned long long readXCR0() { return _xgetbv(0); }

#else
// clang/zig windows-gnu + GCC/Clang Linux (validation builds): GNU inline
// asm -- identical instruction sequences, no compiler-rt dependency.
inline void cpuidLeaf(int regs[4], int leafId, int subleaf) {
    __asm__ __volatile__("cpuid"
                         : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]),
                           "=d"(regs[3])
                         : "a"(leafId), "c"(subleaf));
}
inline unsigned long long readXCR0() {
    unsigned int eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<unsigned long long>(edx) << 32) | eax;
}
#endif

/// The canonical explicit detection (see the file header): never executes
/// any vector instruction (cpuid/xgetbv only), so it is safe at the
/// baseline ISA and on every x86-64 processor.
inline Capabilities detect() {
    Capabilities caps{false, false};
    int regs[4];
    cpuidLeaf(regs, 0, 0);
    const int maxLeaf = regs[0];
    if (maxLeaf >= 1) {
        cpuidLeaf(regs, 1, 0);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool fmaLeaf = (regs[2] & (1 << 12)) != 0;
        bool avx2Leaf = false;
        if (maxLeaf >= 7) {
            cpuidLeaf(regs, 7, 0);
            avx2Leaf = (regs[1] & (1 << 5)) != 0;
        }
        // AVX2 AND FMA both use YMM registers: require OSXSAVE and
        // XCR0[2:1] (XMM+YMM state) before advertising either.
        bool ymmSaved = false;
        if (osxsave) {
            const unsigned long long x = readXCR0();
            ymmSaved = (x & 0x6u) == 0x6u;
        }
        caps.avx2 = avx2Leaf && ymmSaved;
        caps.fma = fmaLeaf && ymmSaved;
    }
    return caps;
}

}  // namespace cpuid_explicit
}  // namespace simd
}  // namespace octavox
