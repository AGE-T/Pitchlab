// T-PY suite (implementation specification §10.5 item 12, cycle 6, OD-12):
// the clean-room C++ pYIN ReferencePitchTracker's own contract.
//
// Independent analytic anchors (the §10.5 golden discipline — NEVER
// expected = tracker(actual)):
//   * Integer-period sine 500 Hz @ 48 kHz: period 96 EXACTLY; the signal is
//     exactly periodic at tau = 96 (and its multiples); the FIRST in-band
//     trough below every threshold is tau = 96 => the threshold mass
//     concentrates on the fundamental => the track is 500 Hz, voiced.
//   * Pure tones 440/220/120 Hz: the same periodicity argument at their
//     (non-integer) periods; the sub-bin value = fs/(tau + parabolic delta).
//   * Zeros: d' == 1 everywhere (the CMNDF guard) => no trough below any
//     threshold => V = 0, U = 1 => ALL frames unvoiced EXACTLY.
//   * White noise: no in-band lag periodicity at W = N - tauMax samples;
//     the CMNDF stays near 1 (fluctuations far above the Beta(2,18) mass
//     at small thresholds) => overwhelmingly unvoiced (empirical bracket).
//   * Step 220 -> 440 Hz: 12 st = 120 bins; the triangular band allows 40
//     bins/frame => the Viterbi path climbs in >= 3 frames; the settle
//     window is an EMPIRICAL class (documented per the §10.5 item 12 rule).
//   * Vibrato: f0(t) = 220 * 2^(0.5 * sin(2*pi*5*t)/12) — the analytic
//     corpus recipe; the tracker's window-averaged period estimate tracks
//     the instantaneous frequency at the frame center (second-order window
//     bias ~ cents; empirical band documented).
//   * Above-band pure 1500 Hz: exactly periodic at tau = 64 (= 2*32, IN the
//     band) => the first-dip rule locks onto the in-band subharmonic 750 Hz
//     (§10.5 item 3's correction — analytically derivable periodicity).
//   * Determinism: two identical calls => bit-identical frames (memcmp on
//     the POD payload; voicedProbability compared bit-exactly).
//   * Rates: the same 500 Hz tone at 44.1/48/96/192 kHz tracks at 500 Hz
//     within the cross-rate band (the per-rate window rule: N = 2048/4096/
//     8192 — the SAME 42.67-46.4 ms time window at every rate).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "analysis/pitch_tracker.h"

using namespace pitchlab::analysis;
using doctest::Approx;

namespace {

const double kPi = 3.14159265358979323846;

/// Phase-accumulated sine at an INSTANTANEOOUS frequency schedule (the
/// corpus generator's synthesis form — fInst evaluated per sample).
[[nodiscard]] std::vector<double> sineWithFInst(
    const std::function<double(double)>& fInst, uint32_t fs, int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  double phase = 0.0;  // cycles, wrapped (the corpus convention)
  for (int64_t i = 0; i < n; ++i) {
    const double f = fInst(static_cast<double>(i) / static_cast<double>(fs));
    phase += f / static_cast<double>(fs);
    phase = std::fmod(phase, 1.0);
    if (phase < 0.0) phase += 1.0;
    x[static_cast<std::size_t>(i)] = 0.25 * std::sin(2.0 * kPi * phase);
  }
  return x;
}

[[nodiscard]] std::vector<double> constantSine(double fHz, uint32_t fs, int64_t n) {
  return sineWithFInst([fHz](double) { return fHz; }, fs, n);
}

[[nodiscard]] double centsBetween(double fMeasured, double fExpected) {
  return 1200.0 * std::log2(fMeasured / fExpected);
}

/// A simple deterministic LCG noise source (test-only; no <random> entropy).
[[nodiscard]] std::vector<double> whiteNoise(uint64_t seed, int64_t n) {
  std::vector<double> x(static_cast<std::size_t>(n));
  uint64_t s = seed;
  for (int64_t i = 0; i < n; ++i) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    const double u = static_cast<double>(s >> 11) / 9007199254740992.0;
    x[static_cast<std::size_t>(i)] = u * 2.0 - 1.0;
  }
  return x;
}

