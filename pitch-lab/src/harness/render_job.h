#pragma once

// Pitch Lab — harness job model (implementation specification §4.8/§4.8.1,
// frozen cycle 3): the ExperimentCompiler's output unit (RenderJob + skips)
// and the OfflineRenderer's output unit (JobResult/RenderSummary).
//
// CONTRACT NOTES:
//   * A RenderJob is FULLY RESOLVED: engine id + validated configuration,
//     input asset (id/path/metadata), compiled REQUESTED curve signal and
//     (creative-saturated) EFFECTIVE curve signal, output settings, block
//     schedule. The renderer executes it without touching any experiment
//     TOML (single compilation pass; no second source of truth).
//   * Curve signals are immutable shared storage: identical (spec, fs,
//     N_in) compilations are cached by the compiler; the effective signal
//     aliases the requested one when unsaturated. Content (and therefore
//     the manifest hash) is unaffected; the renderer never mutates a signal
//     (§5 semantics preserved — destruction happens with the last job that
//     references identical immutable content).
//   * SkipList entries carry the pairing + reason — nothing is silently
//     dropped (§4.8.1 item 3).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/curve.h"
#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

enum class ExperimentKind { Benchmark, Creative };

enum class RenderStatus { Ok, Failed };

/// Block delivery schedule (§4.8.1 item 5): `Constant(n)` — blocks of n
/// frames (the final block is the natural remainder); `Pattern([f1..fk])` —
/// the pattern cycles; final block is the natural remainder. The default
/// comes from config/harness.toml block_frames (owner-locked L-3: 4096).
struct BlockSchedule {
  enum class Kind { Constant, Pattern };
  Kind kind = Kind::Constant;
  std::vector<int64_t> frames;  // Constant: exactly one entry (> 0)

  [[nodiscard]] static BlockSchedule constant(int64_t n) { return BlockSchedule{Kind::Constant, {n}}; }
  [[nodiscard]] static BlockSchedule pattern(std::vector<int64_t> f) {
    return BlockSchedule{Kind::Pattern, std::move(f)};
  }

  /// Largest block the renderer will ever offer with this schedule
  /// (ProcessContext.maxBlockFrames).
  [[nodiscard]] int64_t maxBlock() const {
    int64_t m = 0;
    for (int64_t f : frames) m = (f > m) ? f : m;
    return m;
  }

  /// Block size for block index i (pattern cycled).
  [[nodiscard]] int64_t blockAt(int64_t blockIndex) const {
    if (frames.empty()) return 0;
    if (kind == Kind::Constant) return frames[0];
    return frames[static_cast<std::size_t>(blockIndex) %
                   static_cast<std::size_t>(frames.size())];
  }
};

/// One fully-resolved render job (see file header).
struct RenderJob {
  std::string experimentId;
  ExperimentKind kind = ExperimentKind::Benchmark;
  std::string experimentFile;  // path as authored (manifest provenance)

  std::string engineId;
  EngineConfiguration engineConfig;

  // Input asset (resolved; WAV is authoritative, cross-checked at compile).
  std::string assetId;
  std::filesystem::path inputWavAbs;  // absolute (renderer reads this)
  std::string inputWavRel;            // pitch-lab-root-relative (manifest)

  uint32_t sampleRate = 0;      // job rate == asset rate (compiler-enforced)
  ChannelCount channels = 0;    // == asset channels
  FrameCount inputFrames = 0;   // real input frames (N_in)

  // Curves: REQUESTED (authored) + EFFECTIVE (saturated for creative mode;
  // aliases requested when in range). Hashes land in the manifest.
  PitchCurveSpec requestedSpec;
  std::shared_ptr<const PitchCurveSignal> requestedSignal;
  std::shared_ptr<const PitchCurveSignal> effectiveSignal;  // engine sees this
  bool saturated = false;  // creative + out-of-range ⇒ RANGE_SATURATED

