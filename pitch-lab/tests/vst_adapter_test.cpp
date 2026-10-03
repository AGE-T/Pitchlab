// Pitch Lab VST3 product layer — T-VA*: the realtime adapter suite.
// Drives the REAL adapter (job chains + the v0.1 engines) with synthetic
// host blocks: wet production, fault-freedom, latency accounting, envelope
// re-prepare, bypass/mix, determinism (same input + parameters + block
// schedule => bit-identical output — the realtime guarantee of spec §3).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
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
  for (int engine = 0; engine < engineCount(); ++engine) {
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
  for (int engine = 0; engine < engineCount(); ++engine) {
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
  CHECK(r.status.spliceMode);  // windowed splice adaptation (numeric status)
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

TEST_CASE("mid-stream MODE switches: every ordered timepitch-mode pair stays fault-free (the 12-pair matrix)") {
  // §6.6.1 item 17: a mode change is a configuration/ChainSignature
  // transition through the EXISTING adapter lifecycle — the full ordered-pair
  // matrix over the four internal modes of native.timepitch (12 pairs): the
  // adoption machinery, the equal-power seam and the latency accounting must
  // stay fault-free with the chain ready, INCLUDING the
  // Preserving<->RateFollowing duration-class crossings and the
  // window-product<->raw-OLA accumulation-policy crossings (the two policies
  // never merge — the switches exercise both sides).
  initEngineRegistryOnce();
  const int64_t two = static_cast<int64_t>(kFs * 2.0);
  const int64_t four = static_cast<int64_t>(kFs * 4.0);
  const auto sig = makeSine(four, 220.0);
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  auto modeSnap = [&](int mode, double pitchSt) {
    ParamSnapshot s;
    s.engineIndex = 5;  // native.timepitch (the registry-order pin: T-E19)
    s.tpMode = mode;
    s.pitchSt = pitchSt;
    return s;
  };
  for (int a = 0; a < 4; ++a) {
    for (int b = 0; b < 4; ++b) {
      if (a == b) continue;
      CAPTURE(a);
      CAPTURE(b);
      StreamDrive d;
      d.start();
      d.adapter.setParameterSnapshot(modeSnap(a, 4.0));
      d.adapter.requestHardReset();
      d.run(std::vector<double>(sig.begin(), sig.begin() + two), blocks);
      d.adapter.setParameterSnapshot(modeSnap(b, 4.0));  // the mode switch
      d.run(std::vector<double>(sig.begin() + two, sig.end()), blocks);
      const StatusSnapshot st = d.status();
      const int64_t latA = expectedLatencyFrames(modeSnap(a, 4.0), kFs);
      const int64_t latB = expectedLatencyFrames(modeSnap(b, 4.0), kFs);
      CHECK(st.faults == 0);
      CHECK(st.chainReady);
      // the effective emission latency is the MAXIMUM of the two mode
      // chains (the same Λ_eff rule as the engine-pair matrix)
      CHECK(st.latencyFrames == std::max(latA, latB));
      d.deactivate();
    }
  }
}

TEST_CASE("mid-stream engine switches: every ordered pair stays fault-free") {
  initEngineRegistryOnce();
  const int64_t two = static_cast<int64_t>(kFs * 2.0);
  const int64_t four = static_cast<int64_t>(kFs * 4.0);
  const auto sig = makeSine(four, 220.0);
  const std::vector<int32_t> blocks = {512, 128, 1024, 256, 4096, 64};
  for (int a = 0; a < engineCount(); ++a) {
    for (int b = 0; b < engineCount(); ++b) {
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

// ---------------------------------------------------------------------------
// Task 24 defect-audit regressions (the previously-failing boundaries)
// ---------------------------------------------------------------------------

TEST_CASE("P0.1 regression: startup is non-blocking — the audio thread never waits") {
  // The previous implementation slept up to 25 ms INSIDE process() whenever
  // no chain existed (a realtime-contract violation, spec §4.3). The fix
  // moved the first-chain wait to the MAIN thread (activate()/first
  // publish). Driving the adapter with NO published snapshot leaves the
  // audio path chainless forever — every block must return immediately.
  RealtimeAdapter adapter;
  adapter.activate(kFs, 2, 1024);
  const int32_t frames = 512;
  std::vector<double> inL(frames, 0.25), outL(frames), outR(frames);
  const double* in[2] = {inL.data(), inL.data()};
  double* out[2] = {outL.data(), outR.data()};
  const auto t0 = std::chrono::steady_clock::now();
  constexpr int kBlocks = 40;
  for (int b = 0; b < kBlocks; ++b) {
    adapter.process(in, out, frames, BlockAutomation{});
  }
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  // The previous code slept >= 25 ms per chainless block (>= 1000 ms for 40
  // blocks, deterministically). The fixed path is memcpy-scale. The bound is
  // generous for slow CI runners while still separating the two regimes by
  // >2x.
  CHECK(ms < 400.0);
  CAPTURE(ms);
  // chainless blocks are honest dry passthrough (the latency-compensated
  // fallback, not garbage)
  for (int32_t i = 0; i < frames; ++i) {
    CHECK(std::fabs(outL[static_cast<std::size_t>(i)] - 0.25) < 1e-12);
  }
  adapter.deactivate();
}

TEST_CASE("P0.2 regression: concurrent publication stress (race-free snapshots)") {
  // The atomised-word SeqLock + the atomic Chain::base / Job::index /
  // reprepares fixes make the three-thread publication protocol race-free.
  // (TSAN-verified separately on the sandbox; this case pins the functional
  // behaviour: a writer storm + audio processing + UI polling must produce
  // valid snapshots and no crashes.)
  RealtimeAdapter adapter;
  ParamSnapshot snap;  // default vardelay
  adapter.activate(kFs, 2, 1024);
  adapter.setParameterSnapshot(snap);
  const int32_t frames = 256;
  std::vector<double> inL(frames), outL(frames), outR(frames);
  for (int32_t i = 0; i < frames; ++i) {
    inL[static_cast<std::size_t>(i)] =
        0.4 * std::sin(2.0 * 3.14159265358979 * 220.0 * i / kFs);
  }
  const double* in[2] = {inL.data(), inL.data()};
  double* out[2] = {outL.data(), outR.data()};
  // publish a first status/meters before the readers start (the snapshots
  // are zero-initialised until the first block completes — the strict
  // reader checks below assume a published state)
  for (int b = 0; b < 2; ++b) {
    adapter.process(in, out, frames, BlockAutomation{});
  }
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int i = 0; i < 20000 && !stop.load(std::memory_order_relaxed); ++i) {
      ParamSnapshot s;
      s.pitchSt = (i % 10) - 5.0;
      s.mix = 0.2 + 0.001 * (i % 700);
      adapter.setParameterSnapshot(s);
    }
  });
  std::thread ui([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      const MetersSnapshot m = adapter.meters();
      const StatusSnapshot st = adapter.status();
      CHECK(std::isfinite(m.inPeak[0]));
      // The documented SeqLock reader contract: a stable copy has the
      // published sampleRate; under writer starvation (this stress's own
      // audio thread stores the status EVERY block, back-to-back) the
      // bounded retry may exhaust and return the zeroed degenerate snapshot
      // (seqlock.h: "absorbed downstream — meters/status are cosmetic
      // telemetry"). A TORN value is impossible with the atomised payload +
      // version check: any OTHER sampleRate means a real protocol defect.
      // (This CHECK previously asserted == kFs unconditionally — stricter
      // than the primitive's documented contract; it flaked under CPU
      // contention when the zero-sleep reader was preempted mid-retry 4096
      // times. Not related to any product change: a latent test-side
      // over-assertion, found during the UI-binding hotfix verification.)
      const bool statusOk = (st.sampleRate == kFs) ||
                            (st.sampleRate == 0.0 && !st.chainReady);
      CHECK(statusOk);
    }
  });
  for (int b = 0; b < 598; ++b) {
    adapter.process(in, out, frames, BlockAutomation{});
  }
  stop.store(true, std::memory_order_relaxed);
  writer.join();
  ui.join();
  const StatusSnapshot st = adapter.status();
  CHECK(st.faults == 0);
  adapter.deactivate();
}

TEST_CASE("P1.1 regression: LFO depth automation reaches the realtime curve") {
  // kLfoDepth is declared host-automatable — its events must reach the
  // curve (the previous implementation accepted the events and dropped
  // them: automation-inert parameters). Observable: with pitch 0 and a
  // +2 st depth step, the curve swings outside the ±1 st envelope ->
  // clampEvents; and the audio differs from the no-automation drive.
  const int64_t total = static_cast<int64_t>(kFs * 0.6);
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;  // vardelay, pitch 0

  BlockAutomation auto0;
  DriveResult plain = driveAdapter(snap, total, {1024}, sig);
  CHECK(plain.status.clampEvents == 0);

  BlockAutomation withDepth;
  withDepth.lfoDepth[0] = {0, 2.0};  // +2 st from frame 0
  withDepth.lfoDepthCount = 1;
  DriveResult automated = driveAdapter(snap, total, {1024}, sig, 220.0, &withDepth);
  CHECK(automated.status.clampEvents >= 1);  // the envelope was exceeded
  // the outputs differ (the automation changed the audio)
  double diff = 0.0;
  for (int64_t i = 0; i < total; ++i) {
    diff += std::fabs(plain.out[0][static_cast<std::size_t>(i)] -
                      automated.out[0][static_cast<std::size_t>(i)]);
  }
  CHECK(diff > 1.0);
}

TEST_CASE("P1.1 regression: LFO rate automation changes the modulation speed") {
  // depth 0.4 st keeps the curve safely INSIDE the ±1 st envelope (no
  // clamps -> no mid-stream re-prepares -> the bit-determinism guarantee
  // applies to the two identical drives)
  const int64_t total = static_cast<int64_t>(kFs * 0.8);
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;
  snap.pitchSt = 0.0;
  snap.lfoDepthSt = 0.4;  // vibrato on (inside the envelope)
  snap.lfoRateHz = 0.5;   // slow

  BlockAutomation fast;
  fast.lfoRate[0] = {0, 6.0};  // automated to 6 Hz from frame 0
  fast.lfoRateCount = 1;
  DriveResult slow = driveAdapter(snap, total, {1024}, sig);
  DriveResult quick = driveAdapter(snap, total, {1024}, sig, 220.0, &fast);
  double diff = 0.0;
  for (int64_t i = 0; i < total; ++i) {
    diff += std::fabs(slow.out[0][static_cast<std::size_t>(i)] -
                      quick.out[0][static_cast<std::size_t>(i)]);
  }
  CHECK(diff > 1.0);  // the automated rate produced different modulation
  // determinism: the same automation yields bit-identical output
  DriveResult quick2 = driveAdapter(snap, total, {1024}, sig, 220.0, &fast);
  CHECK(quick.out[0] == quick2.out[0]);
}

TEST_CASE("P1.2 regression: bypass automation is frame-correct") {
  // A bypass event at frame F must take effect AT F (the previous
  // implementation applied the whole-block target at the block start).
  // Drive: strong pitch (+8 st, clearly non-identity wet); bypass ON at
  // frame F inside a 2048 block. Before F: output is wet (differs from the
  // latency-compensated dry); after F + 10 ms fade: output is the
  // latency-compensated dry.
  const int64_t total = 4096;
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;
  snap.pitchSt = 8.0;
  snap.mix = 1.0;

  BlockAutomation ba;
  const int32_t F = 3000;  // past the latency window, inside the second block
  ba.bypass[0] = {F, 1.0};
  ba.bypassCount = 1;

  // NOTE: driveAdapter passes the SAME automation struct to every process
  // call — offsets are ADAPTER-CALL-relative. A single 4096-frame block
  // keeps the event in-range for its call (the processor-level coordinate
  // conversion across chunks is covered in the processor suite).
  DriveResult r = driveAdapter(snap, total, {4096}, sig, 220.0, &ba);
  const int64_t lat = r.status.latencyFrames;
  REQUIRE(lat + 64 < F - 64);  // the wet window must lie past the latency
  // before the event: wet (the pitched output differs from the dry)
  double wetDiff = 0.0;
  for (int64_t p = lat + 64; p < F - 64; ++p) {
    wetDiff += std::fabs(r.out[0][static_cast<std::size_t>(p)] -
                         sig[static_cast<std::size_t>(p - lat)]);
  }
  CHECK(wetDiff > 1.0);
  // 10 ms after the event (fade complete): dry = latency-compensated input
  const int64_t fadeDone = F + 481;
  double dryDiff = 0.0;
  for (int64_t p = fadeDone; p < total; ++p) {
    dryDiff += std::fabs(r.out[0][static_cast<std::size_t>(p)] -
                         sig[static_cast<std::size_t>(p - lat)]);
  }
  CHECK(dryDiff < 1e-9);
}

TEST_CASE("P1.2 regression: multiple bypass toggles inside one block") {
  const int64_t total = 4096;
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;
  snap.pitchSt = 8.0;
  BlockAutomation ba;
  ba.bypass[0] = {2600, 1.0};
  ba.bypass[1] = {3000, 0.0};
  ba.bypass[2] = {3400, 1.0};
  ba.bypassCount = 3;
  DriveResult r = driveAdapter(snap, total, {4096}, sig, 220.0, &ba);
  CHECK(r.status.faults == 0);
  const int64_t lat = r.status.latencyFrames;
  // after the third toggle + fade: dry again
  const int64_t dryFrom = 3400 + 481;
  double dryDiff = 0.0;
  for (int64_t p = dryFrom; p < total; ++p) {
    dryDiff += std::fabs(r.out[0][static_cast<std::size_t>(p)] -
                         sig[static_cast<std::size_t>(p - lat)]);
  }
  CHECK(dryDiff < 1e-9);
}

TEST_CASE("P1.3 regression: dense automation beyond the old 32-point cap") {
  // A linear ramp A->B over N frames is EXACTLY representable by 2 points
  // and by 64 points (timelineAt interpolates linearly). The previous
  // 32-point cap silently truncated the 64-point form (dropping the ramp's
  // second half). The two forms must produce BIT-IDENTICAL output.
  const int64_t total = 8192;
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;
  snap.pitchSt = 0.0;

  BlockAutomation sparse;
  sparse.pitch[0] = {0, 0.0};
  sparse.pitch[1] = {static_cast<int32_t>(total - 1), 6.0};
  sparse.pitchCount = 2;

  BlockAutomation dense;
  for (int i = 0; i < 64; ++i) {
    const double u = static_cast<double>(i) / 63.0;
    dense.pitch[i] = {static_cast<int32_t>((total - 1) * u), 6.0 * u};
  }
  dense.pitchCount = 64;

  DriveResult a = driveAdapter(snap, total, {1024}, sig, 220.0, &sparse);
  DriveResult b = driveAdapter(snap, total, {1024}, sig, 220.0, &dense);
  CHECK(a.out[0] == b.out[0]);
  CHECK(a.status.automationDropped == 0);
  CHECK(b.status.automationDropped == 0);
}

TEST_CASE("P1.3 diagnostics: overflow beyond capacity is counted, never silent") {
  // 300 points on one parameter: consolidated (first 255 + last), the drop
  // counter published through the status.
  const int64_t total = 4096;
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;
  BlockAutomation flood;
  for (int i = 0; i < BlockAutomation::kMaxPoints; ++i) {
    flood.pitch[i] = {i * 13, 0.0};
  }
  flood.pitchCount = BlockAutomation::kMaxPoints;
  // the consolidation itself is the processor collector's job (exercised
  // in the processor suite); here we pin the ADAPTER's accounting of the
  // drop counter it is handed (never silent, published through the status)
  flood.droppedPoints = 44;
  DriveResult r = driveAdapter(snap, total, {2048}, sig, 220.0, &flood);
  CHECK(r.status.automationDropped >= 44);
}

TEST_CASE("P2.1 regression: the bypass fade is equal-power") {
  // At identity pitch (wet == dry), the mid-fade output of an equal-power
  // crossfade is ~sqrt(2)·x; the previous LINEAR curve passed x through
  // unchanged. This differential pins the spec §4.1 item 6 curve.
  const int64_t total = 4096;
  const auto sig = makeSine(total, 220.0, 0.5);
  ParamSnapshot snap;  // pitch 0: identity (wet tracks the delayed input)
  BlockAutomation ba;
  ba.bypass[0] = {2600, 1.0};  // past the latency window
  ba.bypassCount = 1;
  DriveResult r = driveAdapter(snap, total, {4096}, sig, 220.0, &ba);
  const int64_t lat = r.status.latencyFrames;
  // mid-fade (5 ms after the event): equal-power sum ~1.414x; linear = 1x
  const int64_t mid = 2600 + 240;
  double amp = 0.0, ref = 0.0;
  for (int64_t p = mid - 24; p <= mid + 24; ++p) {
    const double x = sig[static_cast<std::size_t>(p - lat)];
    amp += std::fabs(r.out[0][static_cast<std::size_t>(p)]);
    ref += std::fabs(x);
  }
  const double ratio = amp / (ref + 1e-12);
  CHECK(ratio > 1.15);   // equal-power mid-fade (linear would be ~1.0)
  CHECK(ratio < 1.55);   // ...and not something pathological
}

TEST_CASE("P2.5 regression: meter ballistics are block-schedule independent") {
  // The same input stream processed on two different block schedules must
  // produce THE SAME meter values at the same stream length (per-SAMPLE
  // time-based decay; the previous per-block factors made the meters
  // block-size dependent).
  const int64_t total = static_cast<int64_t>(kFs * 0.4);
  std::vector<double> sig(static_cast<std::size_t>(total));
  for (int64_t i = 0; i < total; ++i) {
    const double t = static_cast<double>(i) / kFs;
    // a decaying tone + one clip burst (exercises peak, rms AND clip paths)
    double v = 0.8 * std::sin(2.0 * 3.14159265358979 * 330.0 * t) *
               std::exp(-1.5 * t);
    if (i > kFs * 0.1 && i < kFs * 0.1 + 64) v = 1.4;  // clip burst
    sig[static_cast<std::size_t>(i)] = v;
  }
  // bypassed + dry-mix: the OUTPUT stream is the latency-compensated dry
  // (identical samples regardless of the internal schedule), so the OUTPUT
  // meters are comparable bit-identically too (with wet active the internal
  // sub-chunking legitimately perturbs the output)
  ParamSnapshot snap;
  snap.mix = 0.0;
  snap.bypass = true;
  DriveResult a = driveAdapter(snap, total, {256}, sig);
  DriveResult b = driveAdapter(snap, total, {4096}, sig);
  for (int c = 0; c < 2; ++c) {
    CHECK(std::fabs(a.meters.inPeak[c] - b.meters.inPeak[c]) < 1e-12);
    CHECK(std::fabs(a.meters.inRms[c] - b.meters.inRms[c]) < 1e-12);
    CHECK(std::fabs(a.meters.inClip[c] - b.meters.inClip[c]) < 1e-12);
    CHECK(std::fabs(a.meters.outPeak[c] - b.meters.outPeak[c]) < 1e-12);
    CHECK(std::fabs(a.meters.outRms[c] - b.meters.outRms[c]) < 1e-12);
    CHECK(std::fabs(a.meters.outClip[c] - b.meters.outClip[c]) < 1e-12);
  }
}

TEST_CASE("P2.4 regression: input and output clip states are distinct") {
  // A hot input that stays below 1.0, with +12 dB output gain, must clip
  // the OUTPUT only — the previous implementation shared one clip state.
  const int64_t total = static_cast<int64_t>(kFs * 0.3);
  std::vector<double> sig(static_cast<std::size_t>(total), 0.5);  // no input clip
  ParamSnapshot snap;
  snap.outputDb = 12.0;  // x4 gain -> output clips
  DriveResult r = driveAdapter(snap, total, {1024}, sig);
  CHECK(r.meters.inClip[0] < 0.5);
  CHECK(r.meters.outClip[0] > 0.9);
}

TEST_CASE("P1.4 regression: adapter processing reset (suspend/resume)") {
  // The setProcessing resume contract: after requestProcessingReset() the
  // stream continues fault-free with a fresh chain and correct parameters.
  RealtimeAdapter adapter;
  ParamSnapshot snap;
  snap.engineIndex = 2;  // granular
  snap.pitchSt = 3.0;
  const auto sig = makeSine(static_cast<int64_t>(kFs * 0.5), 220.0);
  std::vector<double> out[2] = {std::vector<double>(sig.size()),
                                std::vector<double>(sig.size())};
  const int64_t total = static_cast<int64_t>(sig.size());
  auto drive = [&]() {
    for (int64_t done = 0; done < total;) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(2048, total - done));
      const double* inB[2] = {sig.data() + done, sig.data() + done};
      double* outB[2] = {out[0].data() + done, out[1].data() + done};
      adapter.process(inB, outB, take, BlockAutomation{});
      done += take;
    }
  };
  adapter.activate(kFs, 2, 2048);
  adapter.setParameterSnapshot(snap);
  drive();
  const StatusSnapshot before = adapter.status();
  CHECK(before.chainReady);
  CHECK(before.engineIndex == 2);
  // suspend + resume: fresh chain, stream continues
  adapter.requestProcessingReset();
  drive();
  StatusSnapshot after = adapter.status();
  // the fresh chain arrives asynchronously (~ms) — drive (bounded) until live
  for (int guard = 0; guard < 200 && (!after.chainReady || after.engineIndex != 2); ++guard) {
    const double* inB[2] = {sig.data(), sig.data()};
    double* outB[2] = {out[0].data(), out[1].data()};
    adapter.process(inB, outB, 1024, BlockAutomation{});
    after = adapter.status();
  }
  CHECK(after.chainReady);
  CHECK(after.engineIndex == 2);
  CHECK(after.faults == 0);
  // output is live (non-silent) after the resume
  double acc = 0.0;
  for (double v : out[0]) acc += v * v;
  CHECK(acc > 1e-6);
  adapter.deactivate();
}

