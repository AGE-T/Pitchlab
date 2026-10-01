// Pitch Lab VST3 product layer — the realtime-capability classification
// (Task 32). See realtime_status.h for the evidence model; this file is
// the pure classifier + the UI-side label formatting.

#include "vst/realtime_status.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "vst/parameters.h"

namespace pitchlab::vst {

namespace {

// ---------------------------------------------------------------------------
// The encoded BENCHMARK PRIORS (Task 32).
//
// Each row is an EXPLICIT product rule derived from committed, measured
// benchmark evidence — never a blanket engine-wide verdict and never a
// final classification (the runtime measurement always supersedes it once
// the window is sufficient). The prior applies only BEFORE measurement,
// i.e. for a fraction of a second after a chain adoption (or while the
// chain is not yet producing): its job is the honest head start — the
// warning is visible immediately where measured evidence says it is
// likely, instead of after the first ~100 ms of audio.
//
// THE RULES ACTUALLY ENCODED (one row; each cites its evidence):
//
//   native.granular, fs >= 88200 Hz -> LIMITED
//     Evidence: the Task-30 matrix (results/vst3/task30/
//     realtime-envelope-policy-measurements.md, the 96 kHz block): RTF
//     1.146–1.159 at identity, 1.35–1.89 at -12 st (worst: grain 0.5 s,
//     1.73–1.89; -12 + LFO 2, 1.40–1.44) and 0.58–0.69 at +12 st on the
//     reference machine — i.e. the double-rate granular configurations
//     span the capacity boundary BY CONFIGURATION. The prior is LIMITED
//     (close to or beyond capacity, configuration-dependent — the honest
//     level for a boundary-spanning family), NOT "not sustainable": the
//     +12-st configurations measure comfortably inside capacity and a
//     blanket offline verdict would be false. The 88.2 kHz extension is
//     the double-rate CLASS (cost scales with frames per timeline second;
//     88.2 is strictly cheaper than 96 — the worst rows stay above 1.0);
//     documented here, measured only at 96 kHz.
//
// NOTHING ELSE is encoded: every other engine/rate combination has either
// measured comfortably inside capacity in the committed evidence (the PV
// engines at 48 AND 96 kHz: RTF 0.012–0.063; varispeed/vardelay at
// 44.1/48: 0.01–0.3) or has no committed evidence at all — in both cases
// the pre-measurement state is the honest UNKNOWN, and the runtime
// measurement (which follows within a fraction of a second) makes the
// actual claim.
// ---------------------------------------------------------------------------

struct BenchmarkPrior {
  const char* engineId;
  double fsMin;      // applies when the activation's sample rate >= fsMin
  RtLevel level;
};

constexpr BenchmarkPrior kBenchmarkPriors[]{
    {"native.granular", 88200.0, RtLevel::Limited},
};

}  // namespace

// ---------------------------------------------------------------------------
// The classifier
// ---------------------------------------------------------------------------

RtClassification classifyRealtimeStatus(const StatusSnapshot& st) {
  RtClassification c;
  c.faults = st.faults;
  c.windowFrames = st.rtFrames;

  // No active chain (never activated, building, or hard-reset): nothing
  // runs, nothing is measured — the honest state is UNKNOWN, not a guess.
  if (!st.chainReady || !(st.sampleRate > 0.0)) {
    return c;
  }

  // ---- 1) the MEASURED classification (supersedes everything) -----------
  // The window: input frames since the CURRENT chain's adoption (the
  // adapter resets the accumulators at adoption — the measurement always
  // describes the current configuration, never a blend of retired
  // chains). Two process blocks minimum, however large the host's blocks.
  const int64_t minFrames =
      std::max<int64_t>(kRtfMinWindowFrames, 2 * static_cast<int64_t>(st.maxBlock));
  if (st.rtFrames >= minFrames) {
    const double timelineSec = static_cast<double>(st.rtFrames) / st.sampleRate;
    if (timelineSec > 0.0) {
      c.basis = RtBasis::Measured;
      c.measuredRtf = (static_cast<double>(st.engineCpuNanos) / 1e9) / timelineSec;
      if (c.measuredRtf > kRtfSustainableMax) {
        c.level = RtLevel::NotSustainable;
      } else if (c.measuredRtf > kRtfOkMax) {
        c.level = RtLevel::Limited;
      } else {
        c.level = RtLevel::Ok;
      }
      // Fault escalation: the aggregate audio-path fault counter (engine
      // process faults + exceptions + delivery underruns + dry-history
      // misses — its historical composition) is EVIDENCE about delivery
      // sustainability, independent of the CPU ratio. A path that
      // measured fine but actually failed to deliver wet at least once
      // this activation is at least LIMITED; a path near the boundary
      // that also faulted is NOT sustainable. One level, never two.
      if (st.faults > 0) {
        if (c.level == RtLevel::Ok) {
          c.level = RtLevel::Limited;
        } else if (c.level == RtLevel::Limited) {
          c.level = RtLevel::NotSustainable;
        }
        c.faultEscalated = true;  // keeps the formatter's causal text honest
      }
      return c;
    }
  }

  // ---- 2) the BENCHMARK prior (pre-measurement only) ---------------------
  const char* engineId =
      st.engineIndex >= 0 ? engineIdForIndex(st.engineIndex) : nullptr;
  if (engineId != nullptr) {
    for (const BenchmarkPrior& p : kBenchmarkPriors) {
      if (std::strcmp(engineId, p.engineId) == 0 && st.sampleRate >= p.fsMin) {
        c.level = p.level;
        c.basis = RtBasis::Benchmark;
        // A prior is never fault-blind, but pre-measurement there is no
        // meaningful window yet; the fault escalation above runs only on
        // the measured path (the counters accumulate with audio, and the
        // measured classification follows within a fraction of a second).
        return c;
      }
    }
  }

  // ---- 3) the honest UNKNOWN ----------------------------------------------
  return c;
}

// ---------------------------------------------------------------------------
// Label formatting (UI thread / tools / tests — the only string site)
// ---------------------------------------------------------------------------

void formatRealtimeStatusLine(const RtClassification& c, char* buf, std::size_t bufSize) {
  if (buf == nullptr || bufSize == 0) return;
  switch (c.basis) {
    case RtBasis::Measured:
      switch (c.level) {
        case RtLevel::Ok:
          std::snprintf(buf, bufSize, "REALTIME OK · MEASURED RTF %.2f", c.measuredRtf);
          return;
        case RtLevel::Limited:
          std::snprintf(buf, bufSize, "REALTIME LIMITED · MEASURED RTF %.2f", c.measuredRtf);
          return;
        case RtLevel::NotSustainable:
          std::snprintf(buf, bufSize, "!! REALTIME NOT SUSTAINABLE · MEASURED RTF %.2f",
                        c.measuredRtf);
          return;
        case RtLevel::Unknown:
          break;  // measured basis always has a level; defensive
      }
      break;
    case RtBasis::Benchmark:
      switch (c.level) {
        case RtLevel::Limited:
          std::snprintf(buf, bufSize, "REALTIME LIMITED · BENCHMARK PRIOR · MEASURING");
          return;
        case RtLevel::NotSustainable:
          std::snprintf(buf, bufSize, "!! REALTIME NOT SUSTAINABLE · BENCHMARK PRIOR");
          return;
        default:
          break;  // the encoded priors are Limited only; defensive
      }
      break;
    case RtBasis::Unset:
      break;
  }
  // Unknown / None (and defensive fallbacks): the honest no-claim state.
  std::snprintf(buf, bufSize, "REALTIME STATUS UNKNOWN · MEASURING");
}

void formatRealtimeDetailLine(const RtClassification& c, char* buf, std::size_t bufSize) {
  if (buf == nullptr || bufSize == 0) return;
  buf[0] = '\0';
  if (c.level == RtLevel::NotSustainable) {
    if (c.basis == RtBasis::Measured && !c.faultEscalated) {
      // The task's required semantics, verbatim in meaning: the CURRENT
      // configuration exceeds the measured realtime capacity; the offline
      // render is the tool for this setting.
      std::snprintf(buf, bufSize,
                    "CURRENT CONFIGURATION EXCEEDS MEASURED REALTIME CAPACITY — USE OFFLINE RENDER");
    } else if (c.basis == RtBasis::Measured) {
      std::snprintf(buf, bufSize,
                    "REALTIME DELIVERY FAULTS + CPU NEAR/BEYOND CAPACITY — USE OFFLINE RENDER");
    } else {
      std::snprintf(buf, bufSize,
                    "BENCHMARK EVIDENCE: BEYOND MEASURED CAPACITY AT THIS RATE — USE OFFLINE RENDER");
    }
    return;
  }
  if (c.level == RtLevel::Limited) {
    if (c.basis == RtBasis::Benchmark) {
      std::snprintf(buf, bufSize,
                    "96 KHZ BENCHMARK: RTF 0.6-1.9 BY CONFIG — CONFIRMING BY MEASUREMENT");
    } else if (c.faultEscalated) {
      std::snprintf(buf, bufSize, "DELIVERY FAULTS RECORDED THIS ACTIVATION — PATH DEGRADED");
    } else {
      std::snprintf(buf, bufSize, "NEAR MEASURED REALTIME CAPACITY — HEADROOM BELOW 25%");
    }
    return;
  }
  // Ok / Unknown: no detail line (the envelope line owns the slot).
}

const char* rtLevelName(RtLevel level) {
  switch (level) {
    case RtLevel::Ok: return "ok";
    case RtLevel::Limited: return "limited";
    case RtLevel::NotSustainable: return "not-sustainable";
    case RtLevel::Unknown: return "unknown";
  }
  return "unknown";
}

const char* rtBasisName(RtBasis basis) {
  switch (basis) {
    case RtBasis::Unset: return "none";
    case RtBasis::Measured: return "measured";
    case RtBasis::Benchmark: return "benchmark";
  }
  return "none";
}

}  // namespace pitchlab::vst