  // Output settings (§15.4 [output]).
  bool wantMaster = true;      // float64 master (L-4)
  bool wantListening = false;  // float32 listening copy

  std::vector<std::string> analysisMetrics;  // recorded; execution = §17 step 5

  BlockSchedule blockSchedule;  // default = harness.toml block_frames

  // Roots (renderer uses; manifest paths recorded root-relative).
  std::filesystem::path pitchlabRoot;  // absolute
  std::filesystem::path outputRoot;    // absolute (<root>/artifacts normally)
};

/// One visible skip (never a silent drop). `reason` is the frozen vocabulary
/// of §4.8.1 item 3: rate-mismatch | channel-mismatch | sample-rate-unsupported
/// | channels-unsupported | ratio-out-of-range.
struct SkipEntry {
  std::string assetId;
  std::string curveId;
  std::string engineId;
  uint32_t sampleRate = 0;
  int channels = 0;
  std::string reason;
  std::string detail;  // human-readable one-liner for reports/CLI
};

/// Per-job renderer accounting + outputs (§4.3/§F.3; manifest data source).
struct JobResult {
  std::string experimentId;
  std::string engineId;
  std::string assetId;
  std::string curveId;  // requested spec id

  RenderStatus status = RenderStatus::Ok;
  std::string failureReason;           // empty when ok (§4.8.1 item 9)
  std::vector<std::string> taints;     // e.g. "length-policy" (§4.3.3/4.3.4)

  // Frame accounting (§4.3): consumed counts REAL input only; padding is
  // recorded separately (§4.3.6).
  FrameCount inputFramesActual = 0;        // N_in
  FrameCount inputFramesConsumed = 0;      // real frames the engine consumed
  FrameCount inputPaddingFrames = 0;       // zero frames delivered + consumed
  int64_t inputExhaustionTransitions = 0;  // T-E9: exactly 1 (§4.2.1 item 7)
  PitchEngine::Latency declaredLatency{};

  FrameCount outputFramesProduced = 0;  // total incl. flush
  FrameCount flushFrames = 0;           // frames emitted by finish()
  FrameCount expectedOutputFrames = 0;  // §4.3.3/§4.3.4 expectation
  int64_t lengthDeltaFrames = 0;        // actual - expected
  bool lengthPolicyTaint = false;
  int64_t lengthToleranceFrames = 0;

  // Curve access report (§4.4.5; modelled by the renderer's expectation
  // replay over the expected timeline — covers the engine's enforced-bounded
  // actual accesses; zeroed when not applicable, e.g. Preserving engines).
  int64_t curveMinIndexRequested = 0;
  int64_t curveMaxIndexRequested = 0;
  int64_t curveClampCount = 0;

  // Outputs (empty paths when not written).
  std::filesystem::path masterWav;
  std::filesystem::path listeningWav;
  std::filesystem::path manifestPath;
  std::string masterSha256;  // hex; empty when no master written
};

struct RenderSummary {
  std::vector<JobResult> results;
  [[nodiscard]] bool allOk() const {
    for (const JobResult& r : results) {
      if (r.status != RenderStatus::Ok) return false;
    }
    return true;
  }
};

/// Harness defaults (config/harness.toml + config/tolerances.toml, §15.1/§15.2).
struct HarnessConfig {
  int64_t blockFrames = 4096;            // owner-locked default (L-3)
  int64_t lengthToleranceFrames = 4096;  // provisional ±1 block (OD-6)
};

/// Parse config/harness.toml + config/tolerances.toml under `pitchlabRoot`.
/// Throws ConfigError on missing/invalid files (they are committed defaults,
/// not optional). Missing tolerance entry falls back to the provisional
/// default with no error (§15.2 allows the file to grow).
[[nodiscard]] HarnessConfig loadHarnessConfig(const std::filesystem::path& pitchlabRoot);

}  // namespace pitchlab