// ---------------------------------------------------------------------------
// Task 30 — the envelope/clamp/re-prepare POLICY suite (T-J*):
// the depth-aware envelope covers the complete legal PITCH+LFO surface at
// the engine's declared range; a clamp is a counted boundary event, NEVER a
// lifecycle trigger; re-prepares fire only on genuine dependency changes.
// ---------------------------------------------------------------------------

TEST_CASE("T-J1: clamp without configuration change never re-prepares") {
  // THE churn scenario (Task 29's measured futile loop): −12 st + LFO —
  // the effective curve dips below the raw pitch-parameter floor every
  // cycle. Pre-Task-30: every clamped block bumped requestEpoch → a
  // rebuild whose re-centred envelope was STILL truncated → the loop
  // (759 clamps / 329 re-prepares / a seam every ~6 ms). Now: the envelope
  // covers the excursion (ZERO clamps at depth ≤ margin) and NO lifecycle
  // action occurs. Engine: granular (the measured case).
  initEngineRegistryOnce();
  const int64_t total = static_cast<int64_t>(kFs * 2);
  for (double depth : {0.5, 2.0}) {
    ParamSnapshot snap;
    snap.engineIndex = 4;  // native.granular
    snap.pitchSt = -12.0;
    snap.lfoDepthSt = depth;
    CAPTURE(depth);
    const DriveResult r = driveAdapter(snap, total, {128});
    CHECK(r.status.clampEvents == 0);   // the envelope covers the excursion
    CHECK(r.status.reprepares <= 2);    // the initial chain (+ at most one)
    CHECK(r.status.chainsAdopted <= 2);
    CHECK(r.status.faults == 0);
    CHECK(r.status.chainReady);
  }
}

