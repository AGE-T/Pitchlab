#pragma once

// Pitch Lab — version and phase constants (infrastructure, not DSP).
//
// Per Operating Principles §81 the status chain of everything in the build is
// tracked in kPhase. The implementation freeze (2026-09-25) shipped
// specification + infrastructure only; cycle 1 (2026-09-26, implementation
// specification §17 step 1) began the implementation phase: core types,
// deterministic RNG, WAV I/O and the shared resampling primitive are now
// implemented and unit-tested. NO engine is implemented (the registry remains
// empty and a CI test asserts it). The value is mirrored in manifests and
// reported by `pitchlab --version`.

namespace pitchlab {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// Operating-Principles status chain marker for the whole build.
inline constexpr const char* kPhase = "implementation";

/// "0.1.0"
const char* versionString();

/// Status-chain phase of this build (see kPhase).
const char* phaseString();

/// Compiler identification for provenance manifests
/// (e.g. "gcc 13.2.0" / "clang 18.1.3"); never empty.
const char* compilerId();

}  // namespace pitchlab
