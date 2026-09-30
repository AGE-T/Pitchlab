// Pitch Lab — Task 28 Phase D entry point: proto.fdpsola evaluation driver.

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "prototypes/candidate_fdpsola.h"
#include "prototypes/proto_run.h"

namespace {

pitchlab::proto::CandidateSpec fdpsolaSpec() {
  using pitchlab::proto::ProtoParams;
  using pitchlab::proto::CandidateSpec;
  CandidateSpec spec;
  spec.id = "fdpsola";
  spec.family = "frequency-domain PSOLA (grain-spectral envelope control)";
  ProtoParams def;
  def["mode"] = std::string("pitch");
  def["puv_hz"] = 200.0;
  def["formant_ratio"] = 1.0;
  spec.defaultParams = def;

  auto withParams = [&def](ProtoParams extra) {
    ProtoParams p = def;
    for (const auto& [k, v] : extra) p[k] = v;
    return p;
  };
  spec.paramSweeps = {
      // The formant-control evidence axis: gamma at a FIXED pitch ratio.
      {"gamma-0.5", withParams({{"formant_ratio", 0.5}})},
      {"gamma-1.5", withParams({{"formant_ratio", 1.5}})},
      {"gamma-2.0", withParams({{"formant_ratio", 2.0}})},
      // The formant-ONLY capability: identity pitch + gamma != 1 (needs
      // an identity-ratio run — covered by the identity sweep on the
      // +12 pitch-question materials; see the report).
      {"gamma-2.0-stretch", withParams({{"formant_ratio", 2.0},
                                        {"mode", std::string("stretch")}})},
  };
  spec.make = [] {
    return std::unique_ptr<pitchlab::proto::ProtoEngine>(
        std::make_unique<pitchlab::proto::FdPsolaPrototype>());
  };
  // Same selftest semantics as TD-PSOLA: the dominant metric does not
  // apply (formant-preserving mode); identity exactness (0.1 st) covers
  // every material; the F0 net covers the pulse-bearing vocal.
  spec.selftestShiftToleranceSt = 30.0;
  spec.selftestF0ToleranceSt = 0.5;
  spec.selftestF0Materials = {"vocal"};
  spec.selftestMinFramesRatio = 0.4;
  return spec;
}

}  // namespace

int main(int argc, char** argv) {
  const pitchlab::proto::CandidateSpec spec = fdpsolaSpec();
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
    std::cerr << "usage: task28_fdpsola [--selftest | --rtprobe [--out DIR] | "
                 "--out DIR [--no-renders]]\n";
    return 2;
  }
  return pitchlab::proto::runCandidateEvaluation(spec, outDir, renders);
}
