// OCTAVOX native core -- R26 SHARED INTERPOLATION TABLES (header)
// ============================================================================
// The 6-tap interpolation weight tables and the read-mode codes, shared by
// the baseline read implementation (src/dsp.cpp), the scalar kernel
// reference (src/simd_dispatch.cpp) and the AVX2 read kernel
// (src/simd_avx2.cpp). Previously file-local to dsp.cpp (R12..R24); factored
// out in R26 so the ISA units can read the SAME table objects (single
// definition, in src/simd_dispatch.cpp -- a baseline-ISA translation unit,
// so the table constructor runs with the same flags as before and produces
// IDENTICAL values; construction is one-time at first use, thread-safe via
// the magic-static guard).
//
// MODE CODES: the Interpolation enum's underlying values (dsp.hpp):
//   0 Linear, 1 Lagrange6 (2048 phases), 2 Lagrange6Q (256 phases),
//   3 ObsKernel6 (the target's observed .rdata kernel, 256 phases).
// dsp.cpp static_asserts the correspondence.
// ---------------------------------------------------------------------------
#pragma once

namespace octavox {
namespace interp {

/// Interpolation mode codes (the Interpolation enum's underlying values).
enum Mode : int {
    kLinear = 0,
    kLagrange6 = 1,
    kLagrange6Q = 2,
    kObsKernel6 = 3
};

/// The Lagrange weight tables (R12 law: bin-centre phases, 6 taps).
struct Tables {
    double lag2048[2048][6];
    double lag256[256][6];
};

/// The shared table instance (defined in src/simd_dispatch.cpp). Values are
/// IDENTICAL to the R12-R24 file-local tables (the construction code was
/// moved verbatim): bit-exactness of every recorded hash is unaffected.
const Tables& tables();

}  // namespace interp
}  // namespace octavox
