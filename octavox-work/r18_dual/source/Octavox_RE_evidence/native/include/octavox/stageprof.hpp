// OCTAVOX native core -- R19 STAGE ATTRIBUTION PROFILER (public API)
// ============================================================================
// PROVENANCE: our own instrumentation (round 19). Pure observer: the
// collection never touches the DSP data flow.
//
// The core is compiled with -DOCTAVOX_STAGE_TIMERS to accumulate per-stage
// cycle counters on the processing path. With the macro OFF (the default and
// every shipped plugin build) the collection functions return zeros and the
// call sites are compiled out entirely -- the plain build is bit-identical to
// a source tree that never contained the instrumentation.
//
// Stages (see src/dsp.cpp for the exact scoped regions):
//   0 det_buffering   PitchDetector::pushBlock / pushDecimated (window feed,
//                     incl. the decimated mode's anti-alias biquads)
//   1 det_estimate    PitchDetector::estimate (the YIN difference loop)
//   2 det_median      updateSpacingTarget (history + median sort + S law)
//   3 voice_loop      the per-sample voices for-loop (inclusive: contains the
//                     interpolation reads and every voice slew)
//   4 interp_reads    readInterp/readRaw calls (SUBSET of voice_loop)
//   5 output_stage    norm slew + feedback tap + bypass slew + wet/dry mix
//                     + the output write
//   6 io_input        input+feedback read, ring push, dry-ring push
//
// Timer source: __rdtsc on x86_64 (cycle counts; the TSC invariant hypothesis
// holds on every modern x86_64 host), steady_clock otherwise (reported as
// nanosecond-scaled ticks -- non-x86 hosts report ticks, not cycles; the
// sandbox reference machine is x86_64). The counters are plain (non-atomic)
// per-thread accumulators: ONE engine processes on ONE thread at a time
// (the RT contract), so no atomics are needed; MultichannelShifter runs its
// engines sequentially on the calling thread and accumulates into the same
// per-thread counters.
//
// CALIBRATION: each scoped measurement adds the timer-read overhead to its
// stage (two TSC reads per scope). octavox::stageprof::timerOverheadCycles()
// measures the empty-scope cost; the bench subtracts calls * overhead from
// every stage before reporting (the corrected numbers are the honest stage
// costs; the residual of corrected sums vs. the uninstrumented total is
// reported alongside).
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace octavox {
namespace stageprof {

/// Stage ids (fixed order; the names are in stageNames()).
enum Stage : int {
    kDetBuffering = 0,
    kDetEstimate = 1,
    kDetMedian = 2,
    kVoiceLoop = 3,
    kInterpReads = 4,
    kOutputStage = 5,
    kIoInput = 6,
    kStageCount = 7
};

/// True iff the core was compiled with collection active.
bool active();

/// Stage name ("det_buffering" ... "io_input"); index must be in range.
const char* stageName(int stage);

/// Reset all counters (call before the measured region).
void reset();

/// Accumulated timer ticks of a stage (0 when inactive).
uint64_t ticks(int stage);

/// Number of times the stage's scope was entered (0 when inactive).
uint64_t calls(int stage);

/// Measured cost of one empty scoped measurement (the timer-read overhead)
/// in ticks. Measured by a calibration loop on first use.
uint64_t timerOverheadTicks();

}  // namespace stageprof
}  // namespace octavox
