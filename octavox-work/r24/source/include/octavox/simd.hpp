// OCTAVOX native core -- R24 SIMD CAPABILITY LAYER (public API)
// ============================================================================
// PROVENANCE: our own implementation (round 24). The layer provides:
//   * the Config-facing SIMD mode enum (Config::simd, default Scalar),
//   * one-time, cached CPU feature detection (AVX2 and FMA detected
//     INDEPENDENTLY via CPUID),
//   * the YIN difference-function / window-peak kernels in three
//     implementations: scalar (baseline TU, every supported processor),
//     AVX2 (TU compiled with -mavx2 only) and AVX2+FMA (TU compiled with
//     -mavx2 -mfma), and
//   * the dispatch table resolved OUTSIDE the realtime processing loop.
//
// RT-SAFETY CONTRACT (the acceptance rules of the R24 brief):
//   * Every kernel is a pure function over caller-owned buffers: no
//     allocation, no locks, no CPUID, no blocking operation, reentrant.
//   * The kernels are selected (simd::kernels(level)) in
//     PitchDetector::configure(), i.e. at prepare() time -- NEVER inside
//     process(). The audio callback only performs an indirect call through
//     a function pointer stored at configuration time.
//   * The production DSP library itself is compiled with the baseline ISA
//     (x86-64 SSE2); the AVX2 / AVX2+FMA kernels live in separate
//     translation units with their own ISA flags (see CMakeLists.txt),
//     so the library loads and runs on every x86-64 processor. A CPU
//     without the required feature never executes an ISA-extended
//     instruction (the dispatch table only ever hands out the scalar
//     entry points there).
//
// NUMERICAL SEMANTICS OF THE DIFFERENCE KERNELS (R24 audit, OBS from the
// disassembly of the R23 release build): the historical scalar reference
//   d[tau] = sum_{j=0..m-1} (x[seg0+j] - x[seg0+tau+j])^2
// is compiled by GCC -O2 as 2-lane SSE2 with an ORDER-PRESERVING
// reduction: the per-term squaring is vectorised (mulpd) but the
// accumulation stays strictly sequential in j (addsd x2). Per term the
// reference therefore performs one multiplication rounding and one
// addition rounding, summed in ascending j order. All kernels in this
// layer are specified relative to that reference:
//
//   kernel               summation order        per-term rounding
//   -------------------  ---------------------  ---------------------------
//   scalar (reference)  sequential j           mul+add (2 roundings)
//   lag-axis  AVX2      sequential j (per tau) mul+add (2 roundings)
//                       => BIT-IDENTICAL to the reference by construction
//                          (each lane reproduces the exact reference op
//                          sequence for its tau; verified empirically).
//   lag-axis  AVX2+FMA  sequential j (per tau) fused (1 rounding) => the
//                       ONLY deviation is per-term fma rounding; order
//                       preserved.
//   sample-axis AVX2    4-lane reassociated    mul+add (2 roundings)
//   sample-axis AVX2+FMA 4-lane reassociated   fused (1 rounding)
//
// The horizontal reduction of the sample-axis kernels is
//   d = ((P0 + P2) + (P1 + P3)) + s_tail
// with P0..P3 the lane accumulators (fixed, documented association) and
// s_tail the sequential scalar remainder (m mod 4 terms).
//
// The peak kernel computes max |x[i]|; max is exact and order-free, so the
// SIMD peak is bit-identical to the scalar peak at every level.
// ---------------------------------------------------------------------------
#pragma once

namespace octavox {
namespace simd {

/// Config-facing SIMD mode (Config::simd). Default = Scalar: the engine is
/// bit-exact against every recorded reference hash (the R12--R23 record).
/// The accelerated levels are OPT-IN and are validated by the R24 numerical
/// battery before use; the historical reference implementation is preserved
/// verbatim as the Scalar level.
enum class Mode : int {
    Scalar = 0,   ///< baseline implementation (bit-exact historical reference)
    AVX2 = 1,     ///< AVX2 kernels (lag-axis: bit-exact by construction)
    AVX2FMA = 2,  ///< AVX2 + FMA kernels (fused per-term rounding)
    Auto = 3      ///< best available: AVX2+FMA > AVX2 > Scalar
};

/// Resolved implementation level (never Auto). Returned by resolve().
enum class Level : int {
    Scalar = 0,
    AVX2 = 1,
    AVX2FMA = 2
};

/// CPU feature report (each feature detected INDEPENDENTLY).
struct Capabilities {
    bool avx2;
    bool fma;
};

/// One-time CPUID-based feature detection. Cached after the first call
/// (function-local static -- thread-safe, no allocation after init).
/// Call outside the audio callback (prepare() does); the result is a POD.
Capabilities detect();

/// True iff this build contains the ISA-extended kernel units (always true
/// on the supported x86-64 toolchains; false on architectures without the
/// units, where every non-Scalar mode is rejected at prepare()).
bool isaUnitsCompiled();

/// Mode name for diagnostics ("scalar" / "avx2" / "avx2+fma" / "auto").
const char* modeName(Mode m);

/// Level name for diagnostics ("scalar" / "avx2" / "avx2+fma").
const char* levelName(Level l);

/// Resolve a Mode against the detected capabilities and the build. Never
/// fails and never upgrades past the available features:
///   Scalar  -> Scalar
///   AVX2    -> AVX2 if (units && caps.avx2)            else Scalar
///   AVX2FMA -> AVX2FMA if (units && caps.avx2 && caps.fma) else Scalar
///   Auto    -> AVX2FMA / AVX2 / Scalar (best available)
/// prepare() uses the RESOLVED level for the engine and REJECTS the
/// configuration when an explicitly forced mode (AVX2 / AVX2FMA) cannot be
/// honoured (loud failure, the house validation rule); only Auto degrades
/// silently.
Level resolve(Mode m);

// ---------------------------------------------------------------------------
// Kernels. All operate on the sliding-analysis-window layout of the YIN
// detector (see src/dsp.cpp PitchDetector::estimate):
//   d[i] = sum_{j=0..m-1} (x[seg0 + j] - x[seg0 + tau0 + i + j])^2
///     for i in [0, ntau); caller guarantees
///     seg0 + tau0 + ntau - 1 + m <= n (the window law).
// No alignment is required of x or d (unaligned loads/stores throughout);
// buffers are caller-owned and never written beyond ntau results.
// ---------------------------------------------------------------------------

/// Difference-function kernel signature (see block comment above).
using DiffFn = void (*)(const double* x, int seg0, int tau0, int ntau, int m,
                        double* d);

/// Window peak kernel: returns max |x[i]| for i in [0, n) (exact, order-free).
using PeakFn = double (*)(const double* x, int n);

/// The dispatch table for one level. `diff` is the PRODUCTION kernel choice
/// for the level (see the numerical-semantics table above; the lag-axis
/// kernels are the order-preserving choices). `diff_sample` and `diff_lag`
/// expose both vectorisation axes for the benchmark harness; the detector
/// calls `diff`.
struct Kernels {
    Level level;
    DiffFn diff;         ///< production choice for this level
    DiffFn diff_sample;  ///< sample-axis (j-reassociating) variant
    DiffFn diff_lag;     ///< lag-axis (order-preserving) variant
    PeakFn peak;
};

/// The kernel table for a level. The Scalar table is available in every
/// build; AVX2 / AVX2FMA tables exist only when isaUnitsCompiled() (the
/// function pointers are resolved at first use from the baseline TU -- no
/// static initialisation order hazard across the ISA units).
Kernels kernels(Level l);

}  // namespace simd
}  // namespace octavox