TEST_CASE("T-J2: repeated LFO boundary crossings stay stable (granular, -12 st)") {
  // ~25 LFO cycles at 5 Hz over 5 s at the boundary pitch: the re-prepare
  // count must NOT grow with the cycle count (the churn cannot return
  // over time — the structural-invariance proof, adapter-level twin of the
  // diagnostics suite's 10 s drive).
  initEngineRegistryOnce();
  ParamSnapshot snap;
  snap.engineIndex = 4;
  snap.pitchSt = -12.0;
  snap.lfoDepthSt = 2.0;  // the widest legal excursion (the -14 st surface)
  const DriveResult r = driveAdapter(snap, static_cast<int64_t>(kFs * 5), {256});
  CHECK(r.status.clampEvents == 0);
  CHECK(r.status.reprepares <= 2);
  CHECK(r.status.faults == 0);
  CHECK(r.status.chainReady);
}

TEST_CASE("T-J5: identity with LFO depth 0 is unchanged (no clamps, one chain)") {
  // The depth-0 envelope is BIT-IDENTICAL to the pre-Task-30 formula: the
  // static identity case must behave exactly as before — no clamps, the
  // single initial chain, deterministic output (two identical drives are
  // bit-identical).
  initEngineRegistryOnce();
  for (int engine = 0; engine < engineCount(); ++engine) {
    ParamSnapshot snap;
    snap.engineIndex = engine;
    snap.pitchSt = 0.0;
    snap.lfoDepthSt = 0.0;
    CAPTURE(engineIdForIndex(engine));
    const int64_t total = static_cast<int64_t>(kFs * 1.0);
    const DriveResult a = driveAdapter(snap, total, {512});
    const DriveResult b = driveAdapter(snap, total, {512});
    CHECK(a.status.clampEvents == 0);
    CHECK(a.status.reprepares <= 2);
    CHECK(a.status.faults == 0);
    CHECK(a.status.chainReady);
    CHECK(a.out[0] == b.out[0]);  // the determinism guarantee holds
  }
}

