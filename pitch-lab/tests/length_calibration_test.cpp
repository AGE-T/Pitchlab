// T-E14 / T-LEN-CAL — empirical output-length calibration for the
// rate-following reference engine (implementation specification §4.3.4 /
// §13.1 T-E14, cycle 3; the evidence pack consumed by OD-6).
//
// MEASUREMENT DESIGN (frozen §4.8.1 item 6): render native.varispeed over
// the FULL committed curve battery (21 curves, §11.3) x the six first-class
// sample rates (§8.1) and measure
//
//     actual output frames  vs  integrated-curve expectation (M0 + declared
//     outputLatency, replayed with the exact engine recurrence)
//
// per (curve, rate). The report prints expected / actual / absolute and
// relative difference, the worst cases, the per-rate worst, and the |delta|
// distribution. Input content is irrelevant to length (the curve + N_in
// determine it), so each rate uses a deterministic 0.5-second in-test sine
// asset — the corpus files are not required here.
//
// HONESTY RULES (§13.1 T-E14 row): evidence only — NO pass/fail on any
// calibrated value, and the observed maximum is NEVER silently promoted
// into config/tolerances.toml (OD-6 remains open until the owner ratifies;
// the test asserts the tolerance file was not touched). The only hard
// assertions are structural (all renders complete, all |delta| within the
// CURRENT provisional ±1-block tolerance — the same assertion T-E11 makes
// today — and flush within the declared bound).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "test_fixtures.h"

using namespace pitchlab;
using namespace pitchlab::test;
namespace fs = std::filesystem;

namespace {

struct CalibrationRow {
  std::string curveId;
  uint32_t sampleRate = 0;
  int64_t nIn = 0;
  int64_t expected = 0;
  int64_t actual = 0;
  int64_t delta = 0;
  double rel = 0.0;
  int64_t flush = 0;
  int64_t declaredOutLatency = 0;
};

const uint32_t kRates[6] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};

}  // namespace

