// Pitch Lab VST3 product layer — T-VA*: the realtime adapter suite.
// Drives the REAL adapter (job chains + the v0.1 engines) with synthetic
// host blocks: wet production, fault-freedom, latency accounting, envelope
// re-prepare, bypass/mix, determinism (same input + parameters + block
// schedule => bit-identical output — the realtime guarantee of spec §3).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "vst/realtime_adapter.h"

using namespace pitchlab::vst;

namespace {

constexpr double kFs = 48000.0;

std::vector<double> makeSine(int64_t frames, double freq, double amp = 0.5,
                             double phase = 0.0) {
  std::vector<double> out(static_cast<std::size_t>(frames));
  for (int64_t i = 0; i < frames; ++i) {
    out[static_cast<std::size_t>(i)] =
        amp * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i) / kFs +
                       phase);
  }
  return out;
}

/// Drive the adapter with a deterministic block schedule; returns the output
/// (interleaved per channel) and the final status.
struct DriveResult {
  std::vector<double> out[2];
  StatusSnapshot status;
  MetersSnapshot meters;
};

DriveResult driveAdapter(const ParamSnapshot& snap, int64_t totalFrames,
                         const std::vector<int32_t>& blockSchedule,
                         const std::vector<double>& input = {},
                         double inputFreq = 220.0,
                         const BlockAutomation* automation = nullptr) {
  RealtimeAdapter adapter;
  int32_t maxBlock = 0;
  for (int32_t b : blockSchedule) maxBlock = std::max(maxBlock, b);
  adapter.activate(kFs, 2, maxBlock);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();

  DriveResult res;
  res.out[0].assign(static_cast<std::size_t>(totalFrames), 0.0);
  res.out[1].assign(static_cast<std::size_t>(totalFrames), 0.0);

  const std::vector<double> sig =
      input.empty() ? makeSine(totalFrames, inputFreq) : input;

  int64_t pos = 0;
  std::size_t blockIdx = 0;
  while (pos < totalFrames) {
    const int32_t n = blockSchedule[blockIdx % blockSchedule.size()];
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(n, totalFrames - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* out[2] = {res.out[0].data() + pos, res.out[1].data() + pos};
    BlockAutomation none;
    adapter.process(in, out, take, automation != nullptr ? *automation : none);
    pos += take;
    ++blockIdx;
  }
  res.status = adapter.status();
  res.meters = adapter.meters();
  adapter.deactivate();
  return res;
}

double rmsDb(const std::vector<double>& x, int64_t from, int64_t to) {
  if (to <= from) return -999.0;
  double acc = 0.0;
  for (int64_t i = from; i < to; ++i) acc += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
  return 20.0 * std::log10(std::sqrt(acc / static_cast<double>(to - from)) + 1e-12);
}

}  // namespace

TEST_CASE("all five engines produce wet audio without faults") {
  initEngineRegistryOnce();
  const int64_t total = static_cast<int64_t>(kFs * 1.5);  // 1.5 s
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  for (int engine = 0; engine < 5; ++engine) {
    ParamSnapshot snap;
    snap.engineIndex = engine;
    snap.pitchSt = 5.0;  // +5 st: clearly audible processing
    CAPTURE(engineIdForIndex(engine));
    const int64_t settle = static_cast<int64_t>(kFs * 0.4);  // latency + seam margin
    const int64_t end = total - static_cast<int64_t>(kFs * 0.1);
    DriveResult r = driveAdapter(snap, total, blocks, {}, 220.0);
    CHECK(r.status.faults == 0);
    CHECK(r.status.chainReady);
    CHECK(r.status.reprepares >= 1);  // the initial chain built
    // audible wet: the settled output is well above silence
    CHECK(rmsDb(r.out[0], settle, end) > -50.0);
    // stereo channels identical (same input both channels)
    CHECK(std::memcmp(r.out[0].data(), r.out[1].data(), r.out[0].size() * sizeof(double)) == 0);
  }
}

