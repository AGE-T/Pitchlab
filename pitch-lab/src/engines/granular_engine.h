#pragma once

// Pitch Lab — native.granular engine (implementation specification §6.5
// granular pitch engine, creative-leaning; frozen implementation behaviour
// §6.5.1, recorded 2026-09-26 BEFORE coding, cycle 4).
//
// MODEL (§6.5): a constant output grain grid at hop Hg (grains k = 0, 1, …
// covering [k·Hg, k·Hg + G)); each grain reads the input starting at r_k
// (the read grid r_0 = 0, r_{k+1} = r_k + Hg·ratio_k — the quantised
// control grid) advancing at ratio_k per output frame; windowed (hann or
// triangular, precomputed table) and overlap-added with LOCAL-SUM
// normalisation (§6.5.1 item 5); Preserving: output timeline == input
// timeline, canonical length N_in + G. Reads are fractional sinc reads
// through the shared §7 Small-preset interpolator (NO resampling code
// duplicated here; NO AA pre-filter — the sheet specifies none).
// Grain-start jitter: SeededDeterministic dither of the READ start only
// (one draw per grain, grain-indexed, shared across channels — the
// synchronised schedule; consumer tag "engine.native.granular", §4.7).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers (no harness,
// metrics, corpus, web workbench) — an independently reusable engine.
//
// Engine parameters (§14 keys, §6.5.1 item 1): grain_seconds (double,
// default 0.1; G = round(sec·fs) ≥ 4), overlap (integer ≥ 4, default 4;
// Hg = round(G/overlap) ≥ 1), window ("hann"|"triangular", default hann),
// jitter_frames (integer ≥ 0, default 0; > 0 activates seeded dither).
//
// Determinism (§6.5): SeededDeterministic — byte-identical for identical
// (input, curve, config, seed, fs, channels, block schedule); additionally
// bit-identical ACROSS block schedules (the RNG draws are grain-indexed,
// not call-indexed — observed stronger property, NOT the engine-level
// contract; T-E7 keeps asserting the L-5 −80 dBFS audio-equivalence).

#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"
#include "core/rng.h"

namespace pitchlab {

/// The registry descriptor for native.granular (§14 row, binding identity;
/// used by registerProductionEngines() — the single registration point).
[[nodiscard]] EngineDescriptor granularEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makeGranularEngine();

}  // namespace pitchlab
