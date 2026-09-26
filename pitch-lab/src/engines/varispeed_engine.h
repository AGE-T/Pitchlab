#pragma once

// Pitch Lab — native.varispeed engine (implementation specification §6.1
// rate-following reference engine; frozen implementation behaviour §6.1.1,
// recorded 2026-09-26 BEFORE coding, cycle 3).
//
// MODEL (§6.1): fractional read pointer advancing ratio[r] input frames per
// output frame; samples reconstructed by the shared Kaiser windowed-sinc
// interpolator (§7 — via interpolateAt; NO resampling code duplicated here);
// anti-alias pre-filter (§7.4, shared designAntiAliasFir) applied streaming
// with engine state when r_max > 1 and !allow_aliasing. Read-position rule:
//   r(0) = 0;  y[m] = x̂(r(m));  r(m+1) = r(m) + ratioEff[clamp(floor(r(m)), 0, N_in-1)]
// — the same recurrence the renderer replays for the length expectation
// (§4.3.4/§6.1.1 item 2: expectation and reference cannot diverge).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers (no harness,
// metrics, corpus, web workbench) — an independently reusable engine.
//
// Engine parameters (§14 keys): resample_quality = "small"|"standard"|
// "reference" (default "reference"); allow_aliasing = bool (default false;
// disables the pre-filter only — creative mode, manifest-recorded).
//
// Determinism (§6.1): bit-identical for identical (input, config, fs,
// channels, block schedule); additionally bit-identical ACROSS block
// schedules (each output frame is a pure function of absolute input —
// observed stronger property, NOT the engine-level contract; T-E7 keeps
// asserting the L-5 −80 dBFS audio-equivalence criterion).

#include <memory>
#include <vector>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"
#include "core/resampler.h"

namespace pitchlab {

/// The registry descriptor for native.varispeed (§14 row, binding identity;
/// used by registerProductionEngines() — the single registration point).
[[nodiscard]] EngineDescriptor varispeedEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makeVarispeedEngine();

}  // namespace pitchlab