[[nodiscard]] int64_t voicedCountOf(const PitchTrack& t) {
  int64_t v = 0;
  for (const PitchTrackFrame& f : t.frames) {
    if (f.voiced) ++v;
  }
  return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Clean pitched material
// ---------------------------------------------------------------------------

TEST_CASE("T-PY1: integer-period sine 500 Hz @ 48 kHz — voiced, 500 Hz "
          "within the empirical sub-bin band") {
  // 500 Hz => period 96 samples EXACTLY. 1 s of signal => N = 2048,
  // hop 512 => 90 frames.
  const std::vector<double> x = constantSine(500.0, 48000, 48000);
  const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  CHECK(t.windowFrames == 2048);
  CHECK(t.hop == 512);
  REQUIRE(t.frameCount == 90);
  // Evidence-center timestamp: start + (W + tau_hat)/2 — for 500 Hz (tau = 96,
  // W = 1088): 592 (llround(592.0)); frame 0 start = 0.
  CHECK(t.frames[0].center == 592);
  // Frame 0: the init convention gives voiced states the 1e-30 floor — a
  // decisively voiced observation (U ~ 0, so the unvoiced states' own
  // observation floors to 1e-30/K) lets the voiced path win at frame 0; on
  // ambiguous frames (U > 0) frame 0 decodes unvoiced. NOT asserted either
  // way — the honest documented behavior (§10.5 item 7 as corrected at
  // implementation time).
  // From frame 1: voiced, 500 Hz within the documented PROVISIONAL/EMPIRICAL
  // band (10 cents — the parabolic-interpolation + 10-cent-bin class; the
  // integer-period trough has delta ~ 0, measured value recorded in the
  // suite output).
  double worstCents = 0.0;
  int64_t voiced = 0;
  for (int64_t f = 0; f < t.frameCount; ++f) {
    if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
    ++voiced;
    const double c = std::fabs(centsBetween(t.frames[static_cast<std::size_t>(f)].f0Hz, 500.0));
    worstCents = std::max(worstCents, c);
  }
  std::printf("T-PY1: 500 Hz sine: voiced %lld/90, worst |cents| = %.4f\n",
              static_cast<long long>(voiced), worstCents);
  CHECK(voiced >= 85);
  CHECK(worstCents <= 10.0);  // PROVISIONAL/EMPIRICAL class band (documented)
}

TEST_CASE("T-PY2: pure tones across the corpus fundamental classes — "
          "440/220/120 Hz tracked") {
  const double cases[][2] = {{440.0, 48000.0}, {220.0, 48000.0}, {120.0, 48000.0}};
  for (const auto& c : cases) {
    const std::vector<double> x = constantSine(c[0], 48000, 48000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    int64_t voiced = 0;
    double worst = 0.0;
    for (int64_t f = 1; f < t.frameCount; ++f) {
      if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
      ++voiced;
      worst = std::max(worst, std::fabs(centsBetween(
                                  t.frames[static_cast<std::size_t>(f)].f0Hz, c[0])));
    }
    std::printf("T-PY2: %.0f Hz: voiced %lld, worst |cents| = %.4f\n", c[0],
                static_cast<long long>(voiced), worst);
    CHECK(voiced >= 85);
    CHECK(worst <= 15.0);  // non-integer periods: the interpolation-bias class
  }
}

// ---------------------------------------------------------------------------
// Unvoiced / silent material
// ---------------------------------------------------------------------------

TEST_CASE("T-PY3: silence — ALL frames unvoiced exactly; voicing "
          "probability ~ 0") {
  const std::vector<double> x(48000, 0.0);
  const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  REQUIRE(t.frameCount == 90);
  CHECK(voicedCountOf(t) == 0);  // exact: d' == 1 => no trough below any threshold
  for (const PitchTrackFrame& f : t.frames) {
    CHECK(f.voicedProbability < 1.0e-9);
  }
}

TEST_CASE("T-PY4: white noise — overwhelmingly unvoiced (empirical bracket)") {
  // 1 s of deterministic white noise: no in-band lag periodicity; the CMNDF
  // stays near 1. Empirical class: voiced fraction well below 10% (measured
  // value printed; the honest bracket documents the estimator's behaviour
  // on aperiodic material, never a hard golden).
  const std::vector<double> x = whiteNoise(0xC0FFEE, 48000);
  const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  REQUIRE(t.frameCount == 90);
  const double frac = static_cast<double>(voicedCountOf(t)) / 90.0;
  std::printf("T-PY4: white noise: voiced fraction = %.4f\n", frac);
  CHECK(frac <= 0.10);  // PROVISIONAL/EMPIRICAL bracket
}

// ---------------------------------------------------------------------------
// Pitch changes (the HMM climb)
// ---------------------------------------------------------------------------

TEST_CASE("T-PY5: step 220 -> 440 Hz — settles within the documented frame "
          "window (the triangular-band climb)") {
  // 0.5 s at 220 Hz, then 0.5 s at 440 Hz. 12 st = 120 bins; the band allows
  // 40 bins/frame => >= 3 move frames; the settle window is the empirical
  // class (measured, asserted <= 20 frames = 213 ms).
  std::vector<double> x = constantSine(220.0, 48000, 24000);
  {
    const std::vector<double> hi = constantSine(440.0, 48000, 24000);
    x.insert(x.end(), hi.begin(), hi.end());
  }
  const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  REQUIRE(t.frameCount == 90);
  // The step sits at sample 24000 => between frame centers 44 (23424) and 45
  // (23936)... 45*512+1024 = 24064. Find the first frame after which the
  // track stays voiced within 15 cents of the LOCAL expected fundamental
  // (220 before the step region, 440 after).
  const int64_t settleLimit = 20;
  int64_t lastBad = -1;
  for (int64_t f = 1; f < t.frameCount; ++f) {
    const double expected = (t.frames[static_cast<std::size_t>(f)].center < 24000) ? 220.0 : 440.0;
    // Frames whose window straddles the step (center within one window of
    // the step) are transitional — skip them in the settle accounting.
    if (std::llabs(t.frames[static_cast<std::size_t>(f)].center - 24000) < 2048) continue;
    const bool ok = t.frames[static_cast<std::size_t>(f)].voiced &&
                    std::fabs(centsBetween(t.frames[static_cast<std::size_t>(f)].f0Hz, expected)) <= 15.0;
    if (!ok && f > 46 + settleLimit) lastBad = f;  // beyond any honest settle window
  }
  std::printf("T-PY5: step 220->440: last unsettled frame (past the window) = %lld\n",
              static_cast<long long>(lastBad));
  CHECK(lastBad == -1);
  // And the tail (past the step + window) is solidly at 440:
  int64_t voiced440 = 0;
  int64_t counted = 0;
  for (int64_t f = 0; f < t.frameCount; ++f) {
    if (t.frames[static_cast<std::size_t>(f)].center < 24000 + 2048) continue;
    ++counted;
    if (t.frames[static_cast<std::size_t>(f)].voiced &&
        std::fabs(centsBetween(t.frames[static_cast<std::size_t>(f)].f0Hz, 440.0)) <= 15.0) {
      ++voiced440;
    }
  }
  CHECK(counted > 30);
  CHECK(voiced440 >= counted - 2);  // at most 2 stray frames (empirical)
}

// ---------------------------------------------------------------------------
// Vibrato tracking (the analytic corpus recipe)
// ---------------------------------------------------------------------------

TEST_CASE("T-PY6: vibrato — tracks the analytic f0(t) at frame centers") {
  // f0(t) = 220 * 2^(0.5 * sin(2*pi*5*t)/12) — the syn-harmonic-saw recipe,
  // synthesised with the corpus generator's phase-accumulated form. The
  // tracker's window-averaged period estimate follows the instantaneous
  // frequency at the frame center (second-order window bias: the sinc
  // attenuation of a 5 Hz modulation over a 42.7 ms window is ~6% of the
  // 0.5 st depth ~ 3 cents; the empirical band is 20 cents).
  const std::vector<double> x = sineWithFInst(
      [](double t) {
        return 220.0 * std::exp2(0.5 * std::sin(2.0 * kPi * 5.0 * t) / 12.0);
      },
      48000, 48000);
  const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  double worst = 0.0;
  int64_t voiced = 0;
  for (int64_t f = 1; f < t.frameCount; ++f) {
    const PitchTrackFrame& fr = t.frames[static_cast<std::size_t>(f)];
    if (!fr.voiced) continue;
    ++voiced;
    const double tt = static_cast<double>(fr.center) / 48000.0;
    const double expected = 220.0 * std::exp2(0.5 * std::sin(2.0 * kPi * 5.0 * tt) / 12.0);
    worst = std::max(worst, std::fabs(centsBetween(fr.f0Hz, expected)));
  }
  std::printf("T-PY6: vibrato: voiced %lld/89, worst |cents| vs analytic = %.4f\n",
              static_cast<long long>(voiced), worst);
  CHECK(voiced >= 85);
  CHECK(worst <= 20.0);  // PROVISIONAL/EMPIRICAL (the window-average class)
}

// ---------------------------------------------------------------------------
// Confidence / probability semantics
// ---------------------------------------------------------------------------

TEST_CASE("T-PY7: confidence semantics — voiced flag + forward-backward "
          "posterior agree on clean and silent material") {
  {
    const std::vector<double> x = constantSine(500.0, 48000, 48000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    for (int64_t f = 1; f < t.frameCount; ++f) {
      const PitchTrackFrame& fr = t.frames[static_cast<std::size_t>(f)];
      if (fr.voiced) {
        CHECK(fr.voicedProbability > 0.5);  // the flag implies a majority posterior
      }
    }
    // The settled interior carries high confidence (empirical floor 0.9).
    int64_t high = 0;
    int64_t interior = 0;
    for (int64_t f = 5; f < t.frameCount - 5; ++f) {
      ++interior;
      if (t.frames[static_cast<std::size_t>(f)].voicedProbability >= 0.9) ++high;
    }
    std::printf("T-PY7: clean 500 Hz: interior frames with posterior >= 0.9: "
                "%lld/%lld\n",
                static_cast<long long>(high), static_cast<long long>(interior));
    CHECK(high >= interior - 2);
  }
  {
    const std::vector<double> x(48000, 0.0);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    for (const PitchTrackFrame& fr : t.frames) {
      CHECK_FALSE(fr.voiced);
      CHECK(fr.voicedProbability < 1.0e-9);
      CHECK(fr.f0Hz == 0.0);  // unvoiced frames carry 0 (documented semantics)
    }
  }
}

// ---------------------------------------------------------------------------
// Boundary behaviour
// ---------------------------------------------------------------------------

TEST_CASE("T-PY8: boundaries — short signal, band edges, above-band "
          "subharmonic lock") {
  // (a) Signal shorter than one window: zero frames, tail counted.
  {
    const std::vector<double> x = constantSine(500.0, 48000, 1000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    CHECK(t.frameCount == 0);
    CHECK(t.frames.empty());
    CHECK(t.droppedTailFrames == 1000);
  }
  // (b) Band edge 50 Hz: period 960 = tauMax EXACTLY (the boundary local-min
  // rule, left neighbour only). 2 s so there are interior frames.
  {
    const std::vector<double> x = constantSine(50.0, 48000, 96000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    int64_t voiced = 0;
    double worst = 0.0;
    for (int64_t f = 1; f < t.frameCount; ++f) {
      if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
      ++voiced;
      worst = std::max(worst, std::fabs(centsBetween(
                                  t.frames[static_cast<std::size_t>(f)].f0Hz, 50.0)));
    }
    std::printf("T-PY8b: 50 Hz edge: voiced %lld, worst |cents| = %.4f\n",
                static_cast<long long>(voiced), worst);
    CHECK(voiced >= 100);
    CHECK(worst <= 15.0);
  }
  // (c) Band edge 1000 Hz: period 48 = tauMin.
  {
    const std::vector<double> x = constantSine(1000.0, 48000, 48000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    int64_t voiced = 0;
    double worst = 0.0;
    for (int64_t f = 1; f < t.frameCount; ++f) {
      if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
      ++voiced;
      worst = std::max(worst, std::fabs(centsBetween(
                                  t.frames[static_cast<std::size_t>(f)].f0Hz, 1000.0)));
    }
    std::printf("T-PY8c: 1000 Hz edge: voiced %lld, worst |cents| = %.4f\n",
                static_cast<long long>(voiced), worst);
    CHECK(voiced >= 85);
    CHECK(worst <= 15.0);
  }
  // (d) Above-band pure 1500 Hz: EXACTLY periodic at tau = 64 (2 * 32) — IN
  // the band; the first-dip rule locks onto the 750 Hz subharmonic (§10.5
  // item 3's correction: the search range constrains the lag range, never
  // harmonicity — analytically derivable, not a defect).
  {
    const std::vector<double> x = constantSine(1500.0, 48000, 48000);
    const PitchTrack t = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    int64_t voiced750 = 0;
    int64_t voiced = 0;
    for (int64_t f = 1; f < t.frameCount; ++f) {
      if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
      ++voiced;
      if (std::fabs(centsBetween(t.frames[static_cast<std::size_t>(f)].f0Hz, 750.0)) <= 15.0) {
        ++voiced750;
      }
    }
    std::printf("T-PY8d: 1500 Hz pure tone: voiced %lld, at the 750 Hz in-band "
                "subharmonic %lld\n",
                static_cast<long long>(voiced), static_cast<long long>(voiced750));
    CHECK(voiced750 >= voiced - 2);
    CHECK(voiced >= 85);
  }
  // (e) Degenerate parameters: empty track, never throws.
  {
    const PitchTrack t = trackPitch(constantSine(500.0, 48000, 48000), 48000.0, 1000.0, 50.0);
    CHECK(t.frameCount == 0);
    const PitchTrack t2 = trackPitch({}, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
    CHECK(t2.frameCount == 0);
  }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("T-PY9: determinism — two identical calls => bit-identical tracks") {
  const std::vector<double> x = whiteNoise(0xD15EA5E, 48000);
  const PitchTrack a = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  const PitchTrack b = trackPitch(x, 48000.0, kTrackerFminHz, kTrackerFmaxHz);
  REQUIRE(a.frameCount == b.frameCount);
  REQUIRE(a.frames.size() == b.frames.size());
  // Field-wise bit-identity (memcmp over the struct would compare
  // value-uninitialised PADDING bytes — the fields are the contract).
  for (std::size_t i = 0; i < a.frames.size(); ++i) {
    CHECK(a.frames[i].start == b.frames[i].start);
    CHECK(a.frames[i].center == b.frames[i].center);
    CHECK(a.frames[i].voiced == b.frames[i].voiced);
    // Bit-identical doubles (== catches -0/NaN class differences too).
    CHECK(a.frames[i].f0Hz == b.frames[i].f0Hz);
    CHECK(a.frames[i].voicedProbability == b.frames[i].voicedProbability);
  }
  CHECK(a.windowFrames == b.windowFrames);
  CHECK(a.hop == b.hop);
  CHECK(a.droppedTailFrames == b.droppedTailFrames);
  CHECK(a.fminHz == b.fminHz);
  CHECK(a.fmaxHz == b.fmaxHz);
}

// ---------------------------------------------------------------------------
// Sample rates (the per-rate window rule)
// ---------------------------------------------------------------------------

TEST_CASE("T-PY10: sample rates — 500 Hz tracked at 44.1/48/96/192 kHz "
          "(per-rate windows 2048/2048/4096/8192)") {
  const uint32_t rates[] = {44100, 48000, 96000, 192000};
  for (const uint32_t fs : rates) {
    const std::vector<double> x = constantSine(500.0, fs, static_cast<int64_t>(fs));
    const PitchTrack t = trackPitch(x, static_cast<double>(fs), kTrackerFminHz,
                                    kTrackerFmaxHz);
    const int64_t expectedWindow = fs <= 48000 ? 2048 : (fs <= 96000 ? 4096 : 8192);
    CHECK(t.windowFrames == expectedWindow);
    CHECK(t.hop == expectedWindow / 4);
    int64_t voiced = 0;
    int64_t total = 0;
    double worst = 0.0;
    for (int64_t f = 1; f < t.frameCount; ++f) {
      ++total;
      if (!t.frames[static_cast<std::size_t>(f)].voiced) continue;
      ++voiced;
      worst = std::max(worst, std::fabs(centsBetween(
                                  t.frames[static_cast<std::size_t>(f)].f0Hz, 500.0)));
    }
    std::printf("T-PY10: fs %u: window %lld, voiced %lld/%lld, worst |cents| = %.4f\n",
                fs, static_cast<long long>(t.windowFrames),
                static_cast<long long>(voiced), static_cast<long long>(total), worst);
    CHECK(voiced >= total - 5);
    CHECK(worst <= 15.0);  // the cross-rate sub-bin class
  }
}