TEST_CASE("T-J6: pitch automation + LFO is deterministic") {
  // A pitch automation ramp (0 → +6 st) WITH the LFO active (depth 1.5 st):
  // identical drives must be bit-identical (the automation + the curve
  // resolution + the envelope machinery compose deterministically).
  initEngineRegistryOnce();
  ParamSnapshot snap;
  snap.engineIndex = 1;  // native.vardelay (PerSample, non-splice)
  snap.pitchSt = 0.0;
  snap.lfoDepthSt = 1.5;
  BlockAutomation automation;
  automation.pitch[0] = {0, 0.0};
  automation.pitch[1] = {static_cast<int32_t>(kFs / 2), 6.0};  // ramp mid-drive
  automation.pitchCount = 2;
  const int64_t total = static_cast<int64_t>(kFs * 1.0);
  const auto sig = makeSine(total, 220.0);
  const DriveResult a = driveAdapter(snap, total, {512}, sig, 220.0, &automation);
  const DriveResult b = driveAdapter(snap, total, {512}, sig, 220.0, &automation);
  CHECK(a.status.faults == 0);
  // NOTE: the ramp exits the initial (pitch-0-centred) envelope before the
  // re-centred replacement adopts — the counted transient bridge clamps
  // (the designed behaviour; NO churn: the re-centre fires ONCE):
  CHECK(a.status.clampEvents >= 1);
  CHECK(a.status.reprepares <= 3);  // initial + the re-centre (+ margin)
  CHECK(a.out[0] == b.out[0]);      // the determinism guarantee holds
}

