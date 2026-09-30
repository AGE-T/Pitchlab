// Pitch Lab — Task 28 existing-engine differentiation baseline driver.
// Renders the FIVE frozen production engines (default configurations)
// over the baseline matrix and emits the shared measurement records.
//
// Modes:
//   task28_baseline --out D    write baseline/existing-engines.json
//                              (+ baseline/renders/<engine>_<mat>_<tr>.wav)
//   task28_baseline --selftest ctest entry (render + integrity assert)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "core/types.h"
#include "core/wav_io.h"
#include "harness/json_writer.h"
#include "prototypes/proto_baseline.h"
#include "prototypes/proto_corpus.h"
#include "prototypes/proto_curves.h"
#include "prototypes/proto_measure.h"

namespace fs = std::filesystem;
using namespace pitchlab::proto;
using pitchlab::json::Value;
using pitchlab::json::Object;
using pitchlab::json::Array;

namespace {

pitchlab::json::Value fin(double v) {
  if (std::isfinite(v)) return pitchlab::json::Value(v);
  return pitchlab::json::Value(-999.0);
}

pitchlab::json::Value metricsToJson(const WaveMetrics& m) {
  Object o;
  o["am_depth"] = fin(m.amDepth);
  o["am_rate_hz"] = fin(m.amRateHz);
  o["comb_ripple_db"] = fin(m.combRippleDb);
  o["corr_coef"] = fin(m.corrCoef);
  o["dominant_hz"] = fin(m.dominantHz);
  o["dominant_zc_hz"] = fin(m.dominantZcHz);
  o["f0_tracker_hz"] = fin(m.f0TrackerHz);
  o["finite"] = Value(m.finite);
  o["frames"] = Value(static_cast<int64_t>(m.frames));
  o["onset_count"] = Value(static_cast<int64_t>(m.onsetCount));
  o["onset_sharp_db"] = fin(m.onsetSharpDb);
  o["onset_tail_db"] = fin(m.onsetTailDb);
  o["peak_dbfs"] = fin(m.peakDbfs);
  o["rms_dbfs"] = fin(m.rmsDbfs);
  o["rms_ratio"] = fin(m.rmsRatio);
  o["silent"] = Value(m.silent);
  o["stereo_corr"] = fin(m.stereoCorr);
  o["centroid_hz"] = fin(m.centroidHz);
  o["tracker_voiced_ratio"] = fin(m.trackerVoicedRatio);
  return Value(std::move(o));
}

}  // namespace

int main(int argc, char** argv) {
  std::string outDir;
  bool selftest = false;
  bool renders = true;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--selftest") == 0) {
      selftest = true;
    } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      outDir = argv[++i];
    } else if (std::strcmp(argv[i], "--no-renders") == 0) {
      renders = false;
    }
  }

  Array records;
  int failures = 0;
  const bool reduced = selftest;
  const std::vector<std::string> mats =
      reduced ? std::vector<std::string>{"sine", "drum", "vocal"}
              : std::vector<std::string>{};
  const std::vector<std::string>& allMats =
      reduced ? mats : []() -> const std::vector<std::string>& {
        static const std::vector<std::string> ids = [] {
          std::vector<std::string> v;
          for (const Material& m : corpusMaterials()) v.push_back(m.id);
          return v;
        }();
        return ids;
      }();

  for (const std::string& engineId : baselineEngineIds()) {
    for (const std::string& matId : allMats) {
      for (const std::string& trId : baselineTransformIds()) {
        BaselineRecord rec =
            renderBaseline(engineId, matId, trId);
        Object o;
        o["engine"] = Value(engineId);
        o["material"] = Value(matId);
        o["transform"] = Value(trId);
        o["frames_out"] = Value(static_cast<int64_t>(rec.frames));
        if (!rec.ok) {
          o["status"] = Value("FAIL");
          o["error"] = Value(rec.error);
          ++failures;
          records.push_back(Value(std::move(o)));
          continue;
        }
        const Material* mat = findMaterial(matId);
        const WaveMetrics m = measureWave(rec.out, mat->fs, mat->ch);
        o["status"] = Value("PASS");
        o["metrics"] = metricsToJson(m);
        o["det_hash"] = Value(waveHashHex(rec.out));
        if (!m.finite || m.silent) ++failures;

        // Self-referenced pitch response (tonal static transforms).
        const Transform* tr = findTransform(trId);
        const WaveMetrics inMetrics = measureWave(mat->ch, mat->fs);
        if (tr->staticRatio && mat->tonal && inMetrics.dominantHz > 0.0 &&
            m.dominantHz > 0.0) {
          const double expected = inMetrics.dominantHz * tr->staticValue;
          o["dominant_err_st"] =
              Value(12.0 * std::log2(m.dominantHz / expected));
        } else {
          o["dominant_err_st"] = Value(nullptr);
        }
        records.push_back(Value(std::move(o)));

        std::cout << "  " << engineId << " " << matId << "/" << trId
                  << ": frames " << rec.frames << " ok\n";

        // Curated renders (drum/vocal +12/-12, float32).
        if (!selftest && renders && (matId == "drum" || matId == "vocal") &&
            (trId == "plus12" || trId == "minus12")) {
          const fs::path wav = fs::path(outDir) / "baseline" / "renders" /
                               (engineId + "_" + matId + "_" + trId + ".wav");
          fs::create_directories(wav.parent_path());
          std::vector<const double*> ptrs;
          for (const auto& c : rec.out) ptrs.push_back(c.data());
          pitchlab::writeWav(wav, ptrs.data(),
                             static_cast<pitchlab::ChannelCount>(rec.out.size()),
                             rec.frames, static_cast<uint32_t>(mat->fs),
                             pitchlab::WavSampleFormat::Float32);
        }
      }
    }
  }

  if (!selftest) {
    Object report;
    report["note"] = Value(
        "frozen v0.1 production engines, default configurations, driven "
        "through the real PitchEngine contract (read-only reuse; registry "
        "untouched); baseline matrix identity/plus12/minus12/ramp-fast");
    report["records"] = Value(std::move(records));
    const fs::path p = fs::path(outDir) / "baseline" / "existing-engines.json";
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << pitchlab::json::serialize(Value(std::move(report))) << "\n";
    std::cout << "task28 baseline -> " << p.string() << " failures: "
              << failures << "\n";
  }
  return failures == 0 ? 0 : 1;
}
