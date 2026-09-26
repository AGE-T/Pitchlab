#pragma once

// Pitch Lab — version and phase constants (infrastructure, not DSP).
//
// Per Operating Principles §81 the status chain of everything in the build is
// tracked in kPhase. The implementation freeze (2026-09-25) shipped
// specification + infrastructure only; cycle 1 (§17 step 1) implemented the
// core primitives; cycle 2 (§17 step 2) the curve library + corpus; cycle 3
// (2026-09-26, §17 step 3) implemented the FIRST REAL ENGINE
// (native.varispeed, the rate-following reference) — the registry now
// contains exactly the implemented engines (one). The remaining four v0.1
// engines are §17 step-4 work. The value is mirrored in manifests and
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
