#pragma once

// Pitch Lab VST3 product layer — THE REALTIME ADAPTER
// (product-phase specification §3/§4 — the frozen boundary design).
//
// The v0.1 engine contract is a FINITE-JOB, KNOWN-CURVE contract (§3 of the
// product-phase spec); a VST3 process path is an unbounded block stream with
// unknown future automation. This adapter is the smallest correct bridge:
//
//  * PRESERVING engines (vardelay, granular, pv.classic, pv.phaselocked)
//    run as a sequence of virtual jobs (§4.1): long segments (N_seg),
//    overlapping by the engine's own transition length X, crossfaded at
//    the seams; a pitch envelope per job makes every prepare-time
//    whole-curve scan (s_max / r_max / p_end / horizon) a correct bound
//    for any automation inside the envelope (the curve is clamped to it).
//    THE ENVELOPE RULE (Task 30): ±max(1 st, live LFO depth) around the
//    live ratio, the parameter-domain bounds relaxed by the LFO excursion,
//    and the ENGINE's declared ratio range as the hard clamp — a static
//    PITCH+LFO setting never clamps (spec §4.1 item 2). Lifecycle: a
//    re-prepare fires ONLY on a genuine dependency change — engine/
//    configuration change (the chain signature), pitch automation leaving
//    the envelope (the re-centre), the LFO depth growing beyond the
//    chain's envelope margin (the capability re-size; ONE rebuild, never
//    per block), or reset/format events. A runtime clamp is a COUNTED
//    boundary event (an engine's declared limit or a transient exit
//    bridge), never a re-prepare trigger — the pre-Task-30 clamp→epoch
//    coupling was the measured rebuild-churn root cause (759 clamps /
//    329 re-prepares per 2 s at −12 st + LFO) and is removed.
//  * native.varispeed is RateFollowing (§3): its output/input frame counts
//    diverge, which cannot be sustained in fixed-block realtime. It runs
//    under the WINDOWED-SPLICE adaptation (§4.2): real varispeed jobs over
//    overlapping input windows, duration-preserving window advance, 15 ms
//    splice crossfades, fixed worst-case latency. This is a declared
//    adaptation, labelled in the status — NOT the offline render.
//
// REALTIME SAFETY (§4.3): the audio path allocates nothing (all buffers are
// pre-allocated at activation or on the preparation thread), takes no
// locks, performs no I/O, never sleeps and never formats strings. Engine
// instances are created/configured/prepared and destroyed ONLY on the
// preparation thread; the audio thread adopts a finished chain by an atomic
// pointer swap at a block boundary and retires the old chain through a
// lock-free retire stack. Engine exceptions are caught at the chain
// boundary (fallback to latency-compensated dry + counted fault; never a
// crash).
//
// STARTUP (the Task 24 fix for the audio-thread wait): the FIRST chain of
// an activation is materialised BEFORE the first audio block can run — the
// main thread waits (bounded, on the main thread where blocking is legal)
// in activate() when a snapshot is already published, and in
// setParameterSnapshot() when the activation came first. The audio thread
// never waits: if the (pathological) bound is exceeded it emits the
// latency-compensated dry path and the chain adopts mid-stream through the
// normal machinery.
//
// DETERMINISM: identical (input, parameter trajectory, block schedule)
// yields bit-identical output (tested); this is the realtime guarantee —
// it is deliberately NOT the offline renderer's stronger whole-curve
// guarantee (spec §3, honest boundary).

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/pitch_engine.h"
#include "vst/parameters.h"
#include "vst/seqlock.h"

