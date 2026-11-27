// OCTAVOX native core -- R24-W EXPLICIT-CPUID CROSS-CHECK (validation tool)
// ============================================================================
// PROVENANCE: our own implementation (round 24, Windows delivery). This tool
// validates the EXPLICIT CPUID+XGETBV detection algorithm used by the
// WINDOWS builds (src/simd_cpuid_explicit.h via the dispatch TU compiled
// with -DOCTAVOX_FORCE_EXPLICIT_CPUID) against the compiler's own
// __builtin_cpu_supports resolver ON REAL LINUX HARDWARE. Both answers must
// AGREE on both features; any disagreement is a hard failure (exit 1).
//
// This is a HOST-side validation harness -- it proves the algorithm the
// Windows modules embed produces the same capabilities the validated Linux
// path reports on the same silicon. It is NOT a Windows execution claim
// (the Windows modules themselves are structurally verified; see the
// round evidence).
// ---------------------------------------------------------------------------
#include "octavox/simd.hpp"

#include <cstdio>

#if !defined(__GNUC__) && !defined(__clang__)
#error "r24w_cpuid_check is a GCC/Clang host tool (compares against __builtin_cpu_supports)"
#endif

int main() {
    __builtin_cpu_init();
    const bool builtinAvx2 = __builtin_cpu_supports("avx2") != 0;
    const bool builtinFma = __builtin_cpu_supports("fma") != 0;

    // octavox::simd::detect() here is the EXPLICIT path: this target compiles
    // src/simd_dispatch.cpp with -DOCTAVOX_FORCE_EXPLICIT_CPUID (see the
    // native CMakeLists.txt), i.e. the exact algorithm the Windows builds use.
    const octavox::simd::Capabilities caps = octavox::simd::detect();

    std::printf("r24w_cpuid_check\n");
    std::printf("  explicit (Windows algorithm): avx2=%d fma=%d\n",
                (int)caps.avx2, (int)caps.fma);
    std::printf("  builtin  (Linux R24 path)   : avx2=%d fma=%d\n",
                (int)builtinAvx2, (int)builtinFma);
    const bool ok = caps.avx2 == builtinAvx2 && caps.fma == builtinFma;
    std::printf("  verdict: %s\n", ok ? "AGREE" : "DISAGREE");
    return ok ? 0 : 1;
}
