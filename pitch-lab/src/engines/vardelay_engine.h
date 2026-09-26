#pragma once

// Pitch Lab — native.vardelay engine (implementation specification §6.2
// variable-delay (Doppler-style) musical shifter; frozen implementation
// behaviour §6.2.1, recorded 2026-09-26 BEFORE coding, cycle 4).
//
// MODEL (§6.2): y(t) = x(t − D(t)) with instantaneous ratio 1 − D′(t); the
// output timeline == the input timeline (Preserving). The wrapped active
// delay v lives in [0, E]; per output frame t, after the read:
//   v ← v + (1 − ratio[clamp(t, 0, N_in−1)])            (§6.2.1 item 2)
// and wraps v by ±E when it leaves [0, E] strictly (item 3). Wraps blend
// the pre-wrap and post-wrap trajectories (exactly E apart) with
// equal-power crossfades — LOWER wraps fade BEFORE the crossing (curve
// look-ahead, exact), UPPER wraps fade AFTER (item 4). Reads are shared-
// primitive windowed-sinc fractional reads (§7 Small preset via
// interpolateAt; NO resampling code duplicated here; cutoff 1.0 — the
// §6.2 sheet specifies no AA pre-filter, item 8).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers (no harness,
// metrics, corpus, web workbench) — an independently reusable engine.
//
// Engine parameters (§14 keys, §6.2.1 item 1): excursion_seconds (double,
// default 0.5; E = round(sec·fs) ≥ 1), crossfade_frames (integer, default
// 2048; W frames VERBATIM at the job rate), read_kernel (string; only
// "small-sinc").
//
// Determinism (§6.2): bit-identical for identical (input, config, fs,
// channels, block schedule); additionally bit-identical ACROSS block
// schedules (each output frame is a pure function of absolute input
// frames + the absolute curve — observed stronger property, NOT the
// engine-level contract; T-E7 keeps asserting the L-5 −80 dBFS
// audio-equivalence criterion).

#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

/// The registry descriptor for native.vardelay (§14 row, binding identity;
/// used by registerProductionEngines() — the single registration point).
[[nodiscard]] EngineDescriptor vardelayEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makeVardelayEngine();

}  // namespace pitchlab
