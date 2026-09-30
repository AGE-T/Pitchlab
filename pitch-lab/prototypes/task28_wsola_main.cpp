// Pitch Lab — Task 28 Phase B entry point: proto.wsola evaluation driver.

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "prototypes/candidate_wsola.h"
#include "prototypes/proto_run.h"

namespace {

pitchlab::proto::CandidateSpec wsolaSpec() {
  using pitchlab::proto::ProtoParams;
  using pitchlab::proto::CandidateSpec;
  CandidateSpec spec;
  spec.id = "wsola";
  spec.family = "time-segment WSOLA + resampling";
  ProtoParams def;
  def["window_frames"] = 2048.0;
  def["overlap"] = 2.0;
  def["window_shape"] = std::string("hann");
  def["tolerance_frames"] = 768.0;
  spec.defaultParams = def;

  auto withParams = [&def](ProtoParams extra) {
    ProtoParams p = def;
    for (const auto& [k, v] : extra) p[k] = v;
    return p;
  };
  spec.paramSweeps = {
      {"tolerance-96", withParams({{"tolerance_frames", 96.0}})},
      {"tolerance-384", withParams({{"tolerance_frames", 384.0}})},
      {"tolerance-2048", withParams({{"tolerance_frames", 2048.0}})},
      {"window-512", withParams({{"window_frames", 512.0}})},
      {"window-8192", withParams({{"window_frames", 8192.0}})},
      {"overlap-4", withParams({{"overlap", 4.0}})},
      {"shape-rect", withParams({{"window_shape", std::string("rect")}})},
  };
  spec.make = [] {
    return std::unique_ptr<pitchlab::proto::ProtoEngine>(
        std::make_unique<pitchlab::proto::WsolaPrototype>());
  };
  // Measured evidence drives this (see the report): the similarity search
  // suppresses the OLA flutter family; the residual shifted-pitch bias
  // stays within the same gross-net class as OLA's.
  spec.selftestShiftToleranceSt = 1.5;
  return spec;
}

}  // namespace

int main(int argc, char** argv) {
  const pitchlab::proto::CandidateSpec spec = wsolaSpec();
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
    std::cerr << "usage: task28_wsola [--selftest | --rtprobe [--out DIR] | "
                 "--out DIR [--no-renders]]\n";
    return 2;
  }
  return pitchlab::proto::runCandidateEvaluation(spec, outDir, renders);
}