TEST_CASE("adapter determinism: identical drive = bit-identical output") {
  const int64_t total = static_cast<int64_t>(kFs * 0.8);
  const std::vector<int32_t> blocks = {777, 1024, 256, 4096, 128};
  ParamSnapshot snap;
  snap.engineIndex = 2;  // pv.classic
  snap.pitchSt = 3.0;
  const DriveResult a = driveAdapter(snap, total, blocks);
  const DriveResult b = driveAdapter(snap, total, blocks);
  CHECK(a.out[0] == b.out[0]);
  CHECK(a.out[1] == b.out[1]);
  CHECK(a.status.faults == 0);
}

TEST_CASE("identity pitch: output tracks the delayed input at ratio 1") {
  // At ratio 1 and full wet, the preserving engines output the input
  // (delayed by the reported latency). The varispeed splice mode is
  // documented NOT to be sample-exact across seams — checked separately.
  const int64_t total = static_cast<int64_t>(kFs * 1.0);
  const std::vector<int32_t> blocks = {4096};
  ParamSnapshot snap;
  snap.engineIndex = 1;  // vardelay
  snap.pitchSt = 0.0;
  const auto sig = makeSine(total, 220.0);
  DriveResult r = driveAdapter(snap, total, blocks, sig);
  CHECK(r.status.faults == 0);
  const int64_t lat = r.status.latencyFrames;
  INFO("latency frames: " << lat);
  CHECK(lat > 0);
  // after the latency, the output equals the input (double precision, same
  // engine chain, deterministic paths) within numerical noise of the
  // engine's resampler identity criterion (v0.1 uses -80 dBFS audio
  // equivalence: compare energies instead of bit equality)
  // the wet path at identity = the input delayed by the declared latency
  // (the dry path is delay-matched, so out(p) == sig(p - latency))
  const int64_t begin = lat + 256;
  double diffAcc = 0.0;
  double refAcc = 0.0;
  for (int64_t i = begin; i < total - 256; ++i) {
    const double d =
        r.out[0][static_cast<std::size_t>(i)] - sig[static_cast<std::size_t>(i - lat)];
    diffAcc += d * d;
    refAcc += sig[static_cast<std::size_t>(i - lat)] * sig[static_cast<std::size_t>(i - lat)];
  }
  const double relDb = 10.0 * std::log10((diffAcc + 1e-30) / (refAcc + 1e-30));
  INFO("relative error dB: " << relDb);
  CHECK(relDb < -35.0);  // conservative bound for the L-5 -80 dBFS class
}

TEST_CASE("bypass: latency-compensated dry passes the input") {
  const int64_t total = static_cast<int64_t>(kFs * 0.8);
  const std::vector<int32_t> blocks = {2048};
  ParamSnapshot snap;
  snap.engineIndex = 3;  // pv.phaselocked
  snap.pitchSt = 4.0;
  snap.bypass = true;
  const auto sig = makeSine(total, 330.0);
  DriveResult r = driveAdapter(snap, total, blocks, sig);
  CHECK(r.status.faults == 0);
  // after the bypass fade (10 ms) + latency, output == input exactly (the
  // dry path is a pure copy, gain 0 dB)
  // bypass dry = the latency-delayed input (phase-aligned with the wet path)
  const int64_t lat = r.status.latencyFrames;
  const int64_t begin = lat + static_cast<int64_t>(0.012 * kFs);
  for (int64_t i = begin; i < total; ++i) {
    CHECK(r.out[0][static_cast<std::size_t>(i)] ==
          doctest::Approx(sig[static_cast<std::size_t>(i - lat)]).epsilon(1e-9));
  }
  CHECK(r.status.bypassActive);
}

