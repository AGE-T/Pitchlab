#pragma once

// Pitch Lab — native.timepitch, the unified time-pitch engine (implementation
// specification §6.6 — the task-33 SoT recording — and the frozen
// implementation behaviour §6.6.1, recorded 2026-10-01 BEFORE coding, UQ-1).
//
// MODEL (§6.6): ONE production engine unifying the four Task28 research
// candidates as USER-FACING MODES:
//
//   Fixed          = OLA   (proto.ola;   fixed-grid window-product OLA)
//   Adaptive       = WSOLA (proto.wsola; waveform-adaptive placement)
//   Pitch-Synced   = TD-PSOLA (proto.tdpsola; mark-scheduled raw OLA + s/P)
//   Pitch + Formant = FD-PSOLA (proto.fdpsola; per-voiced-grain spectral
//                               formant shift, γ = 1 bit-identical to
//                               Pitch-Synced)
//
// The four algorithms are NOT mathematically collapsed: each mode keeps its
// measured character. The engine is a segment/grain engine with a SHARED
// segment core (sliding input window + compaction, window banks, OLA
// accumulators, emission frontier, shared §7 resampler stage on the
// stretch schedule) + per-mode PLACEMENT LAW + per-mode ACCUMULATION POLICY:
//
//   * Fixed/Adaptive: window-product normalisation y[n] = Σ w·x / Σ w
//     (content first, window sum second — the exact order is part of the
//     determinism; 0-guard ⇒ edge fade), then the stage-B resampler on the
//     stretch schedule (the §6.5 OLA family policy).
//   * Pitch-Synced/Pitch + Formant: RAW OLA + per-grain s/P accumulation
//     scale (NO window-sum normalisation — window-product normalisation was
//     a MEASURED DEFECT in TD-PSOLA; the two policies stay mode-dependent,
//     NEVER merged — the owner architectural principle, §6.6.1 item 6), the
//     §7 resampler BYPASSED (the pitch schedule re-spans periods directly).
//
// MODE-CHECKPOINT NOTE (task 33): the engine ships mode by mode through the
// recorded VST checkpoints — this translation unit's FIRST checkpoint
// registers the engine with the Fixed (OLA) mode functional; each further
// checkpoint appends its mode choice and implementation. The parameter
// descriptor table (below) grows accordingly; tags are stable throughout.
// No mode ever silently falls back to another one (an unimplemented mode is
// a CONFIG ERROR, never a disguised substitute).
//
// DurationBehaviour (mode-scoped, the recorded §D.4 amendment; OQ-1):
// Preserving (Fixed, Adaptive) / RateFollowing (Pitch-Synced, Pitch +
// Formant — the implemented MC90 pitch schedule: voiced spans change
// duration by 1/β, unvoiced unchanged).
//
// Latency (§6.6.1 item 10, frozen constants):
//   Fixed/Adaptive:        input = N/2 + Hs + 2K + 64   (2144 @48k defaults)
//                          output = ⌊(N + Hs + 2K + 256)/minRatio⌋ + 64
//   Pitch-Synced/+Formant: input = 2·pMax + 2K + 128 + Λ_tr  (5848 @48k ≈
//                          122 ms — FIRST-CLASS, never hidden, OQ-2)
//                          output = ⌊(4·pMax + 2K + 512)/max(0.25, minRatio)⌋ + 64
//   (K = 16 — the §7 Standard preset's half-width; Λ_tr per-rate frozen
//   table 3712/3768/7423/7536/15072 @44.1/48/88.2/96/192 kHz.)
//
// Determinism (§6.6.1 item 13): Deterministic — no RNG anywhere;
// absolute-position schedules + ascending accumulation orders + doubles
// throughout + prepared caches; same binary + same input + same config
// trajectory ⇒ bit-identical output for ANY block split (T-E6/T-E7).
//
// Allocation (§6.6.1 item 16): prepare() only (rings, window banks, plan
// caches; POCKETFFT_CACHE_SIZE 0); ZERO allocation in process()/finish()/
// reset() (T-A1/T-D3).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers — an independently
// reusable engine. The Task28 prototypes (prototypes/) stay research-only;
// the production code ports their FROZEN ARITHMETIC (the §6.6.1 items name
// the normative prototype references per mode).
//
// Engine parameters (§6.6 table + §6.6.1 items 1/18; the seven-descriptor
// model, verbatim domains from the validated prototypes):
//   mode            Text   fixed | adaptive | pitch_synced | pitch_formant
//                          (default fixed; rebuildsChain)
//   window_frames   int    [64, 16384], default 2048 — Fixed+Adaptive
//   overlap         int    {2, 4, 8}, default 2 (Hs = N/overlap ≥ 8)
//                          — Fixed+Adaptive
//   window_shape    Text   hann | hamming | bartlett | rect (default hann)
//                          — Fixed+Adaptive
//   tolerance_frames int   [0, 8192], default 768 — Adaptive
//   formant_ratio   real   [0.25, 4.0], default 1.0 — Pitch + Formant
//   puv_hz          real   [50, 500], default 200 — HIDDEN (Pitch-Synced
//                          modes, engine-internal: all Task28 evidence is
//                          at the validated default)
// Resampler quality: internal — the §7 Standard preset hard-wired on every
// validated path (no descriptor row).

#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

/// The registry descriptor for native.timepitch (§6.6 sheet; 6th production
/// engine, selector index 5 — used by registerProductionEngines(), the
/// single registration point).
[[nodiscard]] EngineDescriptor timePitchEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makeTimePitchEngine();

}  // namespace pitchlab
