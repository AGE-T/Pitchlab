#pragma once

// Pitch Lab — Task 28 candidate re-evaluation: the shared prototype
// engine shape + offline/block-streaming driver.
//
// This mirrors the PRODUCTION PitchEngine contract (core/pitch_engine.h)
// — configure -> prepare (ALL allocation) -> process blocks -> finish —
// with three deliberate research simplifications, recorded here so no
// silent divergence is claimed:
//   1. the pitch curve is handed over at prepare() and stays valid for
//      the whole job (same as the production EFFECTIVE-curve view);
//   2. the output timeline is the input timeline (duration-preserving
//      design; the rate-following varispeed semantics are NOT prototyped
//      here — the existing engines already cover that lane);
//   3. reset()/exception vocabulary are minimal (ConfigError only);
//      determinism is verified by fresh-instance double-runs instead.
//
// The driver reproduces the offline renderer's essential loop (input
// lookahead zero-padding, strictly-increasing cumulative consumption,
// exactly-one finish with bounded flush) so the SAME implementation is
// exercised block-streamed (RT probe) and in one shot (offline matrix),
// and block-split invariance is a measurable property.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/pitch_engine.h"
#include "core/types.h"
#include "harness/json_writer.h"

#include <vector>

namespace pitchlab::proto {

using ProtoParams = std::map<std::string, pitchlab::ParameterValue>;

struct ProcessOutcome {
  FrameCount inputFramesConsumed = 0;
  FrameCount outputFramesProduced = 0;
};

class ProtoEngine {
 public:
  virtual ~ProtoEngine() = default;

  [[nodiscard]] virtual const char* engineId() const = 0;

  /// Optional OFFLINE ANALYSIS PRE-PASS (research-only contract
  /// extension, called by the driver between configure() and prepare()
  /// with the FULL input signal). Engines whose analysis stage is
  /// whole-signal (TD-PSOLA's pYIN pitch marks) use it; the default is a
  /// no-op. The realtime-equivalent cost of this stage (a streaming
  /// tracker with window+hop lookahead) is reported by the RT probe as
  /// analysis-side evidence — the synthesis stage itself stays strictly
  /// block-streamed and allocation-audited.
  virtual void analyzeSignal(const std::vector<std::vector<double>>& input) {
    (void)input;
  }

  /// Parameter validation (throws ConfigError). Called before prepare().
  virtual void configure(const ProtoParams& params) = 0;

  /// Allocate all state (the RT probe audits: ZERO allocation after this
  /// point). `ratioCurve` has `totalInputFrames` entries, input-timeline
  /// indexed, and stays valid for the whole job.
  virtual void prepare(double fs, int channels, int maxBlockFrames,
                       FrameCount totalInputFrames,
                       const double* ratioCurve) = 0;

  struct Latency {
    FrameCount inputLookahead = 0;  // driver zero-pad beyond real input
    FrameCount outputFlush = 0;     // upper bound on finish() output
  };
  [[nodiscard]] virtual Latency latency() const = 0;

  /// Consume up to inFrames at ABSOLUTE input position inputFrameIndex
  /// (cumulative consumed must be strictly increasing and exactly once per
  /// frame range); produce up to outCapacity frames at the absolute
  /// output position = cumulative produced (output timeline == input
  /// timeline by design note 2).
  [[nodiscard]] virtual ProcessOutcome process(const AudioBlockView& in,
                                               int inFrames,
                                               AudioBlockOut& out,
                                               int outCapacity,
                                               FrameCount inputFrameIndex) = 0;

  /// End-of-input flush (exactly once; bounded by outputFlush).
  [[nodiscard]] virtual ProcessOutcome finish(AudioBlockOut& out,
                                              int outCapacity) = 0;

  /// Engine-internal character diagnostics for the research records
  /// (grain counts, search statistics, mark statistics, reset counts...).
  [[nodiscard]] virtual json::Value diagnostics() const { return json::Value(); }
};

/// Drive a ProtoEngine over a full planar signal in `maxBlockFrames`
/// blocks (the production renderer's essential loop, research shape).
struct DriveResult {
  bool ok = false;
  std::string error;
  std::vector<std::vector<double>> out;  // planar output
  FrameCount frames = 0;
  FrameCount consumed = 0;
  FrameCount flushed = 0;
  int64_t blocks = 0;
  FrameCount maxFlushSlack = 0;  // finish emitted minus declared outputFlush
};
[[nodiscard]] DriveResult driveEngine(ProtoEngine& engine,
                                      const std::vector<std::vector<double>>& in,
                                      const double* ratioCurve,
                                      FrameCount ratioFrames, double fs,
                                      int maxBlockFrames, int outSlack = 4096);

}  // namespace pitchlab::proto