TEST_CASE("dry/wet mix: 50% mix = half wet half dry") {
  const int64_t total = static_cast<int64_t>(kFs * 0.8);
  const std::vector<int32_t> blocks = {2048};
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot wetSnap, drySnap;
  wetSnap.engineIndex = 1;
  wetSnap.pitchSt = 7.0;
  drySnap = wetSnap;
  drySnap.mix = 0.0;  // fully dry
  const DriveResult wet = driveAdapter(wetSnap, total, blocks, sig);
  const DriveResult dry = driveAdapter(drySnap, total, blocks, sig);
  ParamSnapshot half = wetSnap;
  half.mix = 0.5;
  const DriveResult mid = driveAdapter(half, total, blocks, sig);
  CHECK(wet.status.faults == 0);
  CHECK(dry.status.faults == 0);
  CHECK(mid.status.faults == 0);
  const int64_t begin = wet.status.latencyFrames + 1024;
  for (int64_t i = begin; i < total - 256; ++i) {
    const std::size_t k = static_cast<std::size_t>(i);
    const double expect = 0.5 * wet.out[0][k] + 0.5 * dry.out[0][k];
    CHECK(mid.out[0][k] == doctest::Approx(expect).epsilon(1e-9));
  }
}

TEST_CASE("envelope re-prepare: automation beyond ±1 st rebuilds the chain") {
  const int64_t total = static_cast<int64_t>(kFs * 1.2);
  const std::vector<int32_t> blocks = {512};
  ParamSnapshot snap;
  snap.engineIndex = 2;
  snap.pitchSt = 0.0;

  RealtimeAdapter adapter;
  adapter.activate(kFs, 2, 512);
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const auto sig = makeSine(total, 440.0);
  BlockAutomation automation;
  // a fast +9 st automation ramp (block-relative offsets; the curve exits
  // the ±1 st envelope within the first block and stays outside)
  automation.pitch[0] = {0, 0.0};
  automation.pitch[1] = {2, 9.0};  // +9 st: way outside ±1 st
  automation.pitchCount = 2;
  int64_t pos = 0;
  std::vector<double> out(total, 0.0);
  while (pos < total) {
    const int32_t take =
        static_cast<int32_t>(std::min<int64_t>(512, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out.data() + pos, out.data() + pos};
    adapter.process(in, o, take, automation);
    pos += take;
  }
  const StatusSnapshot st = adapter.status();
  CHECK(st.chainReady);
  CHECK(st.reprepares >= 2);       // initial + envelope exit
  CHECK(st.clampEvents >= 1);      // the exit was detected
  CHECK(st.envelopeMax > 1.7);     // re-centred around +9 st (2^(10/12) ~ 1.78)
  adapter.deactivate();
}

TEST_CASE("latency: expected matches the active chain's reported latency") {
  initEngineRegistryOnce();
  const std::vector<int32_t> blocks = {1024};
  for (int engine = 0; engine < 5; ++engine) {
    ParamSnapshot snap;
    snap.engineIndex = engine;
    const int64_t expected = expectedLatencyFrames(snap, kFs);
    CAPTURE(engineIdForIndex(engine));
    CHECK(expected > 0);
    const DriveResult r =
        driveAdapter(snap, static_cast<int64_t>(kFs * 0.5), blocks);
    CHECK(r.status.latencyFrames == expected);
  }
}

TEST_CASE("block-size robustness: any schedule produces consistent audio") {
  // two different block schedules: outputs are NOT required to be
  // bit-identical (the engine contract allows block-schedule variance —
  // the v0.1 engines themselves define their own invariances), but the
  // drive must remain fault-free and audible
  ParamSnapshot snap;
  snap.engineIndex = 4;
  snap.pitchSt = -4.0;
  const auto sig = makeSine(static_cast<int64_t>(kFs * 0.8), 196.0);
  const DriveResult a =
      driveAdapter(snap, static_cast<int64_t>(kFs * 0.8), {4096}, sig);
  const DriveResult b =
      driveAdapter(snap, static_cast<int64_t>(kFs * 0.8), {97, 1024, 397}, sig);
  CHECK(a.status.faults == 0);
  CHECK(b.status.faults == 0);
  CHECK(rmsDb(a.out[0], a.status.latencyFrames + 2048,
              static_cast<int64_t>(kFs * 0.7)) > -50.0);
  CHECK(rmsDb(b.out[0], b.status.latencyFrames + 2048,
              static_cast<int64_t>(kFs * 0.7)) > -50.0);
}

TEST_CASE("mono processing: single-channel chain runs fault-free") {
  RealtimeAdapter adapter;
  adapter.activate(kFs, 1, 1024);
  ParamSnapshot snap;
  snap.engineIndex = 2;
  snap.pitchSt = 2.0;
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total = static_cast<int64_t>(kFs * 0.6);
  const auto sig = makeSine(total, 220.0);
  std::vector<double> out(total, 0.0);
  int64_t pos = 0;
  while (pos < total) {
    const int32_t take =
        static_cast<int32_t>(std::min<int64_t>(1024, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out.data() + pos, out.data() + pos};
    adapter.process(in, o, take, BlockAutomation{});
    pos += take;
  }
  const StatusSnapshot st = adapter.status();
  CHECK(st.channels == 1);
  CHECK(st.faults == 0);
  CHECK(rmsDb(out, st.latencyFrames + 2048, total) > -50.0);
  adapter.deactivate();
}

TEST_CASE("varispeed mode: windowed splice status + worst-case latency") {
  ParamSnapshot snap;
  snap.engineIndex = 0;
  snap.pitchSt = 0.0;
  const DriveResult r = driveAdapter(snap, static_cast<int64_t>(kFs * 0.7), {2048});
  CHECK(std::strcmp(r.status.adaptation, "windowed splice adaptation") == 0);
  CHECK(r.status.faults == 0);
  // fixed worst-case latency: (2·2^(1/12) − 1)·0.2 s + margins, at 48 kHz
  const double worstEnv = 2.0 * std::exp2(1.0 / 12.0);
  const int64_t expect = static_cast<int64_t>(
      std::ceil((worstEnv - 1.0) * 0.2 * kFs)) + 32 + 64;
  CHECK(r.status.latencyFrames == expect);
}

// ---------------------------------------------------------------------------
// Mid-stream chain replacements (the scenarios that caught the wet-timeline
// retiring-feed defect, the Λ_eff starvation on latency-decreasing switches,
// and the job-feed lag gap): every engine pair switch in BOTH directions,
// envelope exits on the preserving AND splice engines, and hard resets —
// all must stay fault-free with the chain ready.
// ---------------------------------------------------------------------------

namespace {

struct StreamDrive {
  RealtimeAdapter adapter;
  std::vector<double> out[2];
  int64_t pos = 0;

  StatusSnapshot status() const { return adapter.status(); }
  void deactivate() { adapter.deactivate(); }
  void start(int ch = 2, int maxBlock = 4096) { adapter.activate(kFs, ch, maxBlock); }
  void snap(int engine, double pitchSt) {
    ParamSnapshot s;
    s.engineIndex = engine;
    s.pitchSt = pitchSt;
    adapter.setParameterSnapshot(s);
  }
  void run(const std::vector<double>& sig, const std::vector<int32_t>& blocks) {
    const int64_t endPos = pos + static_cast<int64_t>(sig.size());
    if (static_cast<int64_t>(out[0].size()) < endPos) {
      out[0].resize(static_cast<std::size_t>(endPos), 0.0);
      out[1].resize(static_cast<std::size_t>(endPos), 0.0);
    }
    std::size_t bi = 0;
    while (pos < endPos) {
      const int32_t n = blocks[bi % blocks.size()];
      const int32_t take =
          static_cast<int32_t>(std::min<int64_t>(n, endPos - pos));
      const int64_t off = pos - (endPos - static_cast<int64_t>(sig.size()));
      const double* in[2] = {sig.data() + off, sig.data() + off};
      double* o[2] = {out[0].data() + pos, out[1].data() + pos};
      BlockAutomation none;
      adapter.process(in, o, take, none);
      pos += take;
      ++bi;
    }
  }
};

}  // namespace

TEST_CASE("mid-stream engine switches: every ordered pair stays fault-free") {
  initEngineRegistryOnce();
  const int64_t two = static_cast<int64_t>(kFs * 2.0);
  const int64_t four = static_cast<int64_t>(kFs * 4.0);
  const auto sig = makeSine(four, 220.0);
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  for (int a = 0; a < 5; ++a) {
    for (int b = 0; b < 5; ++b) {
      if (a == b) continue;
      CAPTURE(a);
      CAPTURE(b);
      StreamDrive d;
      d.start();
      d.snap(a, 4.0);
      d.adapter.requestHardReset();
      d.run(std::vector<double>(sig.begin(), sig.begin() + two), blocks);
      d.snap(b, 4.0);  // the switch happens with real audio after it
      d.run(std::vector<double>(sig.begin() + two, sig.end()), blocks);
      const StatusSnapshot st = d.status();
      CHECK(st.faults == 0);
      CHECK(st.chainReady);
      // the effective emission latency is the MAXIMUM of the two chains
      // (a latency DECREASE must never starve the retiring chain: Λ_eff)
      const int64_t latA = expectedLatencyFrames([&] {
        ParamSnapshot s;
        s.engineIndex = a;
        s.pitchSt = 4.0;
        return s;
      }(), kFs);
      const int64_t latB = expectedLatencyFrames([&] {
        ParamSnapshot s;
        s.engineIndex = b;
        s.pitchSt = 4.0;
        return s;
      }(), kFs);
      CHECK(st.latencyFrames == std::max(latA, latB));
      d.deactivate();
    }
  }
}

TEST_CASE("mid-stream envelope exits on preserving + splice engines") {
  const int64_t two = static_cast<int64_t>(kFs * 2.0);
  const int64_t six = static_cast<int64_t>(kFs * 6.0);
  const auto sig = makeSine(six, 220.0);
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  // pv.classic (preserving) + varispeed and granular (splice): each exits
  // the ±1 st envelope at ratio > 1 and must re-prepare without faults
  for (int engine : {2, 0, 4}) {
    CAPTURE(engine);
    StreamDrive d;
    d.start();
    d.snap(engine, 0.0);
    d.adapter.requestHardReset();
    d.run(std::vector<double>(sig.begin(), sig.begin() + two), blocks);
    d.snap(engine, 7.0);  // exits the ±1 st envelope
    d.run(std::vector<double>(sig.begin() + two, sig.begin() + 2 * two), blocks);
    d.snap(engine, -7.0);  // and out the other side
    d.run(std::vector<double>(sig.begin() + 2 * two, sig.end()), blocks);
    const StatusSnapshot st = d.status();
    CHECK(st.faults == 0);
    CHECK(st.chainReady);
    CHECK(st.clampEvents >= 1);  // the exits were detected and handled
    d.deactivate();
  }
}

TEST_CASE("hard reset mid-stream (host flush) restarts cleanly") {
  const int64_t two = static_cast<int64_t>(kFs * 2.0);
  const int64_t four = static_cast<int64_t>(kFs * 4.0);
  const auto sig = makeSine(four, 220.0);
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  StreamDrive d;
  d.start();
  d.snap(2, 5.0);
  d.adapter.requestHardReset();
  d.run(std::vector<double>(sig.begin(), sig.begin() + two), blocks);
  d.adapter.requestHardReset();  // mid-stream flush
  d.run(std::vector<double>(sig.begin() + two, sig.end()), blocks);
  const StatusSnapshot st = d.status();
  CHECK(st.faults == 0);
  CHECK(st.chainReady);
  d.deactivate();
}
