#pragma once

// Pitch Lab — Task 28 candidate re-evaluation: the deterministic
// sound-design test corpus (research prototypes; see
// research/task28-candidate-re-evaluation.md).
//
// CLEAN-ROOM BOUNDARY: research-only code, OUTSIDE the production engine
// registry and the v0.1 result product. Nothing in pitch-lab/prototypes/
// is reachable from the VST3 product layer; the frozen v0.1 sources are
// never modified. The five candidates are own implementations from
// published algorithm descriptions only (no external DSP code imported).
//
// CORPUS CONTRACT (task brief Phase F, frozen here before coding):
//   * Twelve deterministic materials, synthesised in double with the
//     project PCG64 (makeConsumerStream(1, "task28.corpus.<id>")); no
//     wall-clock, no platform entropy; same binary => bit-identical.
//   * 48 kHz; eleven mono materials + one stereo (pad) material for the
//     inter-channel coherence probes; every material peak-normalised to
//     0.25 (-12 dBFS, shared across channels).
//   * `tonal` marks whether a dominant-frequency measurement is
//     meaningful; `carrierHz` is the nominal carrier (self-referenced
//     measurement uses the measured input dominant anyway).
//   * Durations 0.8–2.0 s: long enough for steady-state statistics and
//     onset/AM analysis, short enough to keep the 5-candidate matrix and
//     the committed WAV evidence small.

#include <string>
#include <vector>

namespace pitchlab::proto {

struct Material {
  std::string id;
  std::string description;
  double fs = 48000.0;
  int channels = 1;
  bool tonal = false;       // dominant-frequency / pitch-response meaningful
  double carrierHz = 0.0;   // nominal carrier (interpretation aid only)
  std::vector<std::vector<double>> ch;  // planar, [c][frames]
};

/// The twelve frozen materials (generated on first use, deterministic).
[[nodiscard]] const std::vector<Material>& corpusMaterials();

/// Find a material by id (nullptr when unknown).
[[nodiscard]] const Material* findMaterial(const std::string& id);

}  // namespace pitchlab::proto