namespace pitchlab::vst {

// ---------------------------------------------------------------------------
// Public snapshots (read by the processor / UI; written by the adapter)
//
// COORDINATE/DOMAIN NOTE (Task 24, P0.11/P1.11): the status snapshot
// carries only STABLE IDENTIFIERS and numeric fields — no human-readable
// strings. The audio thread must not format strings (spec §4.3); the
// consumers (UI / tools) map engineIndex through the registry.
// ---------------------------------------------------------------------------

/// Per-channel meter values (peak with time-based hold-decay + RMS),
/// published per block through a seqlock. Decay is per-SAMPLE (time-based,
/// block-size independent — the Task 24 fix for per-block decay factors).
struct MetersSnapshot {
  double inPeak[2] = {0.0, 0.0};
  double inRms[2] = {0.0, 0.0};
  double outPeak[2] = {0.0, 0.0};
  double outRms[2] = {0.0, 0.0};
  double inClip[2] = {0.0, 0.0};   // input clip indicator (1 -> recent clip)
  double outClip[2] = {0.0, 0.0};  // output clip indicator (1 -> recent clip)
  int channels = 2;
};

/// Runtime status (real values only — no quality score, no fake DSP
/// indicators; spec §7). Strings are derived by the CONSUMERS from
/// engineIndex/spliceMode (registry lookup) — the audio thread stores
/// numbers only.
struct StatusSnapshot {
  int engineIndex = -1;    // registry index of the active chain (-1 = none)
  bool spliceMode = false; // windowed-splice adaptation (varispeed/granular)
  double sampleRate = 0.0;
  int channels = 0;
  int maxBlock = 0;
  int64_t latencyFrames = 0;
  double envelopeMin = 1.0;
  double envelopeMax = 1.0;
  int liveJobs = 0;
  uint64_t reprepares = 0;
  uint64_t faults = 0;
  uint64_t clampEvents = 0;
  uint64_t automationDropped = 0;  // automation points beyond capacity (never silent)
  bool bypassActive = false;
  bool chainReady = false;

  // --- Task 29: categorized fault diagnostics --------------------------------
  //
  // The aggregate `faults` KEEPS ITS HISTORICAL COMPOSITION (compatibility):
  // faults == engineProcessFaults + engineExceptions + deliveryUnderruns +
  // dryHistoryMisses — exactly the classes the pre-Task-29 counter summed.
  // The NEW categories (jobStalls, chainAdoptionFailures, preparationFailures)
  // are diagnostics that identify root classes WITHOUT retroactively changing
  // what `faults` means. NUMERIC ONLY (the audio thread never formats strings
  // — spec §4.3); the UI/tools format them.
  //
  // Audio-path fault categories (sum into `faults`):
  uint64_t engineProcessFaults = 0;  // invalid ProcessReport (bad counts)
  uint64_t engineExceptions = 0;     // exceptions thrown by engines
  uint64_t deliveryUnderruns = 0;    // wet lane miss INSIDE the covered range
  uint64_t dryHistoryMisses = 0;     // job input fell outside dry retention
  // New diagnostic categories (NOT in the aggregate — compatibility):
  uint64_t jobStalls = 0;            // scheduled jobs overdue-unfinished
  uint64_t chainAdoptionFailures = 0;  // forced-deadline adoptions + drops
  // Preparation-side (prep thread; never block audio):
  uint64_t preparationFailures = 0;  // prepare/reset/buildChain failures (retry loop)
  // Cadence + workload (diagnostics for the 96 kHz investigations):
  uint64_t chainsAdopted = 0;     // successful chain adoptions
  uint64_t jobsPrepared = 0;      // total prepared (scheduled) jobs
  uint64_t processCalls = 0;      // process() invocations
  int preparingJobs = 0;          // prepared-ahead job count (live snapshot)
  // Task 31 — the seam-coverage diagnostic (a CADENCE figure like
  // chainsAdopted, NOT a fault — the aggregate `faults` composition is
  // unchanged): frames served from the RETAINED WET HISTORY (the
  // retiring-chain re-coverage source; see the Chain::history note in
  // realtime_adapter.cpp). A nonzero count during a latency-increasing
  // chain replacement is the retention working as specified (spec §4.1
  // item 5: the lanes retain the parameter-range worst-case history "so a
  // mid-stream Λ_eff growth never outruns the retained wet/dry"); the
  // pre-Task-31 adapter counted these frames as delivery underruns +
  // dry-fallback bursts because job recycling had destroyed the data.
  uint64_t seamRecoveries = 0;  // frames served by the retained wet history
};

// ---------------------------------------------------------------------------
// Per-block automation (VST3 IParameterChanges, resolved by the processor)
//
// AUTHORITATIVE COORDINATE SYSTEM (Task 24, the P0.4 fix): VST3 host event
// offsets are ProcessData-block-relative. PitchLabProcessor converts each
// event EXACTLY ONCE, into the coordinate system of the adapter call that
// owns it (processor-chunk-local): an event belongs to the chunk containing
// its offset and is rebased to that chunk's first frame; events are never
// clamped across chunk boundaries. Inside this adapter, ALL automation
// offsets are therefore relative to the CURRENT process() call's first
// frame, and the internal sub-chunk loop stays in the same domain
// (subStart + i).
//
// CAPACITY (Task 24, the P1.3 fix): a documented bounded capacity per
// parameter per block, with a consolidation rule for overflow (keep the
// first capacity-1 points + ALWAYS the final point — the block-end value
// the next block's carry depends on) and a counted, status-published
// `droppedPoints` diagnostic. Never silent, never an allocation.
// ---------------------------------------------------------------------------

struct AutomationPoint {
  int32_t frameOffset = 0;
  double value = 0.0;  // PLAIN value (st / 0..1 / dB / bool as 0|1)
};

struct BlockAutomation {
  static constexpr int kMaxPoints = 256;  // bounded, allocation-free capacity
  AutomationPoint pitch[kMaxPoints];
  int pitchCount = 0;
  AutomationPoint mix[kMaxPoints];
  int mixCount = 0;
  AutomationPoint level[kMaxPoints];
  int levelCount = 0;
  AutomationPoint lfoRate[kMaxPoints];   // Hz (plain)
  int lfoRateCount = 0;
  AutomationPoint lfoDepth[kMaxPoints];  // st (plain)
  int lfoDepthCount = 0;
  AutomationPoint bypass[kMaxPoints];    // 0|1 (plain); step semantics
  int bypassCount = 0;
  uint32_t droppedPoints = 0;  // points beyond capacity (consolidated, counted)
};

// ---------------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------------

class RealtimeAdapter final {
 public:
  RealtimeAdapter();
  ~RealtimeAdapter();