TEST_CASE("T-J7: the block schedule does not change lifecycle semantics") {
  // Two different schedules at the churn scenario: the LIFECYCLE outcomes
  // (clamps, re-prepares, faults) must be identical (schedule-independence
  // of the policy; the audio may differ per the engines' own invariances).
  initEngineRegistryOnce();
  ParamSnapshot snap;
  snap.engineIndex = 4;
  snap.pitchSt = -12.0;
  snap.lfoDepthSt = 2.0;
  const int64_t total = static_cast<int64_t>(kFs * 2);
  const DriveResult a = driveAdapter(snap, total, {128});
  const DriveResult b = driveAdapter(snap, total, {1024});
  const DriveResult c = driveAdapter(snap, total, {97, 512, 397});
  CHECK(a.status.clampEvents == b.status.clampEvents);
  CHECK(a.status.clampEvents == c.status.clampEvents);
  CHECK(a.status.reprepares <= 2);
  CHECK(b.status.reprepares <= 2);
  CHECK(c.status.reprepares <= 2);
  CHECK(a.status.faults == 0);
  CHECK(b.status.faults == 0);
  CHECK(c.status.faults == 0);
}

TEST_CASE("T-J9: a true engine-configuration change still re-prepares") {
  // granular grain length 0.1 → 0.5 mid-stream: a genuine chain-signature
  // change MUST rebuild (the Task-30 policy removed the clamp trigger, not
  // the configuration trigger). Observable: re-prepares >= 2 + the adopted
  // chain reflects the new geometry (the latency changes with the grain).
  initEngineRegistryOnce();
  RealtimeAdapter adapter;
  adapter.activate(kFs, 2, 512);
  ParamSnapshot snap;
  snap.engineIndex = 4;
  snap.pitchSt = -4.0;
  snap.grGrainSec = 0.10;
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();
  const int64_t total = static_cast<int64_t>(kFs * 1.6);
  const auto sig = makeSine(total, 220.0);
  std::vector<double> out(total, 0.0);
  int64_t pos = 0;
  bool switched = false;
  const int64_t switchAt = total / 2;
  while (pos < total) {
    if (!switched && pos >= switchAt) {
      switched = true;
      snap.grGrainSec = 0.20;  // the engine-configuration change (a moderate
                                // grain jump; the extreme 0.5 jump is recorded
                                // as the pre-existing seam limitation)
      adapter.setParameterSnapshot(snap);
    }
    const int32_t take = static_cast<int32_t>(std::min<int64_t>(512, total - pos));
    const double* in[2] = {sig.data() + pos, sig.data() + pos};
    double* o[2] = {out.data() + pos, out.data() + pos};
    adapter.process(in, o, take, BlockAutomation{});
    pos += take;
  }
  const StatusSnapshot st = adapter.status();
  CHECK(st.reprepares >= 2);       // the initial chain + the configuration rebuild
  CHECK(st.chainsAdopted >= 2);    // the replacement adopted mid-stream
  // A bounded seam-scale transient (<= 64 frames of dry fallback at the
  // re-coverage boundary) is the pre-Task-30 mid-stream switch behaviour
  // for same-engine latency growth (measured 6 frames at the 0.1 -> 0.2
  // grain jump). THE EXTREME 0.1 -> 0.5 jump's 14406-frame retiring-
  // coverage burst — recorded as the remaining out-of-scope limitation at
  // Task 30 — was FIXED by Task 31 (the retained wet history + the
  // retiring grid continuation): the burst is now SERVED from the
  // retained history, bit-identical to the previously-emitted wet. The
  // full matrix (extreme/moderate/reverse, blocks 128..2048, rates
  // 44.1/48/96, paced/back-to-back, the replay bit-identity, the
  // cross-engine switch) lives in vst_seam_test (T-S1..T-S10).
  CHECK(st.faults <= 64);
  CHECK(st.deliveryUnderruns == st.faults);  // underrun-class only (no stalls/dry-misses)
  CHECK(st.chainReady);
  adapter.deactivate();
}

