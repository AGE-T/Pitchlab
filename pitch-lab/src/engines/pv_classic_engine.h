#pragma once

// Pitch Lab — native.pv.classic engine (implementation specification §6.3
// classic phase vocoder, benchmark baseline; frozen implementation
// behaviour §6.3.1, recorded 2026-09-26 BEFORE coding, cycle 4).
//
// MODEL (§6.3, the sheet's verbatim pipeline): analysis STFT (fixed grid,
// hop H) -> per-bin phase propagation (instantaneous-frequency estimation)
// -> synthesis with the STRETCHED integer hop (h_n = round(rho_n·H)) ->
// overlap-add of the stretched signal (window-product normalisation) ->
// resample by the ratio through the shared §7 resampler (Standard preset).
// Preserving: output timeline == input timeline, canonical N_in + N.
//
// DEPENDENCY (the documented "lab binary" vendoring, build/CI spec §7):
// this TU includes the vendored pocketfft header (external/pocketfft/,
// BSD-3-Clause, ORIGIN.toml-pinned) and uses the persistent plan object
// pocketfft::detail::pocketfft_r<double> constructed in prepare() — the
// header's public convenience functions allocate per call and are NOT used
// in the processing path (T-A1; §6.3.1 item 16).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers + the vendored
// pocketfft header (no harness, metrics, corpus, web workbench) — an
// independently reusable engine.
//
// Engine parameters (§14 keys, §6.3.1 item 1): window ("hann" only),
// fft_size (power of two >= 32, default 2048), hop (1..fft_size/2,
// default 512). Deterministic; the seed is accepted and unused.

#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

/// The registry descriptor for native.pv.classic (§14 row, binding identity;
/// used by registerProductionEngines() — the single registration point).
[[nodiscard]] EngineDescriptor pvClassicEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makePvClassicEngine();

}  // namespace pitchlab
