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
//    the seams; a ±1 st pitch envelope per job makes every prepare-time
//    whole-curve scan (s_max / r_max / p_end / horizon) a correct bound
//    for any automation inside the envelope (the curve is clamped to it);
//    automation leaving the envelope triggers a re-prepare (a new chain,
//    crossfaded in — never silent clamping).
//  * native.varispeed is RateFollowing (§3): its output/input frame counts
//    diverge, which cannot be sustained in fixed-block realtime. It runs
//    under the WINDOWED-SPLICE adaptation (§4.2): real varispeed jobs over
//    overlapping input windows, duration-preserving window advance, 15 ms
//    splice crossfades, fixed worst-case latency. This is a declared
//    adaptation, labelled in the status — NOT the offline render.
//
// REALTIME SAFETY (§4.3): the audio path allocates nothing (all buffers are
// pre-allocated at activation or on the preparation thread), takes no
// locks, performs no I/O. Engine instances are created/configured/prepared
// and destroyed ONLY on the preparation thread; the audio thread adopts a
// finished chain by an atomic pointer swap at a block boundary and retires
// the old chain through a lock-free retire stack. Engine exceptions are
// caught at the chain boundary (fallback to latency-compensated dry +
// counted fault; never a crash).
//
// DETERMINISM: identical (input, parameter trajectory, block schedule)
// yields bit-identical output (tested); this is the realtime guarantee —
// it is deliberately NOT the offline renderer's stronger whole-curve
// guarantee (spec §3, honest boundary).

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/pitch_engine.h"
#include "vst/parameters.h"
#include "vst/seqlock.h"

namespace pitchlab::vst {

// ---------------------------------------------------------------------------
// Public snapshots (read by the processor / UI; written by the adapter)
// ---------------------------------------------------------------------------

/// Per-channel meter values (peak with hold-decay + RMS), published per
/// block through a seqlock.
struct MetersSnapshot {
  double inPeak[2] = {0.0, 0.0};
  double inRms[2] = {0.0, 0.0};
  double outPeak[2] = {0.0, 0.0};
  double outRms[2] = {0.0, 0.0};
  double outClip[2] = {0.0, 0.0};  // clip indicator decay (1 -> recent clip)
  int channels = 2;
};

/// Runtime status (real values only — no quality score, no fake DSP
/// indicators; spec §7).
struct StatusSnapshot {
  char engineId[48] = "";
  char engineName[64] = "";
  char adaptation[64] = "";  // e.g. "continuous realtime" / "windowed splice"
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
  bool bypassActive = false;
  bool chainReady = false;
};

// ---------------------------------------------------------------------------
// Per-block automation (VST3 IParameterChanges, resolved by the processor)
// ---------------------------------------------------------------------------

struct AutomationPoint {
  int32_t frameOffset = 0;
  double value = 0.0;  // PLAIN value (st / 0..1 / dB)
};

struct BlockAutomation {
  static constexpr int kMaxPoints = 32;
  AutomationPoint pitch[kMaxPoints];
  int pitchCount = 0;
  AutomationPoint mix[kMaxPoints];
  int mixCount = 0;
  AutomationPoint level[kMaxPoints];
  int levelCount = 0;
  int32_t bypassFrame = -1;  // -1 = no event this block
  bool bypassValue = false;
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

  /// Allocate rings for the stream format, start the preparation thread and
  /// request the first chain. Idempotent per format (a format change
  /// requires deactivate() first — the processor does this from
  /// setProcessing(false), the VST3 contract for setup changes).
  void activate(double sampleRate, int channels, int maxBlockFrames);

  /// Stop the preparation thread and drop every chain (audio stopped).
  void deactivate();

  /// Publish the parameter snapshot (main thread: UI setParameter, state
  /// restore, presets). Chain-affecting changes are picked up by the
  /// preparation thread and rebuilt with a crossfaded swap.
  void setParameterSnapshot(const ParamSnapshot& snapshot);

  /// Force a chain rebuild at the next block (used after state restore /
  /// reset so the new parameters apply immediately).
  void requestHardReset();

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