  RealtimeAdapter(const RealtimeAdapter&) = delete;
  RealtimeAdapter& operator=(const RealtimeAdapter&) = delete;

  // ---- main-thread setup (audio processing stopped; may allocate) --------
  //
  // NOTE: activate() and the FIRST setParameterSnapshot() after it (in
  // either order) block on the MAIN thread (bounded) until the first chain
  // is published — the non-blocking-startup architecture (the audio thread
  // never waits).

  /// Allocate rings for the stream format, start the preparation thread and
  /// request the first chain. Idempotent per format (a format change
  /// requires deactivate() first — the processor does this from
  /// setActive(false), the VST3 contract for setup changes).
  void activate(double sampleRate, int channels, int maxBlockFrames);

  /// Stop the preparation thread and drop every chain (audio stopped).
  void deactivate();

  /// Publish the parameter snapshot (main thread: UI setParameter, state
  /// restore, presets). Chain-affecting changes are picked up by the
  /// preparation thread and rebuilt with a crossfaded swap. If the adapter
  /// is active and NO chain exists yet, this waits (bounded, main thread)
  /// for the first chain so the first audio block starts wet-deterministic.
  void setParameterSnapshot(const ParamSnapshot& snapshot);

  /// Force a chain rebuild at the next block (used after state restore /
  /// reset so the new parameters apply immediately).
  void requestHardReset();

  /// Suspend/resume support (spec §8: "resume = reset + fresh chain").
  /// Audio-thread-safe (atomic flag only): the next process() block retires
  /// the active chain (through the standard crossfade retirement) and
  /// resets the streaming musical state; the preparation thread builds the
  /// fresh chain and the normal adoption machinery swaps it in. No DSP
  /// state from before the suspend survives past the standard seam blend.
  void requestProcessingReset();

  // ---- audio thread -------------------------------------------------------

  /// Process one host block (planar, non-interleaved, double; `in` and `out`
  /// never alias). `in` is [channels][frames]; `out` is [channels][frames].
  void process(const double* const* in, double* const* out, int32_t frames,
               const BlockAutomation& automation);

  // ---- any thread (UI / status) -------------------------------------------

  [[nodiscard]] MetersSnapshot meters() const { return meters_.load(); }
  [[nodiscard]] StatusSnapshot status() const { return status_.load(); }

 private:
  struct Chain;
  struct Job;
  struct Impl;

  // internal helpers (defined in the .cpp; audio-thread only)
  void feedJob(const Chain& chain, Job& job, int64_t t, int32_t subN,
               const double* const* in, int64_t feedLimit, int64_t historyFloor,
               bool& clampDetected);
  void finishJob(Job& job, int64_t historyFloor);

  std::unique_ptr<Impl> impl_;

  // accessed by Impl (defined in the .cpp)
  SeqLock<MetersSnapshot> meters_;
  SeqLock<StatusSnapshot> status_;
};

/// The latency (frames) the adapter will declare for the given snapshot at
/// the given sample rate — computable synchronously (the host sees the
/// correct latency from the first setup, before the first chain is built).
/// Mirrors the per-engine geometry the adapter itself uses (ONE definition,
/// realtime_adapter.cpp).
[[nodiscard]] int64_t expectedLatencyFrames(const ParamSnapshot& snapshot, double sampleRate);

}  // namespace pitchlab::vst
