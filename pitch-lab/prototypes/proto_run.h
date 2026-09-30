#pragma once

// Pitch Lab — Task 28 shared prototype runners:
//   * runCandidateEvaluation — the full material x transform matrix (+
//     the parameter-effect matrix), measurement records + curated WAV
//     renders, written as canonical JSON artifacts
//   * runCandidateSelftest  — the fast ctest entry (reduced matrix,
//     invariant assertions, determinism double-run, allocation audit,
//     block-split invariance); no source-tree writes
//   * runRtProbe            — the realtime feasibility probe (per-block
//     timing, RTF, allocation audit, block-size sweep, block-split
//     invariance)
//
// Every runner is deterministic in its ARTIFACTS (the rtprobe timing
// fields are the one exception, marked in the JSON).

#include <functional>
#include <string>
#include <vector>

#include "prototypes/proto_engine.h"

namespace pitchlab::proto {

struct CandidateSpec {
  std::string id;                                  // e.g. "ola"
  std::string family;                              // e.g. "time-segment OLA"
  ProtoParams defaultParams;
  std::vector<std::pair<std::string, ProtoParams>>
      paramSweeps;  // label -> full parameter set (param-effect matrix)
  std::function<std::unique_ptr<ProtoEngine>()> make;
  // Selftest static-shift dominant tolerance (semitones). The
  // IMPLEMENTATION invariants (identity exactness, duration, finiteness,
  // determinism, allocation audit, block-split invariance) are asserted
  // tightly; the shifted-pitch check is a GROSS regression net whose width
  // must reflect the candidate's real artifact family (e.g. OLA's
  // grain-rate FM/AM biases every frequency estimator — the measured bias
  // is part of the candidate's character, not an implementation defect;
  // the evidence lives in the report records).
  double selftestShiftToleranceSt = 0.6;
};

/// Full evaluation; writes <out>/<id>/candidate-report.json (+ renders/).
/// Returns 0 on success, non-zero when any record failed its integrity
/// invariants.
int runCandidateEvaluation(const CandidateSpec& spec,
                           const std::string& outDir, bool writeRenders);

/// The ctest selftest (no writes). Returns 0 on PASS.
int runCandidateSelftest(const CandidateSpec& spec);

/// The RT feasibility probe; writes <out>/<id>/rtprobe.json when outDir
/// is non-empty (timing fields are local-evidence, not byte-deterministic).
int runRtProbe(const CandidateSpec& spec, const std::string& outDir);

}  // namespace pitchlab::proto
