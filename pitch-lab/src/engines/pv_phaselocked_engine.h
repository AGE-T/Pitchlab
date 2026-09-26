#pragma once

// Pitch Lab — native.pv.phaselocked engine (implementation specification §6.4
// phase-locked PV, Laroche–Dolson '99 peak-shift techniques; frozen
// implementation behaviour §6.4.1, recorded 2026-09-26 BEFORE coding, cycle 4).
//
// MODEL (§6.4, the L-D'99 peak-shift formulation — the phase-locked
// alternative to native.pv.classic): analysis STFT (fixed grid, hop H) ->
// per-bin IF estimation -> spectral peak detection -> region-of-influence
// assignment (midpoints) -> IDENTITY phase locking (the region's complex
// spectrum rigidly rotated by the cumulated per-region rotation Z,
// Z_{u+1} = Z_u·e^{j·Δω_{u+1}·H}; the intra-region phase differences are
// preserved — §6.4.1 item 8) -> per-region frequency translation
// Δω = (ρ−1)·ω̂_peak with linear sideband interpolation of the placement
// (§6.4.1 item 9) -> IFFT -> windowed OLA on the SAME grid as the analysis.
// Duration-preserving by construction: the output timeline EQUALS the input
// timeline — NO time-stretch, NO resampler, NO AA pre-FIR (§6.4.1 item 2).
//
// DEPENDENCY (the documented "lab binary" vendoring, build/CI spec §7):
// this TU includes the vendored pocketfft header (external/pocketfft/,
// BSD-3-Clause, ORIGIN.toml-pinned) and uses the persistent plan object
// pocketfft::detail::pocketfft_r<double> constructed in prepare() — the
// header's public convenience functions allocate per call and are NOT used
// in the processing path (T-A1; §6.4.1 item 18).
//
// BOUNDARY (T-A2): this TU includes ONLY core headers + the vendored
// pocketfft header (no harness, metrics, corpus, web workbench) — an
// independently reusable engine.
//
// Engine parameters (§14 keys, §6.4.1 item 1): window ("hann" only),
// fft_size (power of two >= 32, default 2048), hop (1..fft_size/2, default
// 512), locking_mode ("identity" only — the §14-registered key; "scaled" is
// specified but not implemented in v0.1 and is a CONFIG ERROR, never a
// silent substitution). Deterministic; the seed is accepted and unused.

#include <memory>

#include "core/engine_registry.h"
#include "core/pitch_engine.h"

namespace pitchlab {

/// The registry descriptor for native.pv.phaselocked (§14 row, binding
/// identity; used by registerProductionEngines() — the single registration
/// point).
[[nodiscard]] EngineDescriptor pvPhaseLockedEngineDescriptor();

/// Factory for the registry (§4.2.1 item 5).
[[nodiscard]] std::unique_ptr<PitchEngine> makePvPhaseLockedEngine();

}  // namespace pitchlab