TEST_CASE("T-LEN-CAL: varispeed output-length calibration evidence (OD-6)") {
  const std::string srcRoot = PITCHLAB_SOURCE_DIR;
  const fs::path batteryDir = fs::path(srcRoot) / "experiments" / "curves";
  REQUIRE(fs::exists(batteryDir));

  // Battery inventory (sorted — deterministic, mirrors loadCurveBattery).
  std::vector<fs::path> curveFiles;
  for (const auto& entry : fs::directory_iterator(batteryDir)) {
    if (entry.path().extension() == ".toml") curveFiles.push_back(entry.path());
  }
  std::sort(curveFiles.begin(), curveFiles.end());
  REQUIRE(curveFiles.size() == 21);

  TestRoot tr{"len-cal"};
  // Copy the committed battery into the test root (the compiler resolves
  // curves from <root>/experiments/curves).
  for (const fs::path& f : curveFiles) {
    fs::copy_file(f, tr.root / "experiments" / "curves" / f.filename(),
                  fs::copy_options::overwrite_existing);
  }

  EngineRegistry registry = TestRoot::productionRegistry();
  const HarnessConfig cfg = loadHarnessConfig(tr.root);
  const int64_t tolerance = cfg.lengthToleranceFrames;  // provisional 4096

  std::vector<CalibrationRow> rows;
  rows.reserve(curveFiles.size() * 6);

  for (uint32_t rate : kRates) {
    // 0.5-second deterministic sine asset at this rate.
    const std::string assetId = "cal-" + std::to_string(rate);
    const int64_t nIn = static_cast<int64_t>(rate) / 2;
    tr.makeAsset(assetId, rate, {sineFrames(440.0, rate, nIn)});

    for (const fs::path& f : curveFiles) {
      const std::string curveId = f.stem().string();
      const std::string expId = "cal-" + std::to_string(rate);
      const fs::path exp = tr.makeExperiment(
          expId, "id = \"" + expId + "\"\nkind = \"benchmark\"\n[suite]\ninputs  = [\"" + assetId +
                     "\"]\ncurves  = [\"" + curveId + "\"]\nengines = [\"native.varispeed\"]\n"
                     "sampleRates = [" +
                     std::to_string(rate) + "]\nchannels = [1]\n[[engine_config]]\nengine = "
                     "\"native.varispeed\"\nparams = { resample_quality = \"reference\" }\nseed = "
                     "0\n[output]\nmaster = false\nlistening = false\n");
      const CompileOutcome outcome = compileExperiment(exp, registry, tr.root, tr.artifacts, cfg);
      REQUIRE(outcome.jobs.size() == 1);
      REQUIRE(outcome.skips.empty());  // every battery curve is inside [0.0625, 16]
      const RenderSummary summary = renderJobs(outcome.jobs, registry);
      REQUIRE(summary.results.size() == 1);
      const JobResult& r = summary.results[0];
      REQUIRE(r.status == RenderStatus::Ok);

      CalibrationRow row;
      row.curveId = curveId;
      row.sampleRate = rate;
      row.nIn = nIn;
      row.expected = r.expectedOutputFrames;
      row.actual = r.outputFramesProduced;
      row.delta = r.lengthDeltaFrames;
      row.rel = r.expectedOutputFrames > 0
                    ? std::fabs(static_cast<double>(row.delta)) /
                          static_cast<double>(row.expected)
                    : 0.0;
      row.flush = r.flushFrames;
      row.declaredOutLatency = r.declaredLatency.outputLatencyFrames;
      rows.push_back(row);

      // Structural assertions (NOT calibrated-value assertions):
      CHECK(r.taints.empty());  // within the provisional tolerance
      CHECK(row.flush <= row.declaredOutLatency);
      CHECK(std::abs(row.delta) <= tolerance);
    }
  }

  REQUIRE(rows.size() == 126);

  // ---- The report (evidence, printed into the CTest/JUnit output) ----
  std::printf("\n================ T-LEN-CAL: OUTPUT-LENGTH CALIBRATION EVIDENCE ================\n");
  std::printf("engine: native.varispeed (reference preset K=24) | battery: 21 curves x 6 rates\n");
  std::printf("%-36s %8s %10s %10s %8s %12s %8s %8s\n", "curve", "fs", "N_in", "expected",
              "actual", "|delta|", "rel", "flush/L");
  std::printf("--------------------------------------------------------------------------------\n");
  for (const CalibrationRow& r : rows) {
    std::printf("%-36s %8u %10lld %10lld %8lld %12lld %8.2e %6lld/%lld\n", r.curveId.c_str(),
                r.sampleRate, static_cast<long long>(r.nIn), static_cast<long long>(r.expected),
                static_cast<long long>(r.actual),
                static_cast<long long>(std::abs(r.delta)), r.rel,
                static_cast<long long>(r.flush), static_cast<long long>(r.declaredOutLatency));
  }

  // Worst cases + distribution.
  const CalibrationRow* worstAbs = &rows[0];
  const CalibrationRow* worstRel = &rows[0];
  std::map<int64_t, int> histogram;
  for (const CalibrationRow& r : rows) {
    if (std::abs(r.delta) > std::abs(worstAbs->delta)) worstAbs = &r;
    if (r.rel > worstRel->rel) worstRel = &r;
    int64_t bucket = std::abs(r.delta);
    if (bucket > 64) bucket = 65;  // final bucket ">64"
    ++histogram[bucket];
  }
  std::printf("--------------------------------------------------------------------------------\n");
  std::printf("worst |delta| : %lld frames (%s @ %u Hz; expected %lld, actual %lld)\n",
              static_cast<long long>(std::abs(worstAbs->delta)), worstAbs->curveId.c_str(),
              worstAbs->sampleRate, static_cast<long long>(worstAbs->expected),
              static_cast<long long>(worstAbs->actual));
  std::printf("worst rel     : %.3e (%s @ %u Hz)\n", worstRel->rel, worstRel->curveId.c_str(),
              worstRel->sampleRate);
  std::printf("|delta| distribution over 126 renders:\n");
  for (const auto& [bucket, count] : histogram) {
    if (bucket <= 64) {
      std::printf("  |delta| = %lld : %d case(s)\n", static_cast<long long>(bucket), count);
    } else {
      std::printf("  |delta| > 64 : %d case(s)\n", count);
    }
  }
  std::printf("per-rate worst |delta|:\n");
  for (uint32_t rate : kRates) {
    int64_t worst = 0;
    for (const CalibrationRow& r : rows) {
      if (r.sampleRate == rate) worst = std::max(worst, std::abs(r.delta));
    }
    std::printf("  %u Hz : %lld frames\n", rate, static_cast<long long>(worst));
  }
  std::printf("OD-6 STATUS: OPEN — this test REPORTS evidence; it does NOT promote any observed\n");
  std::printf("value into config/tolerances.toml (the provisional +/-1 block (4096) remains the\n");
  std::printf("normative-provisional tolerance until the owner ratifies from this evidence).\n");
  std::printf("================================================================================\n\n");

  // Honesty check: the REPO's tolerance file was NOT modified by this
  // calibration (no silent promotion of an observed value).
  const HarnessConfig repoCfg = loadHarnessConfig(fs::path(srcRoot));
  CHECK(repoCfg.lengthToleranceFrames == 4096);
  CHECK(tolerance == 4096);  // the provisional value, still in force
}