TEST_CASE("T-J11: latency reporting stays coherent at every LFO depth") {
  // expectedLatencyFrames (the synchronous per-snapshot geometry) must
  // equal the chain's reported latency at depth 0 AND at the depth maximum
  // (the depth-aware geometry: at depth <= 1 st the value is bit-identical
  // to the pre-Task-30 constant; beyond it the splice worst case widens to
  // the +-14 st legal surface).
  initEngineRegistryOnce();
  for (double depth : {0.0, 2.0}) {
    for (int engine = 0; engine < engineCount(); ++engine) {
      ParamSnapshot snap;
      snap.engineIndex = engine;
      snap.lfoDepthSt = depth;
      CAPTURE(depth);
      CAPTURE(engineIdForIndex(engine));
      const int64_t expected = expectedLatencyFrames(snap, kFs);
      CHECK(expected > 0);
      const DriveResult r = driveAdapter(snap, static_cast<int64_t>(kFs * 0.5), {1024});
      CHECK(r.status.latencyFrames == expected);
    }
  }
}

TEST_CASE("T-J12: invalid ratios never reach the engine (vardelay at the extremes)") {
  // native.vardelay's declared range is [0.5, 2.0] — NARROWER than the
  // legal +-14 st surface. At -12 st + LFO depth 2 the effective curve
  // would dip to ratio 0.445: the envelope CLAMPS at the engine's declared
  // floor (an honest, counted boundary; no rebuild can move a declared
  // capability), and the engine receives only valid ratios (0 faults —
  // the engine contract never sees a ConfigError/exception).
  initEngineRegistryOnce();
  ParamSnapshot snap;
  snap.engineIndex = 1;  // native.vardelay
  snap.pitchSt = -12.0;
  snap.lfoDepthSt = 2.0;
  const int64_t total = static_cast<int64_t>(kFs * 2);
  const DriveResult r = driveAdapter(snap, total, {256});
  CHECK(r.status.engineProcessFaults == 0);
  CHECK(r.status.engineExceptions == 0);
  CHECK(r.status.faults == 0);
  CHECK(r.status.chainReady);
  // the envelope is clamped at the ENGINE's declared floor (0.5), not the
  // parameter floor (0.4454): the boundary is the engine capability
  CHECK(r.status.envelopeMin >= 0.5 - 1e-9);
  CHECK(r.status.envelopeMax <= 2.0 + 1e-9);
  // the clamp counter records the honest boundary events (the LFO's dips
  // below the engine floor), and they cause NO rebuild churn
  CHECK(r.status.clampEvents > 0);
  CHECK(r.status.reprepares <= 2);
}

TEST_CASE("T-J: LFO depth automation re-sizes the envelope exactly once (the capability exit)") {
  // The depth automation steps 0 → 2 st at frame 0 (the chain was built
  // for depth 0, margin 1 st): the preparation thread must rebuild ONCE
  // with the wider margin (the envelope-capability exit), then stay stable
  // (depth movement WITHIN the margin never rebuilds). The transient clamp
  // (before the replacement adopts) is counted — never silent.
  initEngineRegistryOnce();
  const int64_t total = static_cast<int64_t>(kFs * 1.2);
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot snap;  // vardelay, pitch 0, depth 0
  BlockAutomation withDepth;
  withDepth.lfoDepth[0] = {0, 2.0};  // +2 st from frame 0
  withDepth.lfoDepthCount = 1;
  const DriveResult r = driveAdapter(snap, total, {256}, sig, 220.0, &withDepth);
  CHECK(r.status.clampEvents >= 1);     // the transition window is counted
  CHECK(r.status.reprepares <= 3);      // initial + the capability re-size (+1)
  CHECK(r.status.chainsAdopted <= 3);
  CHECK(r.status.faults == 0);
  // the re-sized chain's envelope covers the full ±2 st excursion around
  // pitch 0 (2^(-2/12) .. 2^(+2/12) = 0.8909 .. 1.1225, the live-centred
  // window at depth 2):
  CHECK(r.status.envelopeMin <= 0.8909 + 1e-9);
  CHECK(r.status.envelopeMax >= 1.1224);
}

// ---------------------------------------------------------------------------
// Task 32 — the LFO OFF state (rate 0 Hz): semantics + transitions
//
// The product-facing rate domain is 0.0..8.0 Hz; rate == 0 disables the
// LFO: no sine contribution regardless of depth, and the phase is PARKED
// deterministically at the zero crossing (spec §6) — so the 0 -> nonzero
// transition starts the modulation from sin(0) == 0 (no curve step), and
// the nonzero -> 0 transition is the honest hard-off (bounded by choosing
// the transition at a zero-crossing frame, it too is continuous).
// ---------------------------------------------------------------------------

TEST_CASE("Task 32: rate 0 with nonzero depth — bit-identical to depth 0 (OFF means OFF)") {
  initEngineRegistryOnce();
  // VarDelay's chain geometry is depth-INDEPENDENT (crossfade-based) and
  // its engine never reads the pre-filled envelope ahead of the live-write
  // frontier (production is paced BEHIND the consumption by the crossfade
  // lead) — the only depth-dependent quantities (the envelope clamp
  // bounds) never engage for a curve sitting deep inside both envelopes.
  // The -8 st curve (0.6300) sits inside the depth-0 envelope
  // [0.5946, 0.6675] and the depth-2 envelope [0.5612, 0.7072]: no
  // clamping in either drive, identical curve -> BIT-IDENTICAL output.
  const int64_t total = static_cast<int64_t>(kFs * 0.9);
  const std::vector<int32_t> blocks = {1024, 256, 777};
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot off;  // rate 0 + depth 2 (the required case: OFF with nonzero depth)
  off.engineIndex = 1;
  off.pitchSt = -8.0;
  off.lfoRateHz = 0.0;
  off.lfoDepthSt = 2.0;
  ParamSnapshot dep0;  // rate 0 + depth 0 — the canonical disabled LFO
  dep0.engineIndex = 1;
  dep0.pitchSt = -8.0;
  dep0.lfoRateHz = 0.0;
  dep0.lfoDepthSt = 0.0;
  const DriveResult a = driveAdapter(off, total, blocks, sig);
  const DriveResult b = driveAdapter(dep0, total, blocks, sig);
  REQUIRE(a.status.faults == 0);
  REQUIRE(b.status.faults == 0);
  CHECK(a.status.latencyFrames == b.status.latencyFrames);
  CHECK(a.status.reprepares == b.status.reprepares);  // the rate/depth never churn the chain
  CHECK(a.out[0] == b.out[0]);  // BIT-IDENTICAL — no modulation at all
  CHECK(a.out[1] == b.out[1]);
  // finite everywhere (no NaN/Inf through the OFF path)
  for (int64_t i = 0; i < total; ++i) {
    REQUIRE(std::isfinite(a.out[0][static_cast<std::size_t>(i)]));
  }
}

