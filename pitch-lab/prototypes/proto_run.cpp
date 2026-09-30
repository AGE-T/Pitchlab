// Pitch Lab — Task 28 shared prototype runners (see proto_run.h).

#include "prototypes/proto_run.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "core/wav_io.h"
#include "harness/json_writer.h"
#include "prototypes/proto_alloc.h"
#include "prototypes/proto_curves.h"
#include "prototypes/proto_corpus.h"
#include "prototypes/proto_measure.h"

namespace pitchlab::proto {
namespace fs = std::filesystem;

namespace {

// Finite-guard for the record boundary: the canonical JSON writer prints
// non-finite doubles as inf/nan tokens (invalid JSON); every metric field
// passes through here. -999.0 is the shared "not meaningful" sentinel.
[[nodiscard]] double fin(double v) {
  if (std::isfinite(v)) return v;
  return -999.0;
}

json::Value metricsToJson(const WaveMetrics& m) {
  json::Object o;
  o["am_depth"] = json::Value(fin(m.amDepth));
  o["am_rate_hz"] = json::Value(fin(m.amRateHz));
  o["comb_ripple_db"] = json::Value(fin(m.combRippleDb));
  o["corr_coef"] = json::Value(fin(m.corrCoef));
  o["dominant_hz"] = json::Value(fin(m.dominantHz));
  o["dominant_zc_hz"] = json::Value(fin(m.dominantZcHz));
  o["f0_tracker_hz"] = json::Value(fin(m.f0TrackerHz));
  o["finite"] = json::Value(m.finite);
  o["frames"] = json::Value(static_cast<int64_t>(m.frames));
  o["onset_count"] = json::Value(static_cast<int64_t>(m.onsetCount));
  o["onset_sharp_db"] = json::Value(fin(m.onsetSharpDb));
  o["onset_tail_db"] = json::Value(fin(m.onsetTailDb));
  o["peak_dbfs"] = json::Value(fin(m.peakDbfs));
  o["rms_dbfs"] = json::Value(fin(m.rmsDbfs));
  o["rms_ratio"] = json::Value(fin(m.rmsRatio));
  o["silent"] = json::Value(m.silent);
  o["stereo_corr"] = json::Value(fin(m.stereoCorr));
  o["centroid_hz"] = json::Value(fin(m.centroidHz));
  o["tracker_voiced_ratio"] = json::Value(fin(m.trackerVoicedRatio));
  return json::Value(std::move(o));
}

json::Value paramsToJson(const ProtoParams& p) {
  json::Object o;
  for (const auto& [k, v] : p) {
    if (const auto* d = std::get_if<double>(&v)) {
      o[k] = json::Value(*d);
    } else if (const auto* s = std::get_if<std::string>(&v)) {
      o[k] = json::Value(*s);
    } else if (const auto* b = std::get_if<bool>(&v)) {
      o[k] = json::Value(*b);
    } else if (const auto* i = std::get_if<int64_t>(&v)) {
      o[k] = json::Value(*i);
    }
  }
  return json::Value(std::move(o));
}

struct RecordOutcome {
  bool ok = false;
  json::Object record;
  std::string summaryLine;
  double dominantErrSt = 0.0;  // valid when measurable
};

RecordOutcome evaluateOne(const CandidateSpec& spec, const Material& mat,
                          const Transform& tr, const ProtoParams& params,
                          int maxBlock) {
  RecordOutcome out;
  const FrameCount nIn = static_cast<FrameCount>(mat.ch[0].size());
  const std::vector<double> curve = tr.build(nIn, mat.fs);

  auto runOnce = [&]() {
    std::unique_ptr<ProtoEngine> engine = spec.make();
    engine->configure(params);
    engine->analyzeSignal(mat.ch);
    DriveResult r = driveEngine(*engine, mat.ch, curve.data(), nIn, mat.fs,
                                maxBlock);
    json::Value diag = engine->diagnostics();
    return std::make_pair(std::move(r), std::move(diag));
  };

  auto [res, diag] = runOnce();
  out.record["candidate"] = json::Value(spec.id);
  out.record["family"] = json::Value(spec.family);
  out.record["material"] = json::Value(mat.id);
  out.record["transform"] = json::Value(tr.id);
  out.record["params"] = paramsToJson(params);
  out.record["frames_in"] = json::Value(static_cast<int64_t>(nIn));

  if (!res.ok) {
    out.record["status"] = json::Value("FAIL");
    out.record["error"] = json::Value(res.error);
    out.summaryLine = "  FAIL " + mat.id + "/" + tr.id + ": " + res.error;
    return out;
  }

  // Determinism double-run (fresh instance, bit-identical).
  auto [res2, diag2] = runOnce();
  const bool deterministic =
      res2.ok && waveHashHex(res.out) == waveHashHex(res2.out) &&
      pitchlab::json::serialize(diag) == pitchlab::json::serialize(diag2);
  out.record["deterministic"] = json::Value(deterministic);

  const WaveMetrics m = measureWave(res.out, mat.fs, mat.ch);
  out.record["status"] = json::Value("PASS");
  out.record["frames_out"] = json::Value(static_cast<int64_t>(res.frames));
  out.record["blocks"] = json::Value(static_cast<int64_t>(res.blocks));
  out.record["flushed"] = json::Value(static_cast<int64_t>(res.flushed));
  out.record["metrics"] = metricsToJson(m);
  out.record["det_hash"] = json::Value(waveHashHex(res.out));
  out.record["engine_diagnostics"] = diag;

  // Pitch response (self-referenced expected dominant).
  const WaveMetrics inMetrics = measureWave(mat.ch, mat.fs);
  if (tr.staticRatio && mat.tonal && inMetrics.dominantHz > 0.0 &&
      m.dominantHz > 0.0) {
    const double expected = inMetrics.dominantHz * tr.staticValue;
    out.dominantErrSt = 12.0 * std::log2(m.dominantHz / expected);
    out.record["dominant_err_st"] = json::Value(out.dominantErrSt);
    out.record["expected_dominant_hz"] = json::Value(expected);
  } else {
    out.record["dominant_err_st"] = json::Value(nullptr);
  }
  // Ramp endpoints (dynamic-pitch tracking evidence).
  if (!tr.staticRatio && m.f0TrackerHz > 0.0) {
    const F0Summary f = f0Summary(res.out[0], mat.fs);
    out.record["f0_out_start_hz"] = json::Value(f.startHz);
    out.record["f0_out_end_hz"] = json::Value(f.endHz);
  } else {
    out.record["f0_out_start_hz"] = json::Value(nullptr);
    out.record["f0_out_end_hz"] = json::Value(nullptr);
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "  " << mat.id << "/" << tr.id << ": frames " << res.frames
      << "  dom_err "
      << (out.record["dominant_err_st"].isNull()
              ? std::string("  --  ")
              : [&] {
                  std::ostringstream e;
                  e << std::fixed << std::setprecision(2)
                    << out.dominantErrSt << " st";
                  return e.str();
                }())
      << "  rms_r " << m.rmsRatio << "  am " << m.amDepth
      << "  comb " << m.combRippleDb << " dB  sharp "
      << m.onsetSharpDb << " dB";
  out.summaryLine = oss.str();
  out.ok = m.finite && !m.silent && deterministic;
  if (!m.finite) out.summaryLine += "  [NON-FINITE]";
  if (m.silent) out.summaryLine += "  [SILENT]";
  if (!deterministic) out.summaryLine += "  [NON-DETERMINISTIC]";
  return out;
}

void writeJsonFile(const fs::path& path, const json::Value& v) {
  fs::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary);
  f << json::serialize(v) << "\n";
}

const std::vector<std::string>& renderMaterials() {
  static const std::vector<std::string> v = {"drum", "vocal", "metallic"};
  return v;
}
const std::vector<std::string>& renderTransforms() {
  static const std::vector<std::string> v = {"plus12", "minus12"};
  return v;
}

}  // namespace

int runCandidateEvaluation(const CandidateSpec& spec,
                           const std::string& outDir, bool writeRenders) {
  const fs::path base = fs::path(outDir) / spec.id;
  json::Array records;

  std::cout << "task28 evaluation: " << spec.id << " (" << spec.family
            << ")\n";
  int failures = 0;

  // Full matrix: 12 materials x 10 transforms (default parameters).
  for (const Material& mat : corpusMaterials()) {
    for (const Transform& tr : transforms()) {
      RecordOutcome r =
          evaluateOne(spec, mat, tr, spec.defaultParams, 1024);
      std::cout << r.summaryLine << "\n";
      if (!r.record["status"].asString().empty() &&
          r.record["status"].asString() == "FAIL") {
        ++failures;
      } else if (!r.ok) {
        ++failures;
      }
      records.push_back(json::Value(std::move(r.record)));
    }
  }

  // Parameter-effect matrix: 2 materials x +12 st per sweep config.
  std::cout << "task28 parameter-effect matrix: " << spec.id << "\n";
  for (const auto& [label, params] : spec.paramSweeps) {
    for (const char* matId : {"harmstack", "drum"}) {
      const Material* mat = findMaterial(matId);
      const Transform* tr = findTransform("plus12");
      RecordOutcome r = evaluateOne(spec, *mat, *tr, params, 1024);
      json::Object rec = std::move(r.record);
      rec["param_label"] = json::Value(label);
      std::cout << r.summaryLine << "  [" << label << "]\n";
      if (rec["status"].asString() == "FAIL" || !r.ok) ++failures;
      records.push_back(json::Value(std::move(rec)));
    }
  }

  json::Object report;
  report["candidate"] = json::Value(spec.id);
  report["family"] = json::Value(spec.family);
  report["records"] = json::Value(std::move(records));
  report["record_count"] =
      json::Value(static_cast<int64_t>(report["records"].asArray().size()));
  report["corpus_note"] = json::Value(
      "12 deterministic materials (proto_corpus), 10 transforms "
      "(proto_curves), 48 kHz, default + parameter-effect configs");
  report["failure_count"] = json::Value(static_cast<int64_t>(failures));
  writeJsonFile(base / "candidate-report.json", json::Value(std::move(report)));

  // Curated listening renders (float32).
  if (writeRenders) {
    for (const std::string& matId : renderMaterials()) {
      for (const std::string& trId : renderTransforms()) {
        const Material* mat = findMaterial(matId);
        const Transform* tr = findTransform(trId);
        const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
        const std::vector<double> curve = tr->build(nIn, mat->fs);
        std::unique_ptr<ProtoEngine> engine = spec.make();
        engine->configure(spec.defaultParams);
        DriveResult res =
            driveEngine(*engine, mat->ch, curve.data(), nIn, mat->fs, 1024);
        if (!res.ok) continue;
        const fs::path wav = base / "renders" / (matId + "_" + trId + ".wav");
        fs::create_directories(wav.parent_path());
        std::vector<const double*> ptrs;
        for (const auto& c : res.out) ptrs.push_back(c.data());
        writeWav(wav, ptrs.data(), static_cast<ChannelCount>(res.out.size()),
                 res.frames, static_cast<uint32_t>(mat->fs),
                 WavSampleFormat::Float32);
      }
    }
  }

  std::cout << "task28 evaluation: " << spec.id << " -> "
            << (base / "candidate-report.json").string() << "  failures: "
            << failures << "\n";
  return failures == 0 ? 0 : 1;
}

int runCandidateSelftest(const CandidateSpec& spec) {
  int failures = 0;
  const std::vector<std::string> mats = {"sine", "drum", "vocal"};
  const std::vector<std::string> trs = {
      "identity", "plus12", "minus12", "ramp-fast"};

  std::cout << "task28 selftest: " << spec.id << "\n";
  for (const std::string& matId : mats) {
    const Material* mat = findMaterial(matId);
    const WaveMetrics inMetrics = measureWave(mat->ch, mat->fs);
    const double m_f0In = inMetrics.f0TrackerHz;
    for (const std::string& trId : trs) {
      const Transform* tr = findTransform(trId);
      RecordOutcome r =
          evaluateOne(spec, *mat, *tr, spec.defaultParams, 1024);
      const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
      const json::Value& status = r.record.count("status") != 0
                                      ? r.record.at("status")
                                      : json::Value();
      if (status.kind() == json::Value::Kind::String &&
          status.asString() == "FAIL") {
        std::cout << "  FAIL " << matId << "/" << trId << ": "
                  << r.summaryLine << "\n";
        ++failures;
        continue;
      }
      const json::Object& rec = r.record;
      if (!r.ok) {
        std::cout << r.summaryLine << "\n";
        ++failures;
        continue;
      }
      const int64_t framesOut = rec.at("frames_out").asInt();
      if (framesOut < static_cast<int64_t>(spec.selftestMinFramesRatio *
                                           static_cast<double>(nIn))) {
        std::cout << "  FAIL " << matId << "/" << trId
                  << ": frames_out " << framesOut << " < "
                  << spec.selftestMinFramesRatio << " x " << nIn
                  << "\n";
        ++failures;
        continue;
      }
      // Pitch response on tonal static transforms: identity is asserted
      // TIGHTLY (the implementation invariant); shifted ratios use the
      // candidate's documented gross tolerance (see CandidateSpec).
      const double tol =
          trId == "identity" ? 0.1 : spec.selftestShiftToleranceSt;
      const json::Value& errSt = rec.at("dominant_err_st");
      if (mat->tonal && tr->staticRatio &&
          errSt.kind() != json::Value::Kind::Null) {
        if (std::abs(errSt.asDouble()) > tol) {
          std::cout << "  FAIL " << matId << "/" << trId
                    << ": dominant error " << errSt.asDouble()
                    << " st exceeds " << tol << " st\n";
          ++failures;
          continue;
        }
      }
      // F0-tracking assertion (formant-preserving candidates: the pitch
      // evidence is the fundamental, not the strongest partial).
      bool f0Material = false;
      for (const std::string& fm : spec.selftestF0Materials) {
        if (fm == matId) f0Material = true;
      }
      if (spec.selftestF0ToleranceSt > 0.0 && f0Material && tr->staticRatio &&
          m_f0In > 0.0) {
        const double expectedF0 = m_f0In * tr->staticValue;
        const json::Value& f0v = rec.count("metrics") != 0
                                     ? rec.at("metrics").asObject().count(
                                           "f0_tracker_hz") != 0
                                           ? rec.at("metrics").asObject().at(
                                                 "f0_tracker_hz")
                                           : json::Value()
                                     : json::Value();
        if (f0v.kind() == json::Value::Kind::Double && f0v.asDouble() > 0.0) {
          const double errF0 = 12.0 * std::log2(f0v.asDouble() / expectedF0);
          if (std::abs(errF0) > spec.selftestF0ToleranceSt) {
            std::cout << "  FAIL " << matId << "/" << trId
                      << ": f0 error " << errF0 << " st (tracker "
                      << f0v.asDouble() << " vs expected " << expectedF0
                      << ") exceeds " << spec.selftestF0ToleranceSt << " st\n";
            ++failures;
            continue;
          }
        }
      }
    }
  }

  // Allocation audit: zero allocations across prepare->process->finish
  // (prepare excluded: allocation is its job).
  {
    const Material* mat = findMaterial("vocal");
    const Transform* tr = findTransform("plus12");
    const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
    const std::vector<double> curve = tr->build(nIn, mat->fs);
    std::unique_ptr<ProtoEngine> engine = spec.make();
    engine->configure(spec.defaultParams);
    engine->analyzeSignal(mat->ch);
    engine->prepare(mat->fs, mat->channels, 256, nIn, curve.data());
    // drive with block 256, auditing the process/finish path only
    DriveResult res;
    {
      std::vector<std::vector<double>> staging(
          static_cast<std::size_t>(mat->channels),
          std::vector<double>(8192, 0.0));
      std::vector<const double*> inPtrs;
      std::vector<double*> outPtrs;
      for (int c = 0; c < mat->channels; ++c) {
        inPtrs.push_back(mat->ch[static_cast<std::size_t>(c)].data());
        outPtrs.push_back(staging[static_cast<std::size_t>(c)].data());
      }
      const ProtoEngine::Latency lat = engine->latency();
      const FrameCount streamEnd = nIn + lat.inputLookahead;
      FrameCount pos = 0;
      FrameCount produced = 0;
      // Pre-reserved so the AUDIT region (process/finish) contains no
      // driver-side growth: the audit counts every allocation in the
      // process, including the driver's own plumbing.
      std::vector<std::vector<double>> accum(
          static_cast<std::size_t>(mat->channels));
      for (auto& a : accum) {
        a.reserve(static_cast<std::size_t>(
            nIn + lat.outputFlush + lat.inputLookahead + 8192));
      }
      std::vector<double> zeroPad(
          static_cast<std::size_t>(lat.inputLookahead), 0.0);
      allocAuditReset();
      while (pos < streamEnd) {
        FrameCount take = std::min<FrameCount>(256, streamEnd - pos);
        if (pos < nIn) take = std::min(take, nIn - pos);
        AudioBlockView view;
        view.channelCount = mat->channels;
        view.frameCount = take;
        AudioBlockOut outRegion;
        outRegion.channels = outPtrs.data();
        outRegion.channelCount = mat->channels;
        outRegion.frameCapacity = 8192;
        // advance the input views (they point into the material / pad)
        for (int c = 0; c < mat->channels; ++c) {
          inPtrs[static_cast<std::size_t>(c)] =
              pos < nIn
                  ? mat->ch[static_cast<std::size_t>(c)].data() + pos
                  : zeroPad.data() + (pos - nIn);
        }
        view.channels = inPtrs.data();
        ProcessOutcome rep =
            engine->process(view, static_cast<int>(take), outRegion, 8192,
                            pos);
        for (int c = 0; c < mat->channels; ++c) {
          accum[static_cast<std::size_t>(c)].insert(
              accum[static_cast<std::size_t>(c)].end(),
              staging[static_cast<std::size_t>(c)].begin(),
              staging[static_cast<std::size_t>(c)].begin() +
                  static_cast<std::ptrdiff_t>(rep.outputFramesProduced));
        }
        produced += rep.outputFramesProduced;
        pos += rep.inputFramesConsumed;
        if (rep.inputFramesConsumed == 0) break;
      }
      AudioBlockOut outRegion;
      outRegion.channels = outPtrs.data();
      outRegion.channelCount = mat->channels;
      outRegion.frameCapacity = 8192;
      ProcessOutcome rep = engine->finish(outRegion, 8192);
      for (int c = 0; c < mat->channels; ++c) {
        accum[static_cast<std::size_t>(c)].insert(
            accum[static_cast<std::size_t>(c)].end(),
            staging[static_cast<std::size_t>(c)].begin(),
            staging[static_cast<std::size_t>(c)].begin() +
                static_cast<std::ptrdiff_t>(rep.outputFramesProduced));
      }
      const int64_t allocs = allocAuditCount();
      res.ok = true;
      res.out = std::move(accum);
      res.frames = produced + rep.outputFramesProduced;
      if (allocs != 0) {
        std::cout << "  FAIL allocation audit: " << allocs
                  << " allocations in the process/finish path\n";
        ++failures;
      } else {
        std::cout << "  allocation audit: 0 allocations in the "
                     "process/finish path\n";
      }
    }
    // Block-split invariance: block 1024 vs block 256 (both audited
    // drivers already passed; compare against the standard driver).
    std::unique_ptr<ProtoEngine> e2 = spec.make();
    e2->configure(spec.defaultParams);
    DriveResult whole = driveEngine(*e2, mat->ch, curve.data(), nIn, mat->fs,
                                    1024);
    if (!whole.ok || waveHashHex(whole.out) != waveHashHex(res.out)) {
      std::cout << "  FAIL block-split invariance (256 vs 1024)\n";
      ++failures;
    } else {
      std::cout << "  block-split invariance: bit-identical (256 vs 1024)\n";
    }
  }

  // Worst-block pathology guard (generous margin: 20x the block budget).
  {
    const Material* mat = findMaterial("drum");
    const Transform* tr = findTransform("plus12");
    const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
    const std::vector<double> curve = tr->build(nIn, mat->fs);
    std::unique_ptr<ProtoEngine> engine = spec.make();
    engine->configure(spec.defaultParams);
    engine->analyzeSignal(mat->ch);
    engine->prepare(mat->fs, mat->channels, 256, nIn, curve.data());
    const ProtoEngine::Latency lat = engine->latency();
    const FrameCount streamEnd = nIn + lat.inputLookahead;
    std::vector<std::vector<double>> staging(1, std::vector<double>(16384, 0.0));
    const double* inPtr = mat->ch[0].data();
    double* outPtr = staging[0].data();
    std::vector<double> zeroPad(static_cast<std::size_t>(lat.inputLookahead),
                                0.0);
    FrameCount pos = 0;
    double worstMs = 0.0;
    while (pos < streamEnd) {
      FrameCount take = std::min<FrameCount>(256, streamEnd - pos);
      if (pos < nIn) take = std::min(take, nIn - pos);
      AudioBlockView view;
      view.channels = &inPtr;
      view.channelCount = 1;
      view.frameCount = take;
      AudioBlockOut outRegion;
      outRegion.channels = &outPtr;
      outRegion.channelCount = 1;
      outRegion.frameCapacity = 16384;
      inPtr = pos < nIn ? mat->ch[0].data() + pos : zeroPad.data() + (pos - nIn);
      const auto t0 = std::chrono::steady_clock::now();
      ProcessOutcome rep =
          engine->process(view, static_cast<int>(take), outRegion, 16384, pos);
      const auto t1 = std::chrono::steady_clock::now();
      worstMs = std::max(
          worstMs,
          std::chrono::duration<double, std::milli>(t1 - t0).count());
      pos += rep.inputFramesConsumed;
      if (rep.inputFramesConsumed == 0) break;
    }
    const double budgetMs = 1000.0 * 256.0 / mat->fs;
    if (worstMs > 20.0 * budgetMs) {
      std::cout << "  FAIL worst block " << worstMs << " ms > 20x budget "
                << budgetMs << " ms\n";
      ++failures;
    } else {
      std::cout << "  worst block: " << worstMs << " ms (budget "
                << budgetMs << " ms, 20x guard)\n";
    }
  }

  const bool pass = failures == 0;
  std::cout << "task28 selftest: " << spec.id
            << (pass ? " ALL CHECKS PASS" : " FAILURES") << "\n";
  return pass ? 0 : 1;
}

int runRtProbe(const CandidateSpec& spec, const std::string& outDir) {
  const std::vector<int> blockSizes = {128, 256, 1024};
  const std::vector<std::string> matIds = {"harmstack", "drum"};
  const std::vector<std::string> trIds = {"plus12", "ramp-fast",
                                          "reversal-100ms"};

  json::Array runs;
  int failures = 0;
  std::cout << "task28 rtprobe: " << spec.id << "\n";

  for (int bs : blockSizes) {
    for (const std::string& matId : matIds) {
      for (const std::string& trId : trIds) {
        const Material* mat = findMaterial(matId);
        const Transform* tr = findTransform(trId);
        const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
        const std::vector<double> curve = tr->build(nIn, mat->fs);

        std::unique_ptr<ProtoEngine> engine = spec.make();
        engine->configure(spec.defaultParams);
        engine->analyzeSignal(mat->ch);
        engine->prepare(mat->fs, mat->channels, bs, nIn, curve.data());
        const ProtoEngine::Latency lat = engine->latency();

        const FrameCount streamEnd = nIn + lat.inputLookahead;
        const int cap = 8192;
        std::vector<std::vector<double>> staging(
            static_cast<std::size_t>(mat->channels),
            std::vector<double>(static_cast<std::size_t>(cap), 0.0));
        std::vector<double> zeroPad(
            static_cast<std::size_t>(lat.inputLookahead) + 256, 0.0);
        std::vector<const double*> inPtrs(static_cast<std::size_t>(mat->channels));
        std::vector<double*> outPtrs(static_cast<std::size_t>(mat->channels));
        for (int c = 0; c < mat->channels; ++c) {
          outPtrs[static_cast<std::size_t>(c)] =
              staging[static_cast<std::size_t>(c)].data();
        }
        FrameCount pos = 0;
        FrameCount produced = 0;
        double totalMs = 0.0;
        double worstMs = 0.0;
        double worstAtBudget = 0.0;
        // Pre-reserved: the audit region counts driver-side growth too.
        std::vector<double> blockMs;
        blockMs.reserve(static_cast<std::size_t>(streamEnd / bs + 16));
        allocAuditReset();
        while (pos < streamEnd) {
          FrameCount take = std::min<FrameCount>(bs, streamEnd - pos);
          if (pos < nIn) take = std::min(take, nIn - pos);
          for (int c = 0; c < mat->channels; ++c) {
            inPtrs[static_cast<std::size_t>(c)] =
                pos < nIn
                    ? mat->ch[static_cast<std::size_t>(c)].data() + pos
                    : zeroPad.data() + (pos - nIn);
          }
          AudioBlockView view;
          view.channels = inPtrs.data();
          view.channelCount = mat->channels;
          view.frameCount = take;
          AudioBlockOut outRegion;
          outRegion.channels = outPtrs.data();
          outRegion.channelCount = mat->channels;
          outRegion.frameCapacity = cap;
          const auto t0 = std::chrono::steady_clock::now();
          ProcessOutcome rep = engine->process(
              view, static_cast<int>(take), outRegion, cap, pos);
          const auto t1 = std::chrono::steady_clock::now();
          const double ms =
              std::chrono::duration<double, std::milli>(t1 - t0).count();
          totalMs += ms;
          worstMs = std::max(worstMs, ms);
          worstAtBudget =
              std::max(worstAtBudget, ms / (1000.0 * static_cast<double>(bs) /
                                            mat->fs));
          blockMs.push_back(ms);
          produced += rep.outputFramesProduced;
          pos += rep.inputFramesConsumed;
          if (rep.inputFramesConsumed == 0) break;
        }
        AudioBlockOut outRegion;
        outRegion.channels = outPtrs.data();
        outRegion.channelCount = mat->channels;
        outRegion.frameCapacity = cap;
        ProcessOutcome repF = engine->finish(outRegion, cap);
        const int64_t allocs = allocAuditCount();
        produced += repF.outputFramesProduced;

        // p99 block time.
        std::sort(blockMs.begin(), blockMs.end());
        const double p99 = blockMs.empty()
                                ? 0.0
                                : blockMs[std::min(blockMs.size() - 1,
                                                   blockMs.size() * 99 / 100)];
        const double rtf = totalMs / (1000.0 * static_cast<double>(nIn) /
                                      mat->fs);
        const double budgetMs = 1000.0 * static_cast<double>(bs) / mat->fs;

        json::Object run;
        run["material"] = json::Value(matId);
        run["transform"] = json::Value(trId);
        run["block_frames"] = json::Value(static_cast<int64_t>(bs));
        run["mean_block_ms"] = json::Value(
            blockMs.empty() ? 0.0 : totalMs / static_cast<double>(blockMs.size()));
        run["p99_block_ms"] = json::Value(p99);
        run["worst_block_ms"] = json::Value(worstMs);
        run["worst_vs_budget"] = json::Value(worstAtBudget);
        run["block_budget_ms"] = json::Value(budgetMs);
        run["rtf_process_only"] = json::Value(rtf);
        run["allocations_process_finish"] = json::Value(
            static_cast<int64_t>(allocs));
        run["frames_out"] = json::Value(static_cast<int64_t>(produced));
        run["declared_input_lookahead"] = json::Value(
            static_cast<int64_t>(lat.inputLookahead));
        run["declared_output_flush"] =
            json::Value(static_cast<int64_t>(lat.outputFlush));
        runs.push_back(json::Value(std::move(run)));

        if (allocs != 0) {
          std::cout << "  FAIL " << matId << "/" << trId << " bs " << bs
                    << ": " << allocs << " allocations in process/finish\n";
          ++failures;
        }
        std::cout << "  " << matId << "/" << trId << " bs " << bs
                  << std::fixed << std::setprecision(3)
                  << ": RTF " << rtf << "  worst " << worstMs << " ms ("
                  << worstAtBudget << "x budget)  allocs " << allocs << "\n";
      }
    }
  }

  // Block-split invariance (full driver, 128 vs 1024).
  {
    const Material* mat = findMaterial("harmstack");
    const Transform* tr = findTransform("plus12");
    const FrameCount nIn = static_cast<FrameCount>(mat->ch[0].size());
    const std::vector<double> curve = tr->build(nIn, mat->fs);
    std::unique_ptr<ProtoEngine> a = spec.make();
    a->configure(spec.defaultParams);
    DriveResult ra = driveEngine(*a, mat->ch, curve.data(), nIn, mat->fs, 128);
    std::unique_ptr<ProtoEngine> b = spec.make();
    b->configure(spec.defaultParams);
    DriveResult rb = driveEngine(*b, mat->ch, curve.data(), nIn, mat->fs, 1024);
    const bool inv = ra.ok && rb.ok &&
                     waveHashHex(ra.out) == waveHashHex(rb.out);
    if (!inv) {
      std::cout << "  FAIL block-split invariance (128 vs 1024)\n";
      ++failures;
    } else {
      std::cout << "  block-split invariance: bit-identical (128 vs 1024)\n";
    }
  }

  json::Object probe;
  probe["candidate"] = json::Value(spec.id);
  probe["runs"] = json::Value(std::move(runs));
  probe["note"] = json::Value(
      "timing fields are LOCAL EVIDENCE (machine-dependent, not "
      "byte-deterministic); allocation counts and invariance results are "
      "deterministic");
  if (!outDir.empty()) {
    writeJsonFile(fs::path(outDir) / spec.id / "rtprobe.json",
                  json::Value(std::move(probe)));
  }
  const bool pass = failures == 0;
  std::cout << "task28 rtprobe: " << spec.id
            << (pass ? " PASS" : " FAILURES") << "\n";
  return pass ? 0 : 1;
}

}  // namespace pitchlab::proto
