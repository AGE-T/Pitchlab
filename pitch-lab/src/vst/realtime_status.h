#pragma once

// Pitch Lab VST3 product layer — THE REALTIME-CAPABILITY STATUS (Task 32).
//
// THE PRODUCT CONCEPT (spec §7, the Task-32 extension): an honest,
// configuration-aware indication of whether the CURRENT configuration is
// supported sustainably by the realtime processing path — displayed in the
// editor's status area, derived from the adapter's NUMERIC diagnostics.
//
// THE CENTRAL DISTINCTION (kept explicit in the model):
//
//   ENGINE CAPABILITY
//     != CURRENT MEASURED/OBSERVED REALTIME SUSTAINABILITY
//
// An engine may fully support the realtime adapter architecture (all five
// v0.1 engines do — they run through the adapter) while a particular
// CONFIGURATION (engine + sample rate + block size + engine parameters +
// adaptation mode) exceeds the MEASURED realtime capacity. A "not
// sustainable" status is therefore a statement about the current
// configuration's measured cost, NEVER a blanket engine-wide verdict: the
// repository's own evidence (the Task-30 matrix,
// results/vst3/task30/realtime-envelope-policy-measurements.md) shows
// native.granular at 96 kHz measuring RTF 0.58 at +12 st while the
// identity/-12 rows measure 1.1–1.9 — one engine, opposite verdicts by
// configuration. No engine-wide "offline only" rule is encoded anywhere.
//
// THE THREE SOURCES OF EVIDENCE (the basis):
//   * MEASURED  — the audio thread's own runtime measurement since the
//     current chain's adoption (StatusSnapshot::engineCpuNanos /
//     rtFrames): the steady-clock cost of the engine's process/finish work
//     against the audio timeline it covers. This is runtime-safe,
//     per-configuration (the window resets at chain adoption), and always
//     supersedes any prior. It is exactly the quantity the Task-29/30
//     measurement drives measured host-side ("RTF").
//   * BENCHMARK — an explicitly encoded PRIOR from committed, measured
//     benchmark evidence (kBenchmarkPriors; each row cites its artifact).
//     Used ONLY before the runtime measurement window is sufficient — a
//     fraction of a second after a chain adoption. A prior is never a
//     final verdict and never engine-wide-offline.
//   * UNSET     — no reliable classification can yet be made: the honest
//     UNKNOWN state (spec: "rather than guessing"). Displayed as
//     "REALTIME STATUS UNKNOWN", never as a guessed OK.
//
// STATUS OWNERSHIP (spec §4.3, unchanged): the audio thread publishes
// NUMBERS ONLY (the StatusSnapshot fields); ALL string derivation — the
// labels, the warning text — happens HERE, on the consumer side (the
// editor's UI thread / tools / tests). This module contains no VSTGUI
// dependency; it is a pure product-layer function over the snapshot.

#include <cstddef>
#include <cstdint>

#include "vst/realtime_adapter.h"

namespace pitchlab::vst {

// ---------------------------------------------------------------------------
// The classification model
// ---------------------------------------------------------------------------

/// The realtime-sustainability LEVEL of the current configuration.
enum class RtLevel {
  Ok,              // measured comfortably inside capacity (headroom >= 25%)
  Limited,         // near or at the measured capacity boundary, or a prior
                   // from measured benchmark evidence, or fault-degraded
  NotSustainable,  // measured beyond capacity (or fault-evidenced failure):
                   // the current configuration is not sustainably realtime
  Unknown,         // no reliable classification can yet be made
};

/// What the level is derived from (the evidence class — kept explicit so
/// the UI and the documentation can distinguish measured truth from
/// benchmark priors from honest ignorance).
enum class RtBasis {
  Unset,      // UNKNOWN — insufficient evidence (named Unset, not None:
              // X11's Xlib.h #defines None, and consumers may include X11
              // before this header)
  Measured,   // the runtime measurement window (current chain)
  Benchmark,  // an encoded prior from committed measured evidence
};

/// One classification result. `measuredRtf` and `windowFrames` are the
/// measurement evidence (meaningful when basis == Measured; kept for
/// diagnostics/formatters regardless). `faultEscalated` records that the
/// level was RAISED by fault evidence (the snapshot's aggregate `faults`
/// at classification time) rather than by the measured ratio.
struct RtClassification {
  RtLevel level = RtLevel::Unknown;
  RtBasis basis = RtBasis::Unset;
  double measuredRtf = 0.0;
  int64_t windowFrames = 0;
  uint64_t faults = 0;
  bool faultEscalated = false;
};

// ---------------------------------------------------------------------------
// The classification thresholds (documented product rules — Task 32)
// ---------------------------------------------------------------------------

/// Measured RTF at or below this is OK: the engine work leaves >= 25%
/// timeline headroom for the adapter's own overhead (readWet, mix, meters)
/// and the host's remaining buffer-time budget. Chosen as the product's
/// documented safety margin; NOT an invented DSP truth.
inline constexpr double kRtfOkMax = 0.75;

/// Measured RTF above 1.0 is NOT SUSTAINABLE: the engine work alone
/// exceeds the audio timeline — the production cannot keep up and the
/// delivery will degrade (the Task-29/30 granular@96k evidence class).
/// (0.75, 1.0] is LIMITED: no meaningful headroom.
inline constexpr double kRtfSustainableMax = 1.0;

/// The minimum measurement window (input frames since the current chain's
/// adoption) before the cumulative RTF average is trusted. The window is
/// additionally floored at two process blocks (a measurement spanning at
/// least two calls, whatever the host's block size). At 44.1–96 kHz this
/// is ~85–190 ms — the status settles within a fraction of a second.
inline constexpr int64_t kRtfMinWindowFrames = 8192;

// ---------------------------------------------------------------------------
// The classification (pure function over the numeric snapshot)
// ---------------------------------------------------------------------------

/// Classify the CURRENT configuration's realtime sustainability from the
/// adapter's numeric status snapshot. Pure, deterministic, thread-safe to
/// call from any thread (reads only the snapshot's values; no audio-path
/// interaction). See the header comment for the evidence model.
[[nodiscard]] RtClassification classifyRealtimeStatus(const StatusSnapshot& st);

// ---------------------------------------------------------------------------
// UI-thread label formatting (the ONLY place these strings are derived;
// the audio thread never formats — spec §4.3)
// ---------------------------------------------------------------------------

/// The short status line, e.g. "REALTIME OK · MEASURED RTF 0.34" /
/// "!! REALTIME NOT SUSTAINABLE · MEASURED RTF 1.42" /
/// "REALTIME LIMITED · 96 KHZ BENCHMARK PRIOR" /
/// "REALTIME STATUS UNKNOWN · MEASURING". Deterministic for a given
/// classification; ASCII only (font-portable).
void formatRealtimeStatusLine(const RtClassification& c, char* buf, std::size_t bufSize);

/// The detail/warning line shown when the level is Limited or
/// NotSustainable ("" for Ok/Unknown — the envelope line owns that slot).
/// The NOT-SUSTAINABLE text states the semantics the product documentation
/// defines: the current configuration exceeds the MEASURED realtime
/// capacity — use the offline render for this setting. ASCII only.
void formatRealtimeDetailLine(const RtClassification& c, char* buf, std::size_t bufSize);

/// Stable level name (tools/tests): "ok" / "limited" / "not-sustainable" /
/// "unknown".
[[nodiscard]] const char* rtLevelName(RtLevel level);
/// Stable basis name (tools/tests): "none" / "measured" / "benchmark".
[[nodiscard]] const char* rtBasisName(RtBasis basis);

}  // namespace pitchlab::vst