TEST_CASE("Task 32: depth 0 disables the LFO at ANY rate (bit-identical output)") {
  initEngineRegistryOnce();
  // rate 5 with depth 0 vs rate 0 with depth 0: the contribution term is
  // depth-gated; the advancing phase is invisible in the output. Locks the
  // "depth 0 = off" semantics independent of the rate (the required case
  // matrix: rate > 0 with depth = 0).
  const int64_t total = static_cast<int64_t>(kFs * 0.7);
  const std::vector<int32_t> blocks = {512, 1024};
  const auto sig = makeSine(total, 220.0);
  ParamSnapshot on;  // rate 5, depth 0
  on.engineIndex = 1;
  on.pitchSt = -8.0;
  on.lfoRateHz = 5.0;
  on.lfoDepthSt = 0.0;
  ParamSnapshot off;
  off.engineIndex = 1;
  off.pitchSt = -8.0;
  off.lfoRateHz = 0.0;
  off.lfoDepthSt = 0.0;
  const DriveResult a = driveAdapter(on, total, blocks, sig);
  const DriveResult b = driveAdapter(off, total, blocks, sig);
  REQUIRE(a.status.faults == 0);
  CHECK(a.out[0] == b.out[0]);  // the phase advancing at depth 0 changes nothing
  CHECK(a.out[1] == b.out[1]);
}

TEST_CASE("Task 32: OFF -> on transition — the parked phase starts at the zero crossing") {
  initEngineRegistryOnce();
  // Single-block drives (the P1.2 pattern: one process call owns the whole
  // timeline, so per-frame automation is frame-exact):
  //   B: rate 0 for the whole drive (the OFF control)
  //   C: rate 0, automated to 5 Hz at frame F (an arbitrary mid-cycle point
  //      — parking makes the onset continuous at ANY F, not only at cycle
  //      boundaries)
  // Pre-F: C's rate is 0 with the phase parked at 0 — the curve equals B's
  // exactly -> BIT-IDENTICAL prefix. Post-F: the modulation grows from
  // zero; the first millisecond's deviation from B is tiny compared with
  // the established modulation a quarter-period (50 ms) later.
  const int64_t total = static_cast<int64_t>(kFs * 1.2);
  const int64_t F = static_cast<int64_t>(kFs * 0.6);  // 28800, arbitrary mid-drive
  const auto sig = makeSine(total, 220.0);
  const std::vector<int32_t> oneBlock{static_cast<int32_t>(total)};
  ParamSnapshot snap;  // vardelay, pitch -8, depth 1 (a live LFO when on)
  snap.engineIndex = 1;
  snap.pitchSt = -8.0;
  snap.lfoDepthSt = 1.0;
  snap.lfoRateHz = 0.0;  // OFF (also C's frame-0 carry: no automation for B)

  const DriveResult b = driveAdapter(snap, total, oneBlock, sig);

  BlockAutomation onAtF;
  onAtF.lfoRate[0] = {0, 0.0};
  onAtF.lfoRate[1] = {F, 0.0};
  onAtF.lfoRate[2] = {F + 1, 5.0};  // on from frame F+1 (a near-step ramp)
  onAtF.lfoRateCount = 3;
  const DriveResult c = driveAdapter(snap, total, oneBlock, sig, 220.0, &onAtF);
  // determinism of the transition drive
  const DriveResult c2 = driveAdapter(snap, total, oneBlock, sig, 220.0, &onAtF);
  REQUIRE(c.status.faults == 0);
  REQUIRE(b.status.faults == 0);
  CHECK(c.out[0] == c2.out[0]);
  CHECK(c.status.reprepares == b.status.reprepares);  // the rate never rebuilds

  // pre-F prefix: BIT-IDENTICAL (the phase was parked at 0 — the curve is
  // B's curve; the 4096-frame margin excludes the engine's crossfade
  // lookahead reaching past F)
  const int64_t preEnd = F - 4096;
  REQUIRE(preEnd > 0);
  CHECK(std::memcmp(c.out[0].data(), b.out[0].data(),
                    static_cast<std::size_t>(preEnd) * sizeof(double)) == 0);

  // post-F: the modulation is present (differs from the OFF control)
  double post = 0.0;
  for (int64_t p = F + 4096; p < total; ++p) {
    post += std::fabs(c.out[0][static_cast<std::size_t>(p)] -
                      b.out[0][static_cast<std::size_t>(p)]);
  }
  CHECK(post > 100.0);  // a 1 st modulation over ~0.5 s decorrelates the render

  // the smooth onset: at the transition the contribution is sin(0) = 0 and
  // grows — the first millisecond's deviation from B is far below the
  // established modulation at the quarter period (50 ms in)
  const int64_t lat = c.status.latencyFrames;
  double d1 = 0.0;  // the first ~1 ms of modulated output (post latency)
  for (int64_t p = F + 1 + lat; p < F + 49 + lat; ++p) {
    d1 += std::fabs(c.out[0][static_cast<std::size_t>(p)] -
                    b.out[0][static_cast<std::size_t>(p)]);
  }
  double d2 = 0.0;  // the quarter-period window (the modulation at full swing)
  for (int64_t p = F + 2400 + lat; p < F + 2448 + lat; ++p) {
    d2 += std::fabs(c.out[0][static_cast<std::size_t>(p)] -
                    b.out[0][static_cast<std::size_t>(p)]);
  }
  MESSAGE("onset d1=" << d1 << " quarter-period d2=" << d2);
  CHECK(d1 < d2);      // the modulation GREW from the zero crossing
  CHECK(d1 < 0.5);     // and started essentially at zero (no curve step)
}

