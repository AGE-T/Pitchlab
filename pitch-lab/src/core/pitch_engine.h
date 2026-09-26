#pragma once

// Pitch Lab — PitchEngine contract (implementation specification §4.2;
// frozen implementation behaviour §4.2.1, recorded 2026-09-26 BEFORE coding,
// cycle 3 — mirrors the §7.2.1/§4.4.3.1 pattern).
//
// CONTRACT (v0.1, frozen):
//   * Engines are independently reusable plain C++ (architecture §D.1):
//     they know NOTHING about the harness, analyser, reporter, corpus
//     generator, web workbench, file paths or test methodology. The ONLY
//     dependencies of an engine TU are core headers (T-A2 scans this).
//   * Lifecycle: configure() (params + seed) -> prepare() (allocate + reset
//     state; ALL allocation happens here — process()/finish() never
//     allocate, §5 rule 1) -> process() blocks -> finish() exactly once
//     after all real input has been delivered -> destroy. reset() clears
//     DSP state, keeps configuration (§5 rule 3).
//   * Block exchange (§4.3.2): the renderer supplies up to `inFrames` input
//     frames at absolute input-timeline position `inputFrameIndex` and
//     output capacity `outCapacity`; the engine consumes/produces what it
//     can and returns the accounting report. Any block boundary must be
//     tolerated (block-streaming mandate).
//   * Exceptions: engines throw ONLY EngineException (§4.2); anything else
//     escaping an engine is a JOB FAILURE at harness level.
//   * No hidden global state; no per-call heap allocation in the processing
//     path (§4.2 preconditions; audited by T-A1's test-build allocation
//     counter).
//   * Buffers: `in` and `out` are NON-OWNING views/regions owned by the
//     renderer, never aliasing, never outliving the call (§5).
//
// §4.2.1 implementation clarifications recorded here:
//   * ProcessContext carries the EFFECTIVE curve view (so prepare() can
//     compute ratio-dependent latency bounds per §4.3.5); the same view is
//     handed to every process() call.
//   * EngineConfiguration is a sorted typed parameter bag + REQUIRED seed.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/types.h"

namespace pitchlab {

struct EngineConfiguration;  // defined below (§4.5 typed parameter bag)

// ---------------------------------------------------------------------------
// §4.2 contract types
// ---------------------------------------------------------------------------

struct ProcessContext {
  SampleRate sampleRate = 0.0;        // job sample rate (Hz)
  ChannelCount channels = 0;          // job channel count
  int maxBlockFrames = 0;             // harness block size (config/harness.toml, L-3: 4096)
  FrameCount totalInputFrames = 0;    // real input frames (padding excluded)
  const struct PitchCurveView* curve = nullptr;  // EFFECTIVE curve (§4.4.6);
                                      // renderer-owned storage, valid for the
                                      // whole job; same object every process()
};

/// Dense, INPUT-timeline-indexed ratio signal (architecture §D.4.2).
/// `ratio[f]` is the pitch ratio for input frame f; strictly positive and
/// finite (post-compilation validation §4.4.3.1 item 12). PerSample engines
/// index it at floor(read position) clamped to [0, frames-1] (§6.1.1 item 2;
/// clamp events are recorded by the renderer's expectation replay, §4.4.5).
struct PitchCurveView {
  const double* ratio = nullptr;
  FrameCount frames = 0;   // == totalInputFrames
  SampleRate sampleRate = 0.0;
};

/// Per-call block-exchange accounting (§D.4.1 items 1-3).
struct ProcessReport {
  FrameCount inputFramesConsumed = 0;
  FrameCount outputFramesProduced = 0;
  bool inputExhausted = false;  // sticky: set when cumulative consumption
                                // reaches the full padded stream (§4.2.1
                                // item 7); reported as-is thereafter
};

/// §4.5 typed parameter value (engine-defined semantics; harness validates
/// only key names + finiteness). §4.2.1 item 2.
using ParameterValue = std::variant<std::string, double, bool, int64_t>;

/// §4.5 engine configuration: typed parameter bag (sorted by key) + REQUIRED
/// seed. Serialised verbatim (canonical JSON, sorted keys) into the manifest
/// by the harness; the seed participates in per-consumer RNG derivation for
/// SeededDeterministic engines (§4.7) and is recorded for ALL engines.
struct EngineConfiguration {
  std::vector<std::pair<std::string, ParameterValue>> parameters;  // sorted by key
  uint64_t seed = 0;  // REQUIRED present (harness-validated)

  const ParameterValue* find(const std::string& key) const {
    for (const auto& kv : parameters) {
      if (kv.first == key) return &kv.second;
    }
    return nullptr;
  }
};

// ---------------------------------------------------------------------------
// §4.2 PitchEngine interface
// ---------------------------------------------------------------------------

class PitchEngine {
 public:
  virtual ~PitchEngine() = default;

  /// Stable registry identity of this instance's engine (mirrors the
  /// EngineInfo.id of the descriptor that constructed it).
  virtual const char* engineId() const = 0;

  /// Parameter validation (values only; key-set validation is harness-side
  /// against the registry descriptor, §4.5). Called BEFORE prepare().
  /// Throws ConfigError on non-finite/invalid values (§6.1 failure rows).
  virtual void configure(const EngineConfiguration& cfg) = 0;

  /// Allocate all state and reset to job start. ALL allocation happens here
  /// (§5 rule 1). `ctx.curve` is the effective curve (§4.2.1 item 3).
  /// Throws ConfigError on unsupported (fs, channels, maxBlock) etc.
  virtual void prepare(const ProcessContext& ctx) = 0;

  /// Declared latencies (valid after prepare(); constant per job).
  /// inputLatencyFrames: renderer zero-padding lookahead (§4.3.6).
  /// outputLatencyFrames: bounded flush length (§4.3.5).
  struct Latency {
    FrameCount inputLatencyFrames = 0;
    FrameCount outputLatencyFrames = 0;
  };
  virtual Latency latency() const = 0;

  /// Block exchange (§4.3.2). Preconditions: inFrames <= ctx.maxBlockFrames,
  /// strictly increasing inputFrameIndex == cumulative consumed frames,
  /// exactly once per input frame range, in order; outCapacity >= 0; in/out
  /// never alias. Returns the accounting report. Must not allocate (§5).
  virtual ProcessReport process(const AudioBlockView& in, int inFrames,
                                AudioBlockOut& out, int outCapacity,
                                const PitchCurveView& curve,
                                FrameCount inputFrameIndex) = 0;

  /// End-of-input + bounded flush (§D.4.1 items 3-4). Called EXACTLY once,
  /// after the renderer has delivered all real input (and the §4.3.6
  /// padding). Emits up to outCapacity frames; total flush output MUST be
  /// <= latency().outputLatencyFrames (else end-of-render-violation).
  /// Must not allocate (§5).
  virtual ProcessReport finish(AudioBlockOut& out, int outCapacity) = 0;

  /// Clears DSP state, keeps configuration (§5 rule 3). Reusable for a job
  /// with identical (fs, channels, maxBlock, N_in); after reset, processing
  /// the same input with the same curve must produce bit-identical output
  /// to a fresh instance (tested, T-D3).
  virtual void reset() = 0;
};

/// Factory signature used by the registry (§4.6/§4.2.1 item 5). Production
/// registration binds exactly one factory per engine descriptor; the factory
/// has NO arguments (all job-specific state arrives via configure/prepare).
using EngineFactory = std::unique_ptr<PitchEngine> (*)();

}  // namespace pitchlab
