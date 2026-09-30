// Pitch Lab — Task 28 Phase E entry point: proto.pvtransient driver.

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "prototypes/candidate_pvtransient.h"
#include "prototypes/proto_run.h"

namespace {

pitchlab::proto::CandidateSpec pvtransientSpec() {
  using pitchlab::proto::ProtoParams;
  using pitchlab::proto::CandidateSpec;
  CandidateSpec spec;
  spec.id = "pvtransient";
  spec.family = "spectral transient-aware phase vocoder";
  ProtoParams def;
  def["window_frames"] = 2048.0;
  def["overlap"] = 4.0;  // 75% PV overlap
  def["window_shape"] = std::string("hann");
  def["reset_mode"] = std::string("full");
  def["sensitivity"] = 2.5;
  spec.defaultParams = def;

  auto mk = [&def](ProtoParams extra) {
    ProtoParams p = def;
    for (const auto& [k, v] : extra) p[k] = v;
    return p;
  };
  spec.paramSweeps = {
      // THE Phase-E evidence axis: what the reset buys (off vs full vs
      // band) on the transient materials.
      {"reset-off", mk({{"reset_mode", std::string("off")}})},
      {"reset-band", mk({{"reset_mode", std::string("band")}})},
      {"sens-1.5", mk({{"sensitivity", 1.5}})},
      {"sens-5", mk({{"sensitivity", 5.0}})},
      {"window-4096", mk({{"window_frames", 4096.0}})},
      {"overlap-2", mk({{"overlap", 2.0}})},
  };
  spec.make = [] {
    return std::unique_ptr<pitchlab::proto::ProtoEngine>(
        std::make_unique<pitchlab::proto::PvTransientPrototype>());
  };
  // PV-family selftest semantics: classic PV shifts cleanly (the dominant
  // metric applies); tolerance per the family's measured bias.
  spec.selftestShiftToleranceSt = 0.75;
  return spec;
}

}  // namespace

int main(int argc, char** argv) {
  const pitchlab::proto::CandidateSpec spec = pvtransientSpec();
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
    std::cerr << "usage: task28_pvtransient [--selftest | --rtprobe [--out DIR] "
                 "| --out DIR [--no-renders]]\n";
    return 2;
  }
  return pitchlab::proto::runCandidateEvaluation(spec, outDir, renders);
}