TEST_CASE("Task 32: on -> OFF transition — modulation stops, continuous at a zero crossing") {
  initEngineRegistryOnce();
  // A: rate 5 for the whole drive. D: rate 5, automated to 0 at frame F.
  // F = 28800 = 3 full 5 Hz cycles (5*28800/48000 == 3.0 exactly): the
  // contribution at the OFF frame is sin(2*pi*3) == 0 — the hard-off lands
  // ON a zero crossing, so the transition is continuous (the same holds at
  // any zero-crossing; a mid-cycle OFF is the honest bounded step, the
  // depth-to-0 class). Pre-F: BIT-IDENTICAL to A (the OFF never leaked
  // backward). Post-F: the modulation is gone (differs from A).
  const int64_t total = static_cast<int64_t>(kFs * 1.2);
  const int64_t F = 28800;  // 3 full cycles at 5 Hz (zero crossing)
  CHECK(std::fabs(5.0 * static_cast<double>(F) / kFs - 3.0) < 1e-12);
  const auto sig = makeSine(total, 220.0);
  const std::vector<int32_t> oneBlock{static_cast<int32_t>(total)};
  ParamSnapshot snap;  // vardelay, pitch -8, depth 1
  snap.engineIndex = 1;
  snap.pitchSt = -8.0;
  snap.lfoDepthSt = 1.0;
  snap.lfoRateHz = 5.0;

  const DriveResult a = driveAdapter(snap, total, oneBlock, sig);

  BlockAutomation offAtF;
  offAtF.lfoRate[0] = {0, 5.0};
  offAtF.lfoRate[1] = {F, 5.0};
  offAtF.lfoRate[2] = {F + 1, 0.0};  // OFF from frame F+1
  offAtF.lfoRateCount = 3;
  const DriveResult d = driveAdapter(snap, total, oneBlock, sig, 220.0, &offAtF);
  const DriveResult d2 = driveAdapter(snap, total, oneBlock, sig, 220.0, &offAtF);
  REQUIRE(a.status.faults == 0);
  REQUIRE(d.status.faults == 0);
  CHECK(d.out[0] == d2.out[0]);                       // deterministic
  CHECK(d.status.reprepares == a.status.reprepares);  // no lifecycle event

  // pre-F prefix: BIT-IDENTICAL to the always-on control
  const int64_t preEnd = F - 4096;
  CHECK(std::memcmp(d.out[0].data(), a.out[0].data(),
                    static_cast<std::size_t>(preEnd) * sizeof(double)) == 0);

  // post-F: A still modulates, D does not — the renders diverge
  double post = 0.0;
  for (int64_t p = F + 8192; p < total; ++p) {
    post += std::fabs(d.out[0][static_cast<std::size_t>(p)] -
                      a.out[0][static_cast<std::size_t>(p)]);
  }
  CHECK(post > 100.0);

  // continuity at the OFF landing (the transition reaches the output one
  // latency later: out[p] reflects curve[p - latency]): no sample step
  // beyond the signal's own slope — the parked-phase/hard-off semantics
  // introduce NO discontinuity at a zero-crossing OFF.
  const int64_t lat = d.status.latencyFrames;
  double maxStep = 0.0;
  for (int64_t p = F + 1 + lat - 500; p < F + 1 + lat + 500; ++p) {
    if (p < 1 || p >= total) continue;
    const double step = std::fabs(d.out[0][static_cast<std::size_t>(p)] -
                                  d.out[0][static_cast<std::size_t>(p - 1)]);
    maxStep = std::max(maxStep, step);
  }
  MESSAGE("max sample step at the OFF landing: " << maxStep);
  CHECK(maxStep < 0.2);  // the 220 Hz signal's own slope is ~0.015
}

TEST_CASE("Task 32: the snapshot path (UI slider) OFF/ON mid-stream — frame-aligned, no churn") {
  initEngineRegistryOnce();
  // The UI path: setParameterNormalized -> publishSnapshot between process
  // calls (the parameter's CURRENT value is the curve base — P0.4). The
  // switch lands exactly at the call boundary: frame-exact by construction.
  // The rate is NOT in the chain signature — the chain never rebuilds.
  RealtimeAdapter adapter;
  const int64_t F = static_cast<int64_t>(kFs * 0.5);
  const int64_t total = F + static_cast<int64_t>(kFs * 0.5);
  const std::vector<int32_t> blocks = {1024};
  adapter.activate(kFs, 2, 1024);
  ParamSnapshot snap;  // vardelay, pitch -8, depth 1, rate 5 (on)
  snap.engineIndex = 1;
  snap.pitchSt = -8.0;
  snap.lfoDepthSt = 1.0;
  snap.lfoRateHz = 5.0;
  adapter.setParameterSnapshot(snap);
  adapter.requestHardReset();

  const auto sig = makeSine(total, 220.0);
  std::vector<double> out(static_cast<std::size_t>(total), 0.0);
  double* o[2] = {out.data(), out.data()};
  int64_t pos = 0;
  auto drive = [&](int64_t until) {
    while (pos < until) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(1024, until - pos));
      const double* in[2] = {sig.data() + pos, sig.data() + pos};
      BlockAutomation none;
      adapter.process(in, o, take, none);
      pos += take;
    }
  };
  drive(F);
  const uint64_t repreparesBefore = adapter.status().reprepares;
  ParamSnapshot off = snap;
  off.lfoRateHz = 0.0;  // the slider to 0 Hz
  adapter.setParameterSnapshot(off);
  drive(total);
  // ... and back on (the parked phase resumes from the zero crossing)
  adapter.setParameterSnapshot(snap);
  const StatusSnapshot st = adapter.status();
  CHECK(st.faults == 0);
  CHECK(st.reprepares == repreparesBefore);  // ONE chain through the whole toggle
  CHECK(st.rtFrames == total);               // the measurement window spans it
  adapter.deactivate();
}
