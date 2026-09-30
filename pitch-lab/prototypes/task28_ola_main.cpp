// Pitch Lab — Task 28 Phase A entry point: proto.ola evaluation driver.
//
// Modes:
//   task28_ola --selftest          ctest entry (fast, no writes)
//   task28_ola --rtprobe [--out D] realtime feasibility probe
//   task28_ola --out D [--no-renders]  full deterministic evaluation
//                                      (writes candidate-report.json +
//                                      curated renders/)

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "prototypes/candidate_ola.h"
#include "prototypes/proto_run.h"

namespace {

pitchlab::proto::CandidateSpec olaSpec() {
  using pitchlab::proto::ProtoParams;
  using pitchlab::proto::CandidateSpec;
  CandidateSpec spec;
  spec.id = "ola";
  spec.family = "time-segment OLA + resampling";
  ProtoParams def;
  def["window_frames"] = 2048.0;
  def["overlap"] = 2.0;
  def["window_shape"] = std::string("hann");
  spec.defaultParams = def;

  auto withParams = [&def](ProtoParams extra) {
    ProtoParams p = def;
    for (const auto& [k, v] : extra) p[k] = v;
    return p;
  };
  spec.paramSweeps = {
      {"window-512", withParams({{"window_frames", 512.0}})},
      {"window-8192", withParams({{"window_frames", 8192.0}})},
      {"overlap-4", withParams({{"overlap", 4.0}})},
      {"overlap-8", withParams({{"overlap", 8.0}})},
      {"shape-rect", withParams({{"window_shape", std::string("rect")}})},
      {"shape-bartlett",
       withParams({{"window_shape", std::string("bartlett")}})},
      {"shape-hamming", withParams({{"window_shape", std::string("hamming")}})},
  };
  spec.make = [] {
    return std::unique_ptr<pitchlab::proto::ProtoEngine>(
        std::make_unique<pitchlab::proto::OlaPrototype>());
  };
  // Measured: the grain-rate FM/AM artifact family biases every frequency
  // estimator on shifted material (sine -12: projection 0.73 st, ZC 0.33 st
  // — the output's instantaneous frequency genuinely sawtooths at the
  // grain rate; see the report records). Identity is asserted at 0.1 st.
  spec.selftestShiftToleranceSt = 1.5;
  return spec;
}

}  // namespace

int main(int argc, char** argv) {
  const pitchlab::proto::CandidateSpec spec = olaSpec();
  std::string outDir;
  bool selftest = false;
  bool rtprobe = false;
  bool renders = true;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--selftest") == 0) {
      selftest = true;
    } else if (std::strcmp(argv[i], "--rtprobe") == 0) {
      rtprobe = true;
    } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      outDir = argv[++i];
    } else if (std::strcmp(argv[i], "--no-renders") == 0) {
      renders = false;
    }
  }
  if (selftest) {
    return pitchlab::proto::runCandidateSelftest(spec);
  }
  if (rtprobe) {
    return pitchlab::proto::runRtProbe(spec, outDir);
  }
  if (outDir.empty()) {
    std::cerr << "usage: task28_ola [--selftest | --rtprobe [--out DIR] | "
                 "--out DIR [--no-renders]]\n";
    return 2;
  }
  return pitchlab::proto::runCandidateEvaluation(spec, outDir, renders);
}
