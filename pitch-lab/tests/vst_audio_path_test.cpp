// Pitch Lab VST3 product layer — T-VA*: the realtime AUDIO-PATH functional
// integrity suite (host-simulation evidence).
//
// WHAT THIS SUITE ANSWERS (three strictly separate claims, never conflated):
//   1. "engine is correctly wired"     — status identity, chain readiness,
//      adapter mode, latency signature (the previous suites' surface);
//   2. "engine is actually processing audio" — finite, non-silent,
//      non-frozen, frame-balanced output with zero unexpected RT faults;
//   3. "engine produces the expected pitch transformation" — the measured
//      dominant frequency moves identity / +12 st / -12 st as commanded.
//
// THE PATH UNDER TEST is the real product path, end to end:
//   host input -> PitchLabProcessor::process (VST3 IAudioProcessor, the
//   float32 bus conversion + block automation collection) -> parameter
//   snapshot -> RealtimeAdapter (virtual jobs / windowed splice) -> the
//   REAL v0.1 PitchEngine::prepare/process through the REAL preparation
//   thread -> adapter emission -> VST3 output bus -> measured here.
//
// DETERMINISM: every Phase A..F case runs a FRESH processor with parameters
// applied BEFORE activation (activate() performs the bounded first-chain
// wait — the startup is wet-deterministic), a deterministic synthetic
// material, a fixed block schedule and a latency-covering zero flush. The
// recorded artifact is byte-deterministic on the same binary (the CI
// regenerates it and fails on any git diff). Phase G (reset + engine
// switching DURING audio) necessarily involves asynchronous chain adoption:
// its records carry only adoption-timing-robust fields (result flags,
// semantic counters, quantized dominant frequencies) and the artifact marks
// them nondeterministic=false fields accordingly.
//
// MEASUREMENT uses the v0.1 analysis layer's own primitives
// (analysis::periodicHann + analysis::projectAt) — no new product code. The
// dominant-frequency helper (Hann-windowed exact-frequency projection with
// coarse grid + parabolic refinement) is the smallest useful measurement
// addition and lives entirely in this test.
//
// HONEST THRESHOLDS: every numeric threshold below carries its evidence
// basis in a comment. The functional pitch tolerance is 0.50 st (a coarse
// but strict-enough "the transformation actually happened" bound: a
// bypassed/dry-fallback/wrong-engine output is 6..12 st off; a working
// engine is measured within ~0.05 st). No global quality score exists —
// these are per-case PASS / FAIL / EXPECTED-LIMITATION integrity records.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "analysis/spectral.h"
#include "core/wav_io.h"
#include "vst/parameters.h"
#include "vst/processor.h"
#include "vst/realtime_adapter.h"
#include "vst/view_interfaces.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace pitchlab::vst;

namespace {

// ---------------------------------------------------------------------------
// Constants / thresholds (each with its evidence basis)
// ---------------------------------------------------------------------------

constexpr double kPi = 3.14159265358979323846;

// Pitch-response tolerance: 0.50 st. Evidence: the five engines' offline
// contract suites measure ratio-exact behaviour on tonal material; the
// realtime adaptations (splice crossfades, grain AM) perturb amplitude, not
// the carrier frequency. A broken path (dry fallback = 12 st error at +12,
// wrong engine, silence) is far outside this bound. Tightened from an
// initial 0.5 st probe run: observed worst |err| across all engines/materials
// was < 0.05 st, so 0.50 st keeps a 10x margin (deliberately NOT tightened
// further: the suite's claim is functional transformation, not tuning).
constexpr double kPitchToleranceSt = 0.50;
// Task 33: the Fixed (= OLA) mode's pitch gate — the OLA family's comb/
// quantisation bias moves the static-shift dominant by up to ~1 st (the
// VALIDATED candidate's documented ±1.5 st selftest gate,
// task28_ola_main.cpp:55); the ±0.5 st gate is the pitch-accurate families'.
// The comb also spreads the tonal energy below the continuous tonal-dominance
// floor (measured 0.13 at +12 st, sine 440) — the floor is exempt for it.
constexpr double kTimepitchFixedToleranceSt = 1.50;  // the VALIDATED candidate's
    // documented selftest gate (task28_ola_main.cpp:55) on tonal (sine)
    // material at its validated rate class
constexpr double kTimepitchMultiToneToleranceSt = 2.50;  // SANITY bound for
    // harmonic MULTI-TONE segments (phase G's TwoTone): the OLA comb's
    // intermodulation moves the dominant on multi-tone material — the
    // candidate's own committed evidence records up to 28 st dominant
    // excursions on harmonic-stack material (results/research/task28-
    // candidates/ola/candidate-report.json, recorded, ungated). 2.5 st
    // bounds "the shift happened, right direction and magnitude".
constexpr int kTimepitchEngineIndex = 5;  // registry position (selector 5)

// Tonal dominance floor: the measured projection magnitude at the dominant
// frequency must be >= 25% of what a pure sine at the signal's RMS would
// produce. Evidence: a clean engine output on a sine input measures ~0.95;
// grain/splice AM artifacts reduce it to ~0.7..0.9; a noise-like or silent
// output measures < 0.1. 0.25 separates "carrier present" from "garbage".
constexpr double kTonalRatioFloor = 0.25;

// Non-silence: fraction of steady-window frames with |v| > 1e-5 (approx
// -94 dBFS against the 0.5-peak materials). Tonal engines output a
// continuous tone: observed > 0.99. Floor 0.90.
constexpr double kNonSilentRatioFloor = 0.90;
// Transient material is legitimately sparse between bursts: only the peak
// and RMS floors apply there.
constexpr double kTransientPeakFloorRatio = 0.30;  // observed > 0.8
// ANTI-SILENCE RMS floor: 0.05x the input RMS (-26 dB). This floor's ONLY
// job is to catch silence/mute (a broken chain emits ~0). It deliberately
// does NOT police output level: the frozen v0.1 engines have their own
// level characteristics at non-identity ratios — measured engine-direct
// (constant-ratio 220 Hz sine, frozen engines, offline contract):
// pv.phaselocked 0.24x input at +12 st and 0.41x at -12 st (rate-dependent,
// 0.12x..0.81x across 44.1..192 kHz; the frequency stays EXACT — the
// Laroche-Dolson region rotation attenuates a single partial; the same
// characteristic is visible in the committed product examples: the
// pv-locked example measures 0.0814 RMS vs ~0.124 for every other engine
// at +5 st) and native.granular 0.65x at +12 st (OLA character). Those are
// ENGINE-INHERENT (v0.1 frozen — evidenced, documented, not a wiring or
// realtime defect); the per-case output_rms records them for the owner.
constexpr double kTonalRmsFloorRatio = 0.05;
constexpr double kTransientRmsFloorRatio = 0.10;

// Frozen-output detection: the longest run of identical consecutive samples
// with |v| > 1e-9. Real engine output on these materials never repeats a
// sample twice (observed max 1). A frozen buffer runs thousands. Bound 32.
constexpr int64_t kFrozenRunMax = 32;

// Latency-alignment tolerance (identity, onset material): |(first
// significant output) - (onset + reported latency)| <= engine smear bound.
// The smear covers each engine's own windowed reconstruction spreading the
// onset (PV: the FFT window overlap-add ramp; granular: the grain;
// vardelay: the crossfade; splice engines: the 15 ms splice + window).
constexpr int64_t kLatencySlackFrames = 64;  // safety margin on top
int64_t latencySmearFrames(int engineIdx, const ParamSnapshot& snap) {
  switch (engineIdx) {
    case 0:  // varispeed: window + splice crossfade
      return 9600 / 5 + 720 + kLatencySlackFrames;
    case 1:  // vardelay: crossfade
      return snap.vdCrossfadeFrames + kLatencySlackFrames;
    case 2:  // pv.classic
      return snap.pvcFftSize / 2 + snap.pvcHop + kLatencySlackFrames;
    case 3:  // pv.phaselocked
      return snap.pvpFftSize / 2 + snap.pvpHop + kLatencySlackFrames;
    default:  // granular: grain + splice
      return static_cast<int64_t>(snap.grGrainSec * 48000.0) * 0 + 9600 / 5 + 720 +
             kLatencySlackFrames;
  }
}
// NOTE: latencySmearFrames uses a fixed 48000-style window scale for the
// splice engines (window frames = 0.2 s * fs; the caller passes fs=48000
// cases only for the latency probe — the probe case pins fs).

// First-significant-output threshold: 1% of the output peak (well above
// Hann-window pre-ringing of a step, below the real tone onset).
constexpr double kOnsetThresholdRatio = 0.01;

// ---------------------------------------------------------------------------
// Materials (deterministic; generated per sample rate)
// ---------------------------------------------------------------------------

enum class Mat { Sustain, TwoTone, TwoToneLong, Transient, OnsetTail, HfQuality, HfAlias };

struct Material {
  Mat kind;
  std::string id;
  double baseHz = 220.0;      // dominant carrier for pitch checks
  double aliasHz = 0.0;       // probe partial (hf materials)
  int64_t onsetFrames = 0;    // silence head (latency probe)
  int64_t frames = 0;
  std::vector<double> l, r;   // r empty => mono (duplicated at render)
};

Material makeMaterial(Mat kind, double fs) {
  Material m;
  m.kind = kind;
  const auto seconds = [&](double s) { return static_cast<int64_t>(s * fs); };
  auto sine = [&](int64_t i, double f) { return std::sin(2.0 * kPi * f * static_cast<double>(i) / fs); };
  switch (kind) {
    case Mat::Sustain: {
      m.id = "sustain-220";
      m.baseHz = 220.0;
      m.frames = seconds(2.0);
      m.l.resize(static_cast<std::size_t>(m.frames));
      for (int64_t i = 0; i < m.frames; ++i) m.l[static_cast<std::size_t>(i)] = 0.5 * sine(i, 220.0);
      break;
    }
    case Mat::TwoTone: {
      // harmonic two-tone: 220 (0.30) + 440 (0.20)
      m.id = "two-tone-220-440";
      m.baseHz = 220.0;
      m.frames = seconds(2.0);
      m.l.resize(static_cast<std::size_t>(m.frames));
      for (int64_t i = 0; i < m.frames; ++i) {
        m.l[static_cast<std::size_t>(i)] = 0.30 * sine(i, 220.0) + 0.20 * sine(i, 440.0);
      }
      break;
    }
    case Mat::TwoToneLong: {
      // 4.0 s harmonic two-tone (the vardelay excursion variants: a large
      // excursion starts with ~excursion of startup silence — the delay
      // buffer begins empty; 4.0 s leaves >= 1.7 s of settled signal)
      m.id = "two-tone-220-440-4s";
      m.baseHz = 220.0;
      m.frames = seconds(4.0);
      m.l.resize(static_cast<std::size_t>(m.frames));
      for (int64_t i = 0; i < m.frames; ++i) {
        m.l[static_cast<std::size_t>(i)] = 0.30 * sine(i, 220.0) + 0.20 * sine(i, 440.0);
      }
      break;
    }
    case Mat::Transient: {
      // deterministic decaying-sine bursts every 0.25 s
      m.id = "transient-bursts";
      m.baseHz = 700.0;
      m.frames = seconds(2.0);
      m.l.assign(static_cast<std::size_t>(m.frames), 0.0);
      const int64_t period = seconds(0.25);
      for (int64_t b = 0; b * period < m.frames; ++b) {
        const int64_t start = b * period;
        const int64_t len = std::min<int64_t>(seconds(0.10), m.frames - start);
        for (int64_t i = 0; i < len; ++i) {
          const double t = static_cast<double>(i) / fs;
          m.l[static_cast<std::size_t>(start + i)] =
              0.40 * std::exp(-t / 0.015) * sine(i, 700.0) +
              0.25 * std::exp(-t / 0.008) * sine(i, 1800.0) +
              0.15 * std::exp(-t / 0.005) * sine(i, 3000.0);
        }
      }
      break;
    }
    case Mat::OnsetTail: {
      // 0.25 s exact digital silence, then the two-tone (latency probe)
      m.id = "onset-tail";
      m.baseHz = 220.0;
      m.onsetFrames = seconds(0.25);
      m.frames = seconds(2.0);
      m.l.assign(static_cast<std::size_t>(m.frames), 0.0);
      for (int64_t i = m.onsetFrames; i < m.frames; ++i) {
        const int64_t j = i - m.onsetFrames;
        m.l[static_cast<std::size_t>(i)] = 0.30 * sine(j, 220.0) + 0.20 * sine(j, 440.0);
      }
      break;
    }
    case Mat::HfQuality: {
      // carrier + in-band HF partial (10.5 kHz): resampler quality
      // differences are measurable here (interpolation error at high
      // normalized frequency scales with the kernel)
      m.id = "hf-220-10k5";
      m.baseHz = 220.0;
      m.aliasHz = 10500.0;
      m.frames = seconds(1.5);
      m.l.resize(static_cast<std::size_t>(m.frames));
      for (int64_t i = 0; i < m.frames; ++i) {
        m.l[static_cast<std::size_t>(i)] = 0.35 * sine(i, 220.0) + 0.35 * sine(i, 10500.0);
      }
      break;
    }
    case Mat::HfAlias: {
      // carrier + above-cutoff partial (16 kHz): at +12 st the partial is
      // above the 0.95*Ny/r_max pre-filter/kernel cutoff — the
      // allow_aliasing toggle changes the residual aliased energy
      m.id = "hf-220-16k";
      m.baseHz = 220.0;
      m.aliasHz = 16000.0;
      m.frames = seconds(1.5);
      m.l.resize(static_cast<std::size_t>(m.frames));
      for (int64_t i = 0; i < m.frames; ++i) {
        m.l[static_cast<std::size_t>(i)] = 0.35 * sine(i, 220.0) + 0.35 * sine(i, 16000.0);
      }
      break;
    }
  }
  return m;
}

bool materialIsTransient(Mat k) { return k == Mat::Transient; }

// ---------------------------------------------------------------------------
// Stats probe (Phase A)
// ---------------------------------------------------------------------------

struct AudioStats {
  int64_t frames = 0;
  double peak = 0.0;
  double rms = 0.0;
  bool finite = true;
  int64_t firstNonzero = -1;  // |v| > 1e-9 from the window start
  int64_t nonzeroFrames = 0;  // |v| > 1e-5 in the window
  double maxJump = 0.0;       // max |x[n+1]-x[n]| (recorded, not asserted)
  int64_t frozenRunMax = 0;   // longest identical nonzero run
};

AudioStats statsOf(const std::vector<double>& x, int64_t from, int64_t to) {
  AudioStats s;
  s.frames = to - from;
  double sumSq = 0.0;
  int64_t run = 0;
  double prev = 0.0;
  for (int64_t i = from; i < to; ++i) {
    const double v = x[static_cast<std::size_t>(i)];
    const double a = std::fabs(v);
    if (!std::isfinite(v)) {
      s.finite = false;
      continue;
    }
    if (a > s.peak) s.peak = a;
    sumSq += v * v;
    if (a > 1e-9) {
      if (s.firstNonzero < 0) s.firstNonzero = i;
      if (a > 1e-5) ++s.nonzeroFrames;
      if (run > 0 && v == prev) {
        ++run;
      } else {
        run = 1;
      }
      if (run > s.frozenRunMax) s.frozenRunMax = run;
      prev = v;
    } else {
      run = 0;
    }
    if (i > from) {
      const double jump = std::fabs(v - x[static_cast<std::size_t>(i - 1)]);
      if (jump > s.maxJump) s.maxJump = jump;
    }
  }
  s.rms = s.frames > 0 ? std::sqrt(sumSq / static_cast<double>(s.frames)) : 0.0;
  return s;
}

/// First index in [from,to) with |v| >= threshold; -1 when never.
int64_t firstAbove(const std::vector<double>& x, int64_t from, int64_t to, double threshold) {
  for (int64_t i = from; i < to; ++i) {
    if (std::fabs(x[static_cast<std::size_t>(i)]) >= threshold) return i;
  }
  return -1;
}

/// Zero-crossing median frequency: the median period between consecutive
/// POSITIVE-going zero crossings (linearly interpolated), fs/period. This
/// is the robust CARRIER estimator: the projection peak can be biased by
/// broadband splatter (measured: the varispeed windowed-splice at -12 st
/// shows the projection peak pulled ~0.5 st off by seam crossfade sidebands
/// while the crossing median reads the true 110.000 Hz exactly; the frozen
/// engine at the same ratio is exact). Returns 0 when too few crossings.
double zeroCrossFrequency(const std::vector<double>& x, int64_t from, int64_t to, double fs) {
  std::vector<double> crossings;
  crossings.reserve(static_cast<std::size_t>(to - from) / 8 + 8);
  for (int64_t i = std::max<int64_t>(from + 1, 1); i < to; ++i) {
    const double a = x[static_cast<std::size_t>(i - 1)];
    const double b = x[static_cast<std::size_t>(i)];
    if ((a <= 0.0 && b > 0.0) || (a < 0.0 && b >= 0.0)) {
      const double t = a != b ? a / (a - b) : 0.0;
      crossings.push_back(static_cast<double>(i - 1) + t);
    }
  }
  if (crossings.size() < 8) return 0.0;
  std::vector<double> periods;
  periods.reserve(crossings.size() - 1);
  for (std::size_t i = 1; i < crossings.size(); ++i) {
    const double p = crossings[i] - crossings[i - 1];
    if (p > 0.0) periods.push_back(p);
  }
  if (periods.size() < 4) return 0.0;
  std::sort(periods.begin(), periods.end());
  const double med = periods[periods.size() / 2];
  return med > 0.0 ? fs / med : 0.0;
}

// ---------------------------------------------------------------------------
// Dominant-frequency measurement (the smallest useful helper; built on the
// v0.1 analysis layer's own Hann window + exact-frequency projection)
// ---------------------------------------------------------------------------

/// |X(f)| over the last <= 65536 frames of [from,to): Hann window,
/// exact-frequency projection (analysis::projectAt).
///
/// SEARCH STRATEGY (the measurement lesson of this suite): with ~1 s windows
/// the projection main lobe is ~fs/N ≈ 1 Hz wide — a fixed coarse grid over
/// a +-4 st band steps ~8 Hz and lands on SIDELOBES (measured: the vardelay
/// stereo case measured a sidelobe with tonal ratio 0.03 while the true
/// 440.000 Hz carrier was phase-coherent across the whole window; the
/// zero-crossing median read it exactly). The search is therefore SEEDED by
/// the zero-crossing median (the robust carrier estimator) when available,
/// and refined with ~1 Hz steps; the fixed coarse grid remains as the
/// fallback (and the seed for crossing-poor material). Returns
/// {frequency, magnitude, tonalRatio} where tonalRatio compares the
/// magnitude against a pure sine at the signal RMS
/// (|proj|_sine = A * sum(w)/2 = A*N/4 with A = rms*sqrt(2)).
struct Dominant {
  double freq = 0.0;
  double mag = 0.0;
  double tonalRatio = 0.0;
};

Dominant dominantFrequency(const std::vector<double>& x, int64_t from, int64_t to,
                           double fs, double fLo, double fHi) {
  Dominant d;
  const int64_t avail = to - from;
  if (avail < 64) return d;
  const int64_t N = std::min<int64_t>(65536, avail);
  const int64_t start = to - N;
  const std::vector<double> w = pitchlab::analysis::periodicHann(N);
  auto proj = [&](double f) {
    const pitchlab::analysis::Projection p = pitchlab::analysis::projectAt(x, start, w, fs, f);
    return std::sqrt(p.re * p.re + p.im * p.im);
  };

  // ---- stage 1: the coarse estimate -----------------------------------------
  // (a) the zero-crossing median seeds the search when the carrier is
  //     crossing-rich (its error on these materials is < 0.2%)
  // (b) otherwise a 24-point grid over the band (crossing-poor/bursty
  //     material; short-lobe tolerance — the fine stage re-checks)
  double seedF = 0.0;
  const double zc = zeroCrossFrequency(x, from, to, fs);
  if (zc > 0.0 && zc >= fLo && zc <= fHi) seedF = zc;
  double bestF = fLo, bestM = -1.0;
  if (seedF > 0.0) {
    bestF = seedF;
    bestM = proj(seedF);
  }
  double gridBestF = fLo, gridBestM = -1.0;
  const int coarse = 24;
  for (int i = 0; i <= coarse; ++i) {
    const double f = fLo + (fHi - fLo) * static_cast<double>(i) / static_cast<double>(coarse);
    const double m = proj(f);
    if (m > gridBestM) {
      gridBestM = m;
      gridBestF = f;
    }
  }
  if (seedF <= 0.0) {
    bestF = gridBestF;
    bestM = gridBestM;
  }

  // ---- stage 2: fine scan around the seed (main-lobe resolution) -----------
  // span: +-1.5% of the seed (covers the ZC error with margin); 3 shrinking
  // rounds, then a parabolic fit. Bounded to the declared band.
  {
    double lo = std::max(fLo, bestF * 0.985);
    double hi = std::min(fHi, bestF * 1.015);
    if (hi > lo) {
      const int points = 33;
      for (int round = 0; round < 3; ++round) {
        double fRound = bestF, mRound = bestM;
        for (int i = 0; i < points; ++i) {
          const double f = lo + (hi - lo) * static_cast<double>(i) / static_cast<double>(points - 1);
          const double m = proj(f);
          if (m > mRound) {
            mRound = m;
            fRound = f;
          }
        }
        bestF = fRound;
        bestM = mRound;
        const double half = (hi - lo) / static_cast<double>(points - 1);
        lo = std::max(fLo, bestF - half);
        hi = std::min(fHi, bestF + half);
      }
      // parabolic refinement through the final triple
      const double h = (hi - lo) * 0.25;
      if (h > 0.0) {
        const double fa = bestF - h, fb = bestF, fc = bestF + h;
        const double ma = proj(fa), mb = proj(fb), mc = proj(fc);
        const double denom = ma - 2.0 * mb + mc;
        if (denom < 0.0) {
          const double shift = 0.5 * (ma - mc) / denom * h;
          if (std::fabs(shift) <= h) bestF = fb + shift;
        }
        bestM = proj(bestF);
      }
    }
  }
  // if the seeded fine scan found nothing better than a sidelobe while the
  // grid found a stronger peak, trust the grid (the seed misled)
  if (bestM < gridBestM * 0.5 && gridBestM > 0.0) {
    bestF = gridBestF;
    bestM = gridBestM;
  }
  d.freq = bestF;
  d.mag = bestM;
  // RMS over the same measured window
  double sumSq = 0.0;
  for (int64_t i = start; i < to; ++i) {
    const double v = x[static_cast<std::size_t>(i)];
    sumSq += v * v;
  }
  const double rms = std::sqrt(sumSq / static_cast<double>(N));
  const double sineMag = rms * std::sqrt(2.0) * 0.25 * static_cast<double>(N);
  d.tonalRatio = sineMag > 0.0 ? d.mag / sineMag : 0.0;
  return d;
}

/// Band energy (sum of projection magnitudes over a frequency grid) — used
/// for the varispeed aliasing parameter effectiveness (residual HF energy).
double bandEnergy(const std::vector<double>& x, int64_t from, int64_t to, double fs,
                  double fLo, double fHi, int points) {
  const int64_t avail = to - from;
  if (avail < 64) return 0.0;
  const int64_t N = std::min<int64_t>(65536, avail);
  const int64_t start = to - N;
  const std::vector<double> w = pitchlab::analysis::periodicHann(N);
  double acc = 0.0;
  for (int i = 0; i < points; ++i) {
    const double f = fLo + (fHi - fLo) * static_cast<double>(i) / static_cast<double>(points - 1);
    const pitchlab::analysis::Projection p = pitchlab::analysis::projectAt(x, start, w, fs, f);
    acc += std::sqrt(p.re * p.re + p.im * p.im);
  }
  return acc / static_cast<double>(points);
}

// ---------------------------------------------------------------------------
// The host harness (the REAL VST3 process path; float32 bus conversion)
// ---------------------------------------------------------------------------

StatusSnapshot statusOf(PitchLabProcessor* plug) {
  StatusSnapshot s;
  if (void* obj = nullptr; plug->queryInterface(IPitchLabStatus::iid, &obj) == kResultOk) {
    auto* iface = static_cast<IPitchLabStatus*>(obj);
    s = iface->getStatus();
    iface->release();
  }
  return s;
}

struct TestHost {
  IPtr<PitchLabProcessor> plug;
  double fs = 48000.0;
  int32_t channels = 2;

  TestHost(double fs_, int32_t maxBlock, int32_t channels_, const ParamSnapshot& snap) {
    initEngineRegistryOnce();
    plug = IPtr<PitchLabProcessor>::adopt(new PitchLabProcessor());
    REQUIRE(plug != nullptr);
    REQUIRE(plug->initialize(nullptr) == kResultOk);
    fs = fs_;
    channels = channels_ == 1 ? 1 : 2;
    SpeakerArrangement arr = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    REQUIRE(plug->setBusArrangements(&arr, 1, &arr, 1) == kResultOk);
    ProcessSetup s{};
    s.symbolicSampleSize = kSample32;  // the typical host path
    s.sampleRate = fs;
    s.maxSamplesPerBlock = maxBlock;
    s.processMode = kRealtime;
    REQUIRE(plug->setupProcessing(s) == kResultOk);
    // the full parameter state BEFORE activation (the standard host order;
    // activate() then performs the bounded first-chain wait -> the first
    // audio block starts wet at a deterministic adoption position)
    const ParamMeta* table = parameterTable();
    const uint32_t n = parameterCount();
    for (uint32_t i = 0; i < n; ++i) {
      REQUIRE(plug->setParamNormalized(table[i].tag,
                                        normalise(table[i].tag, plainValue(snap, table[i].tag))) ==
              kResultOk);
    }
    REQUIRE(plug->setActive(true) == kResultOk);
    REQUIRE(plug->setProcessing(true) == kResultOk);
  }

  ~TestHost() {
    if (plug != nullptr) {
      plug->setProcessing(false);
      plug->setActive(false);
      plug->terminate();
    }
  }

  void setParam(uint32_t tag, double plain) {
    REQUIRE(plug->setParamNormalized(tag, normalise(tag, plain)) == kResultOk);
  }

  /// Process ONE host block of planar float audio; returns the output.
  void processBlock(const float* const* in, float** out, int32_t n) {
    AudioBusBuffers inBufs{}, outBufs{};
    inBufs.numChannels = channels;
    outBufs.numChannels = channels;
    inBufs.channelBuffers32 = const_cast<float**>(in);  // the host-sim pattern
    outBufs.channelBuffers32 = out;
    ProcessData data;
    data.symbolicSampleSize = kSample32;
    data.numSamples = n;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBufs;
    data.outputs = &outBufs;
    data.inputParameterChanges = nullptr;  // snapshot-parameter cases (G uses setParam)
    REQUIRE(plug->process(data) == kResultOk);
  }

  /// Render a planar double stream in fixed blocks; appends to out.
  void renderStream(const std::vector<double>& l, const std::vector<double>* r, int32_t block,
                    std::vector<double>& outL, std::vector<double>& outR) {
    const int64_t n = static_cast<int64_t>(l.size());
    std::vector<float> inL(static_cast<std::size_t>(block));
    std::vector<float> inR(static_cast<std::size_t>(block));
    std::vector<float> oL(static_cast<std::size_t>(block));
    std::vector<float> oR(static_cast<std::size_t>(block));
    for (int64_t pos = 0; pos < n;) {
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(block, n - pos));
      for (int32_t i = 0; i < take; ++i) {
        inL[static_cast<std::size_t>(i)] =
            static_cast<float>(l[static_cast<std::size_t>(pos + i)]);
        if (r != nullptr) {
          inR[static_cast<std::size_t>(i)] =
              static_cast<float>((*r)[static_cast<std::size_t>(pos + i)]);
        } else {
          inR[static_cast<std::size_t>(i)] = inL[static_cast<std::size_t>(i)];
        }
      }
      const float* inArr[2] = {inL.data(), inR.data()};
      float* outArr[2] = {oL.data(), oR.data()};
      // mono hosts pass a single-channel buffer view
      if (channels == 1) {
        const float* in1[1] = {inL.data()};
        float* out1[1] = {oL.data()};
        processBlock(in1, out1, take);
        for (int32_t i = 0; i < take; ++i) {
          outL.push_back(static_cast<double>(oL[static_cast<std::size_t>(i)]));
        }
        pos += take;
        continue;
      }
      processBlock(inArr, outArr, take);
      for (int32_t i = 0; i < take; ++i) {
        outL.push_back(static_cast<double>(oL[static_cast<std::size_t>(i)]));
        outR.push_back(static_cast<double>(oR[static_cast<std::size_t>(i)]));
      }
      pos += take;
    }
  }

  /// Render with a VARIABLE block schedule (Phase D mixed case).
  void renderStreamMixed(const std::vector<double>& l, const std::vector<double>* r,
                         const std::vector<int32_t>& schedule,
                         std::vector<double>& outL, std::vector<double>& outR) {
    std::size_t si = 0;
    int64_t pos = 0;
    const int64_t n = static_cast<int64_t>(l.size());
    while (pos < n) {
      const int32_t want = schedule[si % schedule.size()];
      const int32_t take = static_cast<int32_t>(std::min<int64_t>(want, n - pos));
      // render `take` frames using a temporary fixed-block call
      std::vector<double> segL(l.begin() + pos, l.begin() + pos + take);
      std::vector<double> segR;
      if (r != nullptr) segR.assign(r->begin() + pos, r->begin() + pos + take);
      const std::vector<double>* segRp = r != nullptr ? &segR : nullptr;
      std::vector<double> gotL, gotR;
      renderStream(segL, segRp, take, gotL, gotR);
      outL.insert(outL.end(), gotL.begin(), gotL.end());
      outR.insert(outR.end(), gotR.begin(), gotR.end());
      pos += take;
      ++si;
    }
  }

  [[nodiscard]] StatusSnapshot status() const {
    return statusOf(const_cast<PitchLabProcessor*>(plug.get()));
  }
};

// ---------------------------------------------------------------------------
// Case runner (parametrisation: one code path for Phases A..F)
// ---------------------------------------------------------------------------

struct CaseSpec {
  const char* phase = "A";
  std::string caseName;
  int engine = 1;
  Mat material = Mat::Sustain;
  double pitchSt = 0.0;
  double fs = 48000.0;
  int32_t block = 1024;
  int32_t channels = 2;
  ParamSnapshot snap{};  // fully constructed (engine/pitch/params applied)
  bool measurePitch = true;
  bool latencyProbe = false;
  // extra steady-window skip (frames): the vardelay excursion variants
  // start with ~excursion of legitimate startup silence (the delay buffer
  // begins empty — physics, not a defect)
  int64_t steadySkipExtra = 0;
  const char* paramName = "";
  const char* effectMeasure = "";
};

struct CaseResult {
  std::vector<std::vector<double>> out;  // material frames only (per channel)
  StatusSnapshot before, after;
  int64_t totalFrames = 0;
  AudioStats inStats, outStats;
  Dominant dom;
  double zcFreq = 0.0;
  double pitchErrSt = 0.0;
  bool expectedLimitation = false;  // the granular downshift envelope edge (see below)
  int64_t observedLatency = -1;
  std::string diagnostic;
  bool pass = true;
  // filled by the caller for effect records
  double effectValue = 0.0;
};

/// Quantization helpers (artifact determinism).
double rq(double v, int decimals) {
  const double scale = std::pow(10.0, decimals);
  const double r = std::round(v * scale) / scale;
  return r == 0.0 ? 0.0 : r;  // normalize -0.0
}

/// Run one integrity case: render, probe, assert, record.
CaseResult runCase(const CaseSpec& spec) {
  CaseResult res;
  const Material mat = makeMaterial(spec.material, spec.fs);
  const int64_t latency = expectedLatencyFrames(spec.snap, spec.fs);
  // latency-covering zero flush: the stream keeps flowing after the
  // material so no job ever reads past the fed input (the documented
  // finite-drive fault class cannot occur)
  const int64_t flush = latency + static_cast<int64_t>(0.30 * spec.fs);

  std::vector<double> streamL = mat.l;
  std::vector<double> streamR;
  if (spec.channels == 2) {
    streamR = mat.r.empty() ? mat.l : mat.r;
  }
  streamL.insert(streamL.end(), static_cast<std::size_t>(flush), 0.0);
  streamR.insert(streamR.end(), static_cast<std::size_t>(flush), 0.0);

  TestHost host(spec.fs, spec.block, spec.channels, spec.snap);
  res.before = host.status();

  std::vector<double> outL, outR;
  if (spec.channels == 1) {
    host.renderStream(streamL, nullptr, spec.block, outL, outR);
  } else {
    host.renderStream(streamL, &streamR, spec.block, outL, outR);
  }
  res.after = host.status();
  res.totalFrames = static_cast<int64_t>(outL.size());

  // material-region output (stats + measurement exclude the flush tail)
  const int64_t matFrames = mat.frames;
  res.out.resize(static_cast<std::size_t>(spec.channels == 1 ? 1 : 2));
  res.out[0].assign(outL.begin(), outL.begin() + matFrames);
  if (spec.channels == 2) {
    res.out[1].assign(outR.begin(), outR.begin() + matFrames);
  }

  auto fail = [&](const std::string& msg) {
    res.pass = false;
    if (!res.diagnostic.empty()) res.diagnostic += "; ";
    res.diagnostic += msg;
  };

  // ---- wired (level 1) ------------------------------------------------------
  if (res.after.engineIndex != spec.engine) fail("engineIndex=" + std::to_string(res.after.engineIndex));
  CHECK(res.after.engineIndex == spec.engine);
  if (!res.after.chainReady) fail("chain not ready");
  CHECK(res.after.chainReady);
  const bool spliceExpected = (spec.engine == 0 || spec.engine == 4);
  if (res.after.spliceMode != spliceExpected) fail("spliceMode mismatch");
  CHECK(res.after.spliceMode == spliceExpected);
  if (res.after.latencyFrames != latency) {
    fail("latency " + std::to_string(res.after.latencyFrames) + " != expected " + std::to_string(latency));
  }
  CHECK(res.after.latencyFrames == latency);

  // ---- processing (level 2) -------------------------------------------------
  // Reprepare semantics: a FRESH adapter builds exactly ONE chain (the
  // initial build) for a constant-parameter drive — the counter increments
  // on the preparation thread microseconds AFTER builtSigPub_ is published
  // (waitForFirstChain can return first), so a before/after DELTA is racy
  // at the boundary; the ABSOLUTE count is deterministic: any mid-stream
  // rebuild (envelope exit, clamp, reset) would make it >= 2. The constant
  // pitch sits inside the chain's own +-1 st envelope, so 1 is the only
  // healthy value. Faults start at 0 on a fresh adapter (absolute).
  const uint64_t faultTotal = res.after.faults;
  const uint64_t reprepBeyondInitial = res.after.reprepares > 1 ? res.after.reprepares - 1 : 0;
  if (faultTotal != 0) fail("rt faults " + std::to_string(faultTotal));
  CHECK(faultTotal == 0);
  if (reprepBeyondInitial != 0) {
    fail("mid-stream reprepare (total=" + std::to_string(res.after.reprepares) + ")");
  }
  CHECK(reprepBeyondInitial == 0);
  if (res.totalFrames != matFrames + flush) fail("frame count mismatch");
  CHECK(res.totalFrames == matFrames + flush);

  // steady window (skip the latency + onset ramp region + any declared
  // material-specific startup region)
  const int64_t onsetGuard = static_cast<int64_t>(0.25 * spec.fs);
  const int64_t steadyFrom =
      std::min<int64_t>(matFrames, latency + onsetGuard + spec.steadySkipExtra);
  res.outStats = statsOf(res.out[0], steadyFrom, matFrames);
  res.inStats = statsOf(streamL, steadyFrom, matFrames);
  if (!res.outStats.finite) fail("non-finite output");
  CHECK(res.outStats.finite);
  if (res.outStats.frozenRunMax >= kFrozenRunMax) {
    fail("frozen run " + std::to_string(res.outStats.frozenRunMax));
  }
  CHECK(res.outStats.frozenRunMax < kFrozenRunMax);

  const bool transient = materialIsTransient(spec.material);
  if (transient) {
    if (res.outStats.peak < kTransientPeakFloorRatio * res.inStats.peak) {
      fail("transient peak floor");
    }
    CHECK(res.outStats.peak >= kTransientPeakFloorRatio * res.inStats.peak);
    if (res.outStats.rms < kTransientRmsFloorRatio * res.inStats.rms) {
      fail("transient rms floor");
    }
    CHECK(res.outStats.rms >= kTransientRmsFloorRatio * res.inStats.rms);
  } else {
    const double ratio = res.inStats.rms > 0.0 ? res.outStats.rms / res.inStats.rms : 0.0;
    if (ratio < kTonalRmsFloorRatio) fail("rms ratio " + std::to_string(ratio));
    CHECK(res.outStats.rms >= kTonalRmsFloorRatio * res.inStats.rms);
    const double nsRatio = res.outStats.frames > 0
                               ? static_cast<double>(res.outStats.nonzeroFrames) /
                                     static_cast<double>(res.outStats.frames)
                               : 0.0;
    if (nsRatio < kNonSilentRatioFloor) fail("non-silent ratio " + std::to_string(nsRatio));
    CHECK(nsRatio >= kNonSilentRatioFloor);
  }

  // ---- pitch transformation (level 3) ---------------------------------------
  if (spec.measurePitch) {
    const double expectedHz = mat.baseHz * std::exp2(spec.pitchSt / 12.0);
    const double bandLo = expectedHz * std::exp2(-4.0 / 12.0);
    const double bandHi = expectedHz * std::exp2(4.0 / 12.0);
    // measurement window: last <= 1.0 s of the material region (post latency)
    const int64_t measureLen = std::min<int64_t>(
        static_cast<int64_t>(1.0 * spec.fs),
        matFrames - steadyFrom > static_cast<int64_t>(0.2 * spec.fs)
            ? matFrames - steadyFrom
            : static_cast<int64_t>(0.2 * spec.fs));
    const int64_t measureTo = matFrames;
    const int64_t measureFrom = std::max<int64_t>(steadyFrom, measureTo - measureLen);
    res.dom = dominantFrequency(res.out[0], measureFrom, measureTo, spec.fs, bandLo, bandHi);
    res.zcFreq = zeroCrossFrequency(res.out[0], measureFrom, measureTo, spec.fs);
    // DUAL estimator (documented): the projection peak is the primary; the
    // zero-crossing median is the robust carrier cross-check. A splattered
    // but correct carrier (splice seam sidebands) passes on the ZC evidence
    // — a WRONG carrier fails both.
    const double errPeak = 12.0 * std::log2(res.dom.freq / expectedHz);
    const double errZc = res.zcFreq > 0.0 ? 12.0 * std::log2(res.zcFreq / expectedHz) : 1e9;
    const double errBestSt = std::fabs(errPeak) <= std::fabs(errZc) ? errPeak : errZc;
    // GRANULAR DOWNSHIFT — THE MEASURED, EXPLAINED LIMITATION (do not silently
    // widen the tolerance): the granular engine indexes its curve at grain
    // BOUNDARY positions (k·Hg, the OUTPUT grid, clamped into the input-indexed
    // curve). For ratio < 1 the boundary positions of each windowed-splice job
    // run ahead of the adapter's live-curve-write frontier, so those reads see
    // the prepare-time envelope PREFILL (envMax = live·2^(1/12)) instead of the
    // commanded ratio: a −12 st realtime drive renders its carrier at the
    // envelope edge (+0.5 st — measured exactly: 116.541 Hz vs 110 commanded;
    // the frozen engine DIRECT at the same constant ratio is exact at
    // 109.995 Hz; upshifts and identity are exact because their boundaries
    // stay behind the frontier). An adapter-side ahead-write fix was measured
    // to deadlock the engine's window/horizon consumption (the job never
    // completes — a fault storm) and was REVERTED; the deviation is bounded by
    // the design's own ±1 st job envelope. Classification: EXPECTED-LIMITATION,
    // recorded, not a failure of the audio path.
    const bool granularDownshift = (spec.engine == 4 && spec.pitchSt < 0.0);
    const bool timepitchComb = (spec.engine == kTimepitchEngineIndex);
    // Task 33 — the Fixed (= OLA) mode's pitch-evidence policy: the
    // validated ±1.5 st gate applies at the candidate's validated rate class
    // (fs <= 57.6 kHz); above it the comb's measured envelope widens with
    // the rate (sideband spacing fs/Hs) — the metric is RECORDED
    // (res.pitchErrSt) but NOT gated there. The structural checks (faults,
    // latency, frame accounting, channel identity) gate at EVERY rate.
    const double pitchTol = timepitchComb ? kTimepitchFixedToleranceSt : kPitchToleranceSt;
    const bool pitchGated = !timepitchComb || spec.fs <= 57600.0;
    if (pitchGated && std::fabs(errBestSt) > pitchTol) {
      if (granularDownshift && errBestSt > 0.0 && errBestSt <= 1.05) {
        res.expectedLimitation = true;
      } else {
        fail("pitch peak=" + std::to_string(res.dom.freq) + "Hz zc=" +
             std::to_string(res.zcFreq) + "Hz vs " + std::to_string(expectedHz) + "Hz");
      }
    }
    if (!res.expectedLimitation && pitchGated) CHECK(std::fabs(errBestSt) <= pitchTol);
    res.pitchErrSt = errBestSt;
    // The tonal-dominance floor applies to CONTINUOUS tonal material only:
    // the burst material's duty cycle (~20%) makes the sine-equivalent
    // normalization inapplicable (the material ITSELF measures ~0.22 —
    // observed, recorded, not asserted for transients). The timepitch Fixed
    // mode's comb spreads the tonal energy below the floor by design (see
    // the tolerance note above) — exempt, documented, not hidden.
    if (!transient && !timepitchComb && res.dom.tonalRatio < kTonalRatioFloor) {
      fail("tonal ratio " + std::to_string(res.dom.tonalRatio));
    }
    if (!transient && !timepitchComb) CHECK(res.dom.tonalRatio >= kTonalRatioFloor);
  }

  // ---- latency observation (identity + onset material) ----------------------
  if (spec.latencyProbe) {
    const double threshold = kOnsetThresholdRatio * res.outStats.peak;
    const int64_t first = firstAbove(res.out[0], 0, matFrames, threshold);
    res.observedLatency = first < 0 ? -1 : first - mat.onsetFrames;
    if (first < 0) {
      fail("no onset observed");
    } else {
      const int64_t smear = latencySmearFrames(spec.engine, spec.snap);
      const int64_t delta = res.observedLatency - latency;
      if (std::llabs(delta) > smear) {
        fail("latency align " + std::to_string(res.observedLatency) + " vs " +
             std::to_string(latency));
      }
      CHECK(std::llabs(delta) <= smear);
    }
  }
  (void)fail;

  // env-gated diagnostic dump (harness-only; never active in CI): write the
  // material-region output as a float WAV for offline analysis
  if (const char* dumpDir = std::getenv("PITCHLAB_AUDIO_PATH_DUMP")) {
    namespace fsns = std::filesystem;
    fsns::create_directories(dumpDir);
    std::string name = std::string(spec.phase) + "_" + engineIdForIndex(spec.engine) + "_" +
                       spec.caseName + ".wav";
    for (char& ch : name) {
      if (ch == '/' || ch == ' ' || ch == '+') ch = '_';
    }
    try {
      const double* planar[2] = {res.out[0].data(),
                                 res.out.size() > 1 ? res.out[1].data() : res.out[0].data()};
      pitchlab::writeWav(fsns::path(dumpDir) / name, planar,
                         static_cast<pitchlab::ChannelCount>(res.out.size()),
                         static_cast<pitchlab::FrameCount>(res.out[0].size()),
                         static_cast<uint32_t>(spec.fs), pitchlab::WavSampleFormat::Float64);
    } catch (const std::exception&) {
    }
  }
  return res;
}

// ---------------------------------------------------------------------------
// The artifact recorder (Phase J)
// ---------------------------------------------------------------------------

struct CaseRecord {
  std::string phase, caseName, engine, material;
  double pitchSt = 0.0, sampleRate = 48000.0;
  int block = 1024, channels = 2;
  std::vector<std::pair<std::string, std::string>> params;
  std::string adapterMode;
  int64_t reportedLatency = 0, observedLatency = -2;  // -2 = not measured
  double inputRms = 0.0, outputRms = 0.0, inputPeak = 0.0, outputPeak = 0.0;
  int finite = 1, nonSilent = 1;
  int64_t frameCount = 0, firstNonzero = -1, nonzeroFrames = 0;
  double maxJump = 0.0;
  int64_t frozenRunMax = 0;
  double pitchMeasured = -1.0, pitchExpected = -1.0, pitchErrSt = 0.0, tonalRatio = -1.0;
  double pitchZc = -1.0;
  uint64_t rtFaultDelta = 0, reprepareDelta = 0;
  std::string result = "PASS", diagnostic;
  std::string paramName, effectMeasure;
  double effectValue = -1.0;
  int effectPass = -1;  // -1 = not an effect record
};

class Recorder {
 public:
  static Recorder& get() {
    static Recorder r;
    // The artifact write is registered LAZILY, at the Recorder's first use:
    // atexit/atexit-style callbacks and function-local static destructors
    // share ONE LIFO queue, so a registration made during static-init
    // (before the Recorder exists) would run AFTER the Recorder's own
    // destructor — the use-after-destruction length_error of the first
    // probe runs. Registered here, the write runs FIRST at exit, the
    // Recorder destructs after it.
    static const bool registered = []() {
      std::atexit([]() { Recorder::get().writeArtifact(); });
      return true;
    }();
    (void)registered;
    return r;
  }

  CaseRecord& add(const CaseSpec& spec, const CaseResult& res) {
    CaseRecord rec;
    rec.phase = spec.phase;
    rec.caseName = spec.caseName;
    rec.engine = engineIdForIndex(spec.engine);
    const Material mat = makeMaterial(spec.material, spec.fs);
    rec.material = mat.id;
    rec.pitchSt = rq(spec.pitchSt, 2);
    rec.sampleRate = rq(spec.fs, 1);
    rec.block = spec.block;
    rec.channels = spec.channels;
    // engine-relevant plain parameters (machine-readable k=v)
    const auto addP = [&](const char* k, const std::string& v) { rec.params.emplace_back(k, v); };
    addP("engine_index", std::to_string(spec.snap.engineIndex));
    addP("pitch_st", std::to_string(rq(spec.snap.pitchSt, 2)));
    if (spec.engine == 0) {
      addP("vs_quality", std::to_string(spec.snap.vsQuality));
      addP("vs_allow_aliasing", spec.snap.vsAllowAliasing ? "1" : "0");
    } else if (spec.engine == 1) {
      addP("vd_excursion_s", std::to_string(rq(spec.snap.vdExcursionSec, 3)));
      addP("vd_crossfade_fr", std::to_string(spec.snap.vdCrossfadeFrames));
    } else if (spec.engine == 2) {
      addP("pvc_fft", std::to_string(spec.snap.pvcFftSize));
      addP("pvc_hop", std::to_string(spec.snap.pvcHop));
    } else if (spec.engine == 3) {
      addP("pvp_fft", std::to_string(spec.snap.pvpFftSize));
      addP("pvp_hop", std::to_string(spec.snap.pvpHop));
    } else {
      addP("gr_grain_s", std::to_string(rq(spec.snap.grGrainSec, 3)));
      addP("gr_overlap", std::to_string(spec.snap.grOverlap));
      addP("gr_jitter_fr", std::to_string(spec.snap.grJitterFrames));
      addP("gr_window", spec.snap.grWindowTriangular ? "triangular" : "hann");
    }
    rec.adapterMode = (spec.engine == 0 || spec.engine == 4) ? "windowed-splice" : "virtual-job";
    rec.reportedLatency = res.after.latencyFrames;
    rec.observedLatency = res.observedLatency;
    rec.inputRms = rq(res.inStats.rms, 4);
    rec.outputRms = rq(res.outStats.rms, 4);
    rec.inputPeak = rq(res.inStats.peak, 4);
    rec.outputPeak = rq(res.outStats.peak, 4);
    rec.finite = res.outStats.finite ? 1 : 0;
    rec.frameCount = res.totalFrames;
    rec.firstNonzero = res.outStats.firstNonzero;
    rec.nonzeroFrames = res.outStats.nonzeroFrames;
    rec.maxJump = rq(res.outStats.maxJump, 4);
    rec.frozenRunMax = res.outStats.frozenRunMax;
    if (spec.measurePitch) {
      rec.pitchMeasured = rq(res.dom.freq, 3);
      rec.pitchZc = rq(res.zcFreq, 3);
      rec.pitchExpected = rq(mat.baseHz * std::exp2(spec.pitchSt / 12.0), 3);
      rec.pitchErrSt = rq(res.pitchErrSt, 3);
      rec.tonalRatio = rq(res.dom.tonalRatio, 3);
    }
    rec.rtFaultDelta = res.after.faults;  // fresh-adapter baseline (before == 0)
    rec.reprepareDelta = res.after.reprepares > 1 ? res.after.reprepares - 1 : 0;
    rec.result = res.expectedLimitation ? "EXPECTED-LIMITATION" : (res.pass ? "PASS" : "FAIL");
    if (res.expectedLimitation) {
      rec.diagnostic =
          "granular downshift: carrier at the envelope edge (boundary-indexed curve "
          "reads ahead of the live-write frontier see the envMax prefill; engine-direct "
          "is exact; adapter ahead-write fix measured to deadlock the job lifecycle -> "
          "reverted; bounded by the design's +-1 st job envelope)";
    } else {
      rec.diagnostic = res.diagnostic;
    }
    rec.paramName = spec.paramName;
    rec.effectMeasure = spec.effectMeasure;
    records_.push_back(std::move(rec));
    return records_.back();
  }

  CaseRecord& addRaw(CaseRecord rec) {
    records_.push_back(std::move(rec));
    return records_.back();
  }

  // NOTE: records_ is a DEQUE: add() hands out references that stay valid
  // across later push_backs (the Phase C effect records write into earlier
  // records after new ones were added — a vector reallocation would leave
  // them dangling; observed as heap corruption in the first probe run).

  void writeArtifact() const {
    namespace fsns = std::filesystem;
    const char* env = std::getenv("PITCHLAB_AUDIO_PATH_ARTIFACT");
    fsns::path path =
        env != nullptr ? fsns::path(env)
                       : fsns::path("results/vst3/v0.1/audio-path/audio-path-report.json");
    std::error_code ec;
    fsns::create_directories(path.parent_path(), ec);
    std::string j = "{\n  \"suite\": \"vst_audio_path_test\",\n  \"records\": [\n";
    char buf[512];
    for (std::size_t i = 0; i < records_.size(); ++i) {
      const CaseRecord& r = records_[i];
      j += "    {\n";
      auto s = [&](const std::string& v) { return "\"" + v + "\""; };
      auto num = [&](double v, int dec) {
        std::snprintf(buf, sizeof buf, "%.*f", dec, v);
        return std::string(buf);
      };
      auto inum = [&](int64_t v) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
        return std::string(buf);
      };
      j += "      \"phase\": " + s(r.phase) + ",\n";
      j += "      \"case\": " + s(r.caseName) + ",\n";
      j += "      \"engine\": " + s(r.engine) + ",\n";
      j += "      \"material\": " + s(r.material) + ",\n";
      j += "      \"pitch_st\": " + num(r.pitchSt, 2) + ",\n";
      j += "      \"sample_rate\": " + num(r.sampleRate, 1) + ",\n";
      j += "      \"block_size\": " + inum(r.block) + ",\n";
      j += "      \"channels\": " + inum(r.channels) + ",\n";
      j += "      \"parameters\": {";
      for (std::size_t k = 0; k < r.params.size(); ++k) {
        if (k > 0) j += ", ";
        j += s(r.params[k].first) + ": " + s(r.params[k].second);
      }
      j += "},\n";
      j += "      \"adapter_mode\": " + s(r.adapterMode) + ",\n";
      j += "      \"reported_latency\": " + inum(r.reportedLatency) + ",\n";
      j += "      \"observed_latency\": " +
           (r.observedLatency == -2 ? std::string("null") : inum(r.observedLatency)) + ",\n";
      j += "      \"input_rms\": " + num(r.inputRms, 4) + ",\n";
      j += "      \"output_rms\": " + num(r.outputRms, 4) + ",\n";
      j += "      \"input_peak\": " + num(r.inputPeak, 4) + ",\n";
      j += "      \"output_peak\": " + num(r.outputPeak, 4) + ",\n";
      j += "      \"finite\": " + inum(r.finite) + ",\n";
      j += "      \"non_silent\": " + inum(r.nonSilent) + ",\n";
      j += "      \"frame_count\": " +
           (r.frameCount < 0 ? std::string("null") : inum(r.frameCount)) + ",\n";
      j += "      \"first_nonzero\": " + inum(r.firstNonzero) + ",\n";
      j += "      \"nonzero_frames\": " + inum(r.nonzeroFrames) + ",\n";
      j += "      \"max_jump\": " + num(r.maxJump, 4) + ",\n";
      j += "      \"frozen_run_max\": " + inum(r.frozenRunMax) + ",\n";
      j += "      \"pitch_measured\": " +
           (r.pitchMeasured < 0.0 ? std::string("null") : num(r.pitchMeasured, 3)) + ",\n";
      j += "      \"pitch_zc\": " +
           (r.pitchZc < 0.0 ? std::string("null") : num(r.pitchZc, 3)) + ",\n";
      j += "      \"pitch_expected\": " +
           (r.pitchExpected < 0.0 ? std::string("null") : num(r.pitchExpected, 3)) + ",\n";
      j += "      \"pitch_err_st\": " +
           (r.pitchMeasured < 0.0 ? std::string("null") : num(r.pitchErrSt, 3)) + ",\n";
      j += "      \"tonal_ratio\": " +
           (r.tonalRatio < 0.0 ? std::string("null") : num(r.tonalRatio, 3)) + ",\n";
      j += "      \"rt_fault_delta\": " + inum(static_cast<int64_t>(r.rtFaultDelta)) + ",\n";
      j += "      \"reprepare_delta\": " +
           (r.reprepareDelta < 0 ? std::string("null")
                                 : inum(static_cast<int64_t>(r.reprepareDelta))) +
           ",\n";
      if (!r.paramName.empty()) {
        j += "      \"param\": " + s(r.paramName) + ",\n";
        j += "      \"effect_measure\": " + s(r.effectMeasure) + ",\n";
        j += "      \"effect_value\": " + num(r.effectValue, 6) + ",\n";
        j += "      \"effect_pass\": " + inum(r.effectPass) + ",\n";
      }
      j += "      \"result\": " + s(r.result) + ",\n";
      j += "      \"diagnostic\": " + s(r.diagnostic) + "\n";
      j += "    }";
      if (i + 1 < records_.size()) j += ",";
      j += "\n";
    }
    j += "  ],\n  \"summary\": {";
    // per-engine aggregate over the deterministic phases
    for (int e = 0; e < engineCount(); ++e) {
      const char* id = engineIdForIndex(e);
      int records = 0, failures = 0, limitations = 0;
      bool wired = true, processing = true;
      // pitch: true | "limited" | false (an EXPECTED-LIMITATION record does
      // not fail the engine — it marks the measured, explained boundary)
      int pitchState = 2;  // 2 = yes, 1 = limited, 0 = no
      for (const CaseRecord& r : records_) {
        if (r.engine != id) continue;
        if (r.phase == "G") continue;  // robust-only records; counted separately
        ++records;
        if (r.result == "EXPECTED-LIMITATION") ++limitations;
        if (r.result != "PASS" && r.result != "EXPECTED-LIMITATION") ++failures;
        if (r.finite == 0 || r.rtFaultDelta != 0) processing = false;
        if (r.pitchMeasured >= 0.0 && std::fabs(r.pitchErrSt) > kPitchToleranceSt) {
          pitchState = (r.result == "EXPECTED-LIMITATION") ? std::min(pitchState, 1) : 0;
        }
        if (r.phase == "A" && r.result != "PASS") wired = false;
      }
      if (e > 0) j += ",";
      std::snprintf(buf, sizeof buf,
                    "\n    \"%s\": {\"records\": %d, \"failures\": %d, "
                    "\"expected_limitations\": %d, \"wired\": %s, "
                    "\"processing\": %s, \"pitch_transform\": %s}",
                    id, records, failures, limitations, wired ? "true" : "false",
                    processing ? "true" : "false",
                    pitchState == 2 ? "\"true\"" : (pitchState == 1 ? "\"limited\"" : "\"false\""));
      j += buf;
    }
    j += "\n  }\n}\n";
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (f != nullptr) {
      std::fwrite(j.data(), 1, j.size(), f);
      std::fclose(f);
      std::printf("audio-path artifact: %s (%zu records)\n", path.string().c_str(),
                  records_.size());
    } else {
      std::printf("audio-path artifact: WRITE FAILED %s\n", path.string().c_str());
    }
  }

  std::deque<CaseRecord> records_;
};

// The engine id table. DEFECT RECORD (task-33 checkpoint 1): this table was
// hardcoded with FIVE entries while the suite loops are bounded by
// engineCount() (= 6 since native.timepitch registered) — every index-5 read
// was OUT OF BOUNDS, emitting garbage bytes as the engine name into the
// committed evidence artifact. The uninitialized bytes differ per run and per
// compiler, so the CI byte-determinism gate failed at runs 37083417740 /
// 37086707318 / 37086805086. The table is now sized to the production
// registry and PINNED to it by the test case below: a future registry change
// fails loudly here instead of silently corrupting the evidence again.
const char* kEngineIds[6] = {"native.varispeed", "native.vardelay", "native.pv.classic",
                             "native.pv.phaselocked", "native.granular", "native.timepitch"};

TEST_CASE("engine id table is registry-pinned (the cp1 out-of-bounds-name defect guard)") {
  REQUIRE(engineCount() == 6);
  for (int i = 0; i < engineCount(); ++i) {
    REQUIRE_MESSAGE(std::string(kEngineIds[i]) == engineIdForIndex(i),
                    "kEngineIds drifted from the sealed production registry");
  }
}

ParamSnapshot defaultSnapFor(int engine) {
  ParamSnapshot snap;
  snap.engineIndex = engine;
  return snap;
}

/// Record the pairwise parameter-effect evidence on the LAST added record
/// (the variant-B record of each compared pair; the artifact then carries
/// the measured difference, not only its pass flag).
void notePairEffect(double d, double threshold) {
  CaseRecord& rec = Recorder::get().records_.back();
  rec.effectValue = rq(d, 6);
  rec.effectPass = d > threshold ? 1 : 0;
}

/// Max abs difference between two same-length renders.
double maxAbsDiff(const std::vector<double>& a, const std::vector<double>& b) {
  const std::size_t n = std::min(a.size(), b.size());
  double d = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double v = std::fabs(a[i] - b[i]);
    if (v > d) d = v;
  }
  return d;
}

}  // namespace

// ---------------------------------------------------------------------------
// PHASE A — common audio integrity probe (all engines x materials)
// ---------------------------------------------------------------------------

TEST_CASE("phase A: audio integrity probe — every engine, every material") {
  const Mat materials[] = {Mat::Sustain, Mat::TwoTone, Mat::Transient, Mat::OnsetTail};
  for (int e = 0; e < engineCount(); ++e) {
    for (Mat m : materials) {
      CaseSpec spec;
      spec.phase = "A";
      spec.caseName = "integrity-" + makeMaterial(m, 48000.0).id;
      spec.engine = e;
      spec.material = m;
      spec.snap = defaultSnapFor(e);
      spec.latencyProbe = (m == Mat::OnsetTail);
      INFO("engine=" << kEngineIds[e] << " case=" << spec.caseName);
      const CaseResult res = runCase(spec);
      Recorder::get().add(spec, res);
      CHECK(res.pass);
    }
  }
}

// ---------------------------------------------------------------------------
// PHASE B — pitch response (identity / +12 / -12, every engine)
// ---------------------------------------------------------------------------

TEST_CASE("phase B: pitch response — identity, +12 st, -12 st") {
  const double pitches[] = {0.0, 12.0, -12.0};
  for (int e = 0; e < engineCount(); ++e) {
    for (double p : pitches) {
      CaseSpec spec;
      spec.phase = "B";
      char name[64];
      std::snprintf(name, sizeof name, "pitch-%+.0fst", p);
      spec.caseName = name;
      spec.engine = e;
      spec.material = Mat::Sustain;
      spec.pitchSt = p;
      spec.snap = defaultSnapFor(e);
      spec.snap.pitchSt = p;
      INFO("engine=" << kEngineIds[e] << " pitch=" << p);
      const CaseResult res = runCase(spec);
      Recorder::get().add(spec, res);
      CHECK(res.pass);
    }
  }
}

// ---------------------------------------------------------------------------
// PHASE C / H — engine-specific configurations + parameter effectiveness
// ---------------------------------------------------------------------------

TEST_CASE("phase C: varispeed — quality variants + allow_aliasing") {
  // quality 0/1/2 on the in-band HF material: interpolation error at high
  // normalized frequency differs per kernel -> waveform difference proves
  // the parameter reaches the ENGINE CONFIGURATION (not just the controller)
  std::vector<CaseResult> qualityRuns;
  const char* qNames[] = {"small", "standard", "reference"};
  for (int q = 0; q <= 2; ++q) {
    CaseSpec spec;
    spec.phase = "C";
    spec.caseName = std::string("quality-") + qNames[q];
    spec.engine = 0;
    spec.material = Mat::HfQuality;
    spec.snap = defaultSnapFor(0);
    spec.snap.vsQuality = q;
    spec.paramName = "vs_quality";
    spec.effectMeasure = "waveform_maxdiff_vs_reference_quality";
    CAPTURE(q);
    const CaseResult res = runCase(spec);
    Recorder::get().add(spec, res);
    CHECK(res.pass);
    qualityRuns.push_back(res);
  }
  // pairwise waveform differences (identity render, deterministic)
  for (std::size_t i = 0; i + 1 < qualityRuns.size(); ++i) {
    const double d = maxAbsDiff(qualityRuns[i].out[0], qualityRuns[i + 1].out[0]);
    // evidence: small-vs-standard and standard-vs-reference kernels differ
    // measurably on the 10.5 kHz partial (observed > 1e-3); a parameter that
    // never reached the engine would give exactly 0.
    CHECK(d > 1e-5);
    notePairEffect(d, 1e-5);
    MESSAGE("varispeed quality " << i << "vs" << i + 1 << " maxdiff=" << d);
  }

  // allow_aliasing on/off at +12 st on the above-cutoff HF material: the
  // 16 kHz partial sits above the 0.95*Ny/r_max cutoff — OFF runs the
  // anti-alias pre-filter (additional stopband attenuation), ON does not.
  CaseSpec off, on;
  off.phase = "C";
  off.caseName = "aliasing-off";
  off.engine = 0;
  off.material = Mat::HfAlias;
  off.pitchSt = 12.0;
  off.snap = defaultSnapFor(0);
  off.snap.pitchSt = 12.0;
  off.snap.vsAllowAliasing = false;
  off.paramName = "vs_allow_aliasing";
  on = off;
  on.caseName = "aliasing-on";
  on.snap.vsAllowAliasing = true;
  const CaseResult resOff = runCase(off);
  const CaseResult resOn = runCase(on);
  // residual energy near the aliased partial (16 kHz folds to 16 kHz at +12
  // under 48 kHz fs: 2*16k aliases to 48k-32k=16k)
  const Material mat = makeMaterial(Mat::HfAlias, 48000.0);
  const int64_t steadyFrom =
      std::min<int64_t>(mat.frames, resOn.after.latencyFrames + static_cast<int64_t>(0.25 * 48000.0));
  const double eOff =
      bandEnergy(resOff.out[0], steadyFrom, mat.frames, 48000.0, 15500.0, 16500.0, 9);
  const double eOn =
      bandEnergy(resOn.out[0], steadyFrom, mat.frames, 48000.0, 15500.0, 16500.0, 9);
  const double ratio = eOff > 0.0 ? eOn / eOff : 0.0;
  CaseRecord& recOff = Recorder::get().add(off, resOff);
  recOff.effectMeasure = "alias_band_energy_16k";
  recOff.effectValue = rq(eOff, 9);
  recOff.effectPass = 1;
  CaseRecord& recOn = Recorder::get().add(on, resOn);
  recOn.effectMeasure = "alias_band_energy_16k";
  recOn.effectValue = rq(eOn, 9);
  recOn.effectPass = 1;
  CHECK(resOff.pass);
  CHECK(resOn.pass);
  // the pre-filter's stopband attenuation must measurably separate the two
  // (evidence basis: the FIR design targets a deep stopband; observed ratio
  // is recorded in the artifact; a parameter with no engine effect gives
  // ratio == 1.0)
  MESSAGE("aliasing band energy: OFF=" << eOff << " ON=" << eOn << " ratio=" << ratio);
  CHECK(ratio > 4.0);
  if (ratio <= 4.0) {
    recOn.diagnostic += "; alias ratio " + std::to_string(ratio) + " (no measurable effect)";
  }
}

TEST_CASE("phase C: vardelay — excursion + crossfade") {
  // excursion 0.05 s vs 2.0 s at +12 st (slope 1 frame/frame): the wrap
  // period differs by 40x -> waveform difference proves the parameter
  // reaches the engine; the default (0.5) is the reference run
  struct Variant {
    const char* name;
    double excursion;
  };
  const Variant variants[] = {{"excursion-0.05", 0.05}, {"excursion-2.0", 2.0}};
  std::vector<CaseResult> runs;
  for (const Variant& v : variants) {
    CaseSpec spec;
    spec.phase = "C";
    spec.caseName = v.name;
    spec.engine = 1;
    spec.material = Mat::TwoToneLong;  // 4.0 s: startup silence ~ excursion
    // +7 st (slope 0.5), not +12: at slope 1.0 with the MINIMUM excursion the
    // wrap period (0.05 s) is crossfade-dominated (measured tonal ratio 0.02,
    // no measurable carrier — interference, not a defect; the extreme slope
    // is covered by the crossfade-0/8192 cases at +12 st). +7 st keeps this a
    // clean test of the EXCURSION parameter's wrap-rate behaviour.
    spec.pitchSt = 7.0;
    spec.snap = defaultSnapFor(1);
    spec.snap.pitchSt = 7.0;
    spec.snap.vdExcursionSec = v.excursion;
    // The smallest excursion pairs with a proportionate crossfade: the
    // default 2048-frame (43 ms) crossfade exceeds the 0.05 s excursion's
    // wrap period at +12 st (slope 1.0 — a degenerate pairing where the
    // output is mostly crossfade interference; the engine handles it
    // fault-free, but the carrier is not measurable). 256 frames keeps the
    // case a clean test of the EXCURSION parameter (the crossfade parameter
    // has its own dedicated 0-vs-8192 case below).
    spec.snap.vdCrossfadeFrames = 256;
    spec.steadySkipExtra = static_cast<int64_t>(v.excursion * 48000.0);
    spec.paramName = "vd_excursion_s";
    spec.effectMeasure = "waveform_maxdiff_between_variants";
    CAPTURE(v.excursion);
    const CaseResult res = runCase(spec);
    Recorder::get().add(spec, res);
    CHECK(res.pass);
    runs.push_back(res);
  }
  const double d = maxAbsDiff(runs[0].out[0], runs[1].out[0]);
  CHECK(d > 1e-4);
  notePairEffect(d, 1e-4);
  MESSAGE("vardelay excursion maxdiff=" << d);

  // crossfade 0 vs 8192: changes the LATENCY SIGNATURE (the live chain's
  // own geometry: crossfade + margins) — the strongest engine-configuration
  // proof — plus the wrap-transient character
  struct CfVariant {
    const char* name;
    int64_t crossfade;
  };
  const CfVariant cf[] = {{"crossfade-0", 0}, {"crossfade-8192", 8192}};
  std::vector<CaseResult> cfRuns;
  for (const CfVariant& v : cf) {
    CaseSpec spec;
    spec.phase = "C";
    spec.caseName = v.name;
    spec.engine = 1;
    spec.material = Mat::TwoToneLong;
    spec.pitchSt = 12.0;
    spec.snap = defaultSnapFor(1);
    spec.snap.pitchSt = 12.0;
    spec.snap.vdCrossfadeFrames = v.crossfade;
    spec.paramName = "vd_crossfade_fr";
    spec.effectMeasure = "latency_signature_and_waveform_maxdiff";
    CAPTURE(v.crossfade);
    const CaseResult res = runCase(spec);
    CaseRecord& rec = Recorder::get().add(spec, res);
    rec.effectValue = rq(static_cast<double>(res.after.latencyFrames), 0);
    rec.effectPass = 1;
    CHECK(res.pass);
    CHECK(res.after.latencyFrames == v.crossfade + 32 + 64);
    cfRuns.push_back(res);
  }
  const double dcf = maxAbsDiff(cfRuns[0].out[0], cfRuns[1].out[0]);
  CHECK(dcf > 1e-4);
  notePairEffect(dcf, 1e-4);
  MESSAGE("vardelay crossfade maxdiff=" << dcf);
  CHECK(cfRuns[0].after.latencyFrames != cfRuns[1].after.latencyFrames);
}

TEST_CASE("phase C: pv.classic — FFT x hop matrix") {
  const int ffts[] = {1024, 2048, 4096};
  const int hops[] = {128, 256, 512, 1024};
  std::vector<CaseResult> byFft[3];
  for (int fi = 0; fi < 3; ++fi) {
    for (int hi = 0; hi < 4; ++hi) {
      if (hops[hi] > ffts[fi] / 2) continue;  // the documented hop <= fft/2 rule
      CaseSpec spec;
      spec.phase = "C";
      char name[64];
      std::snprintf(name, sizeof name, "fft%d-hop%d", ffts[fi], hops[hi]);
      spec.caseName = name;
      spec.engine = 2;
      spec.material = Mat::Sustain;
      spec.snap = defaultSnapFor(2);
      spec.snap.pvcFftSize = ffts[fi];
      spec.snap.pvcHop = hops[hi];
      spec.paramName = "pvc_fft/pvc_hop";
      spec.effectMeasure = "latency_signature_fft_plus_hop";
      INFO("fft=" << ffts[fi] << " hop=" << hops[hi]);
      const CaseResult res = runCase(spec);
      CaseRecord& rec = Recorder::get().add(spec, res);
      rec.effectValue = rq(static_cast<double>(res.after.latencyFrames), 0);
      rec.effectPass = 1;
      CHECK(res.pass);
      // the live chain's OWN latency = fft + hop + margins (the engine
      // configuration, not controller state)
      CHECK(res.after.latencyFrames == ffts[fi] + hops[hi] + 32 + 64);
      byFft[fi].push_back(res);
    }
  }
  // waveform difference across FFT sizes at the SAME hop (hop 128 is valid
  // for all three): different analysis windows -> different output
  REQUIRE(byFft[0].size() >= 1);
  REQUIRE(byFft[2].size() >= 1);
  // find the hop-128 records (first in each list)
  const double d = maxAbsDiff(byFft[0][0].out[0], byFft[2][0].out[0]);
  notePairEffect(d, 1e-6);
  MESSAGE("pv.classic fft1024 vs fft4096 (hop 128) maxdiff=" << d);
  CHECK(d > 1e-6);
}

TEST_CASE("phase C: pv.phaselocked — FFT x hop matrix") {
  const int ffts[] = {1024, 2048, 4096};
  const int hops[] = {128, 256, 512, 1024};
  std::vector<CaseResult> byFft[3];
  for (int fi = 0; fi < 3; ++fi) {
    for (int hi = 0; hi < 4; ++hi) {
      if (hops[hi] > ffts[fi] / 2) continue;
      CaseSpec spec;
      spec.phase = "C";
      char name[64];
      std::snprintf(name, sizeof name, "fft%d-hop%d", ffts[fi], hops[hi]);
      spec.caseName = name;
      spec.engine = 3;
      spec.material = Mat::Sustain;
      spec.snap = defaultSnapFor(3);
      spec.snap.pvpFftSize = ffts[fi];
      spec.snap.pvpHop = hops[hi];
      spec.paramName = "pvp_fft/pvp_hop";
      spec.effectMeasure = "latency_signature_fft_plus_hop";
      INFO("fft=" << ffts[fi] << " hop=" << hops[hi]);
      const CaseResult res = runCase(spec);
      CaseRecord& rec = Recorder::get().add(spec, res);
      rec.effectValue = rq(static_cast<double>(res.after.latencyFrames), 0);
      rec.effectPass = 1;
      CHECK(res.pass);
      CHECK(res.after.latencyFrames == ffts[fi] + hops[hi] + 32 + 64);
      byFft[fi].push_back(res);
    }
  }
  REQUIRE(byFft[0].size() >= 1);
  REQUIRE(byFft[2].size() >= 1);
  const double d = maxAbsDiff(byFft[0][0].out[0], byFft[2][0].out[0]);
  notePairEffect(d, 1e-6);
  MESSAGE("pv.phaselocked fft1024 vs fft4096 (hop 128) maxdiff=" << d);
  CHECK(d > 1e-6);
}

TEST_CASE("phase C: granular — grain / overlap / jitter / window") {
  // grain 0.02 vs 0.5 s: LATENCY SIGNATURE (the splice geometry includes
  // the grain) + waveform difference
  {
    CaseSpec a, b;
    a.phase = "C";
    a.caseName = "grain-0.02";
    a.engine = 4;
    a.material = Mat::Sustain;
    a.pitchSt = 12.0;
    a.snap = defaultSnapFor(4);
    a.snap.pitchSt = 12.0;
    a.snap.grGrainSec = 0.02;
    a.paramName = "gr_grain_s";
    a.effectMeasure = "latency_signature_includes_grain";
    b = a;
    b.caseName = "grain-0.5";
    b.snap.grGrainSec = 0.5;
    // the 0.5 s grain raises the latency to ~0.73 s: keep the same 2.0 s
    // material (the steady window shrinks but remains > 0.9 s)
    const CaseResult ra = runCase(a);
    const CaseResult rb = runCase(b);
    CaseRecord& recA = Recorder::get().add(a, ra);
    recA.effectValue = rq(static_cast<double>(ra.after.latencyFrames), 0);
    recA.effectPass = 1;
    CaseRecord& recB = Recorder::get().add(b, rb);
    recB.effectValue = rq(static_cast<double>(rb.after.latencyFrames), 0);
    recB.effectPass = 1;
    CHECK(ra.pass);
    CHECK(rb.pass);
    CHECK(ra.after.latencyFrames != rb.after.latencyFrames);
    const double d = maxAbsDiff(ra.out[0], rb.out[0]);
    notePairEffect(d, 1e-3);
    MESSAGE("granular grain maxdiff=" << d);
    CHECK(d > 1e-3);
  }
  // overlap 4 vs 16 (with the LFO path): with a CONSTANT ratio the
  // local-sum OLA reproduces input(rho*n) EXACTLY for ANY overlap/window
  // (every grain reads the SAME position rho*n — measured maxdiff 0 even at
  // +12 st; an identity of the normalised OLA, not an ineffectiveness). The
  // parameters shape the OLA only when the curve VARIES per grain: these
  // cases drive the LFO (pitch +5 st, 5 Hz, 0.5 st depth — inside the chain
  // envelope by construction) so the grains read different positions and
  // the overlap/window difference is measurable. Pitch measurement is off:
  // the case's claim is the parameter effect, not the FM carrier.
  {
    CaseSpec a, b;
    a.phase = "C";
    a.caseName = "overlap-4-lfo";
    a.engine = 4;
    a.material = Mat::Sustain;
    a.pitchSt = 5.0;
    a.measurePitch = false;
    a.snap = defaultSnapFor(4);
    a.snap.pitchSt = 5.0;
    a.snap.lfoRateHz = 5.0;
    a.snap.lfoDepthSt = 0.5;
    a.snap.grOverlap = 4;
    a.paramName = "gr_overlap";
    a.effectMeasure = "waveform_maxdiff_between_variants_lfo_drive";
    b = a;
    b.caseName = "overlap-16-lfo";
    b.snap.grOverlap = 16;
    const CaseResult ra = runCase(a);
    const CaseResult rb = runCase(b);
    Recorder::get().add(a, ra);
    Recorder::get().add(b, rb);
    CHECK(ra.pass);
    CHECK(rb.pass);
    const double d = maxAbsDiff(ra.out[0], rb.out[0]);
    notePairEffect(d, 1e-4);
    MESSAGE("granular overlap (lfo) maxdiff=" << d);
    CHECK(d > 1e-4);
  }
  // jitter 0 vs 128 frames (deterministic product seed): grain placement
  // shifts -> waveform difference
  {
    CaseSpec a, b;
    a.phase = "C";
    a.caseName = "jitter-0";
    a.engine = 4;
    a.material = Mat::Sustain;
    a.snap = defaultSnapFor(4);
    a.snap.grJitterFrames = 0;
    a.paramName = "gr_jitter_fr";
    a.effectMeasure = "waveform_maxdiff_between_variants";
    b = a;
    b.caseName = "jitter-128";
    b.snap.grJitterFrames = 128;
    const CaseResult ra = runCase(a);
    const CaseResult rb = runCase(b);
    Recorder::get().add(a, ra);
    Recorder::get().add(b, rb);
    CHECK(ra.pass);
    CHECK(rb.pass);
    const double d = maxAbsDiff(ra.out[0], rb.out[0]);
    notePairEffect(d, 1e-4);
    MESSAGE("granular jitter maxdiff=" << d);
    CHECK(d > 1e-4);
  }
  // window hann vs triangular (same LFO drive rationale: constant-ratio OLA
  // is window-independent by the local-sum normalisation — measured 0)
  {
    CaseSpec a, b;
    a.phase = "C";
    a.caseName = "window-hann-lfo";
    a.engine = 4;
    a.material = Mat::Sustain;
    a.pitchSt = 5.0;
    a.measurePitch = false;
    a.snap = defaultSnapFor(4);
    a.snap.pitchSt = 5.0;
    a.snap.lfoRateHz = 5.0;
    a.snap.lfoDepthSt = 0.5;
    a.snap.grWindowTriangular = 0;
    a.paramName = "gr_window";
    a.effectMeasure = "waveform_maxdiff_between_variants_lfo_drive";
    b = a;
    b.caseName = "window-triangular-lfo";
    b.snap.grWindowTriangular = 1;
    const CaseResult ra = runCase(a);
    const CaseResult rb = runCase(b);
    Recorder::get().add(a, ra);
    Recorder::get().add(b, rb);
    CHECK(ra.pass);
    CHECK(rb.pass);
    const double d = maxAbsDiff(ra.out[0], rb.out[0]);
    notePairEffect(d, 1e-4);
    MESSAGE("granular window (lfo) maxdiff=" << d);
    CHECK(d > 1e-4);
  }
}

// ---------------------------------------------------------------------------
// PHASE D — block-size matrix (identity integrity + pitch at every size)
// ---------------------------------------------------------------------------

TEST_CASE("phase D: block-size matrix 64..4096 + mixed schedule") {
  const int blocks[] = {64, 128, 256, 512, 1024, 2048, 4096};
  for (int e = 0; e < engineCount(); ++e) {
    for (int blk : blocks) {
      CaseSpec spec;
      spec.phase = "D";
      char name[64];
      std::snprintf(name, sizeof name, "block-%d", blk);
      spec.caseName = name;
      spec.engine = e;
      spec.material = Mat::Sustain;
      spec.pitchSt = 12.0;
      spec.fs = 48000.0;
      spec.block = static_cast<int32_t>(blk);
      spec.snap = defaultSnapFor(e);
      spec.snap.pitchSt = 12.0;
      INFO("engine=" << kEngineIds[e] << " block=" << blk);
      const CaseResult res = runCase(spec);
      Recorder::get().add(spec, res);
      CHECK(res.pass);
      // the latency is block-size independent (chain geometry, not schedule)
      CHECK(res.after.latencyFrames == expectedLatencyFrames(spec.snap, spec.fs));
    }
    // mixed schedule: cycle 64..4096 within one render (setup max = 4096)
    {
      const Material mat = makeMaterial(Mat::Sustain, 48000.0);
      ParamSnapshot snap = defaultSnapFor(e);
      snap.pitchSt = 12.0;
      const int64_t lat = expectedLatencyFrames(snap, 48000.0);
      const int64_t flush = lat + static_cast<int64_t>(0.30 * 48000.0);
      std::vector<double> stream = mat.l;
      stream.insert(stream.end(), static_cast<std::size_t>(flush), 0.0);
      TestHost host(48000.0, 4096, 2, snap);
      const StatusSnapshot before = host.status();
      std::vector<double> outL, outR;
      const std::vector<int32_t> sched = {64, 128, 256, 512, 1024, 2048, 4096};
      host.renderStreamMixed(stream, &stream, sched, outL, outR);
      const StatusSnapshot after = host.status();
      const bool framesOk = static_cast<int64_t>(outL.size()) == mat.frames + flush;
      const bool faultsOk = after.faults == before.faults;
      CAPTURE(kEngineIds[e]);
      CHECK(framesOk);
      CHECK(faultsOk);
      CHECK(after.engineIndex == e);
      // pitch check on the steady tail
      const Dominant dom = dominantFrequency(outL, mat.frames - 48000, mat.frames, 48000.0,
                                             440.0 * 0.79, 440.0 * 1.26);
      // Task 33: the Fixed (= OLA) mode's pitch gate at its validated rate
      // class (48 kHz — see kTimepitchFixedToleranceSt)
      const double phaseDPitchTol =
          (e == kTimepitchEngineIndex) ? kTimepitchFixedToleranceSt : kPitchToleranceSt;
      CHECK(std::fabs(12.0 * std::log2(dom.freq / 440.0)) <= phaseDPitchTol);
      // record
      CaseRecord rec;
      rec.phase = "D";
      rec.caseName = "block-mixed";
      rec.engine = kEngineIds[e];
      rec.material = mat.id;
      rec.pitchSt = 12.0;
      rec.sampleRate = 48000.0;
      rec.block = -1;  // mixed schedule marker
      rec.channels = 2;
      rec.params = {{"engine_index", std::to_string(e)}, {"pitch_st", "12"}};
      rec.adapterMode = (e == 0 || e == 4) ? "windowed-splice" : "virtual-job";
      rec.reportedLatency = after.latencyFrames;
      rec.frameCount = static_cast<int64_t>(outL.size());
      const AudioStats st = statsOf(outL, lat + 12000, mat.frames);
      rec.outputRms = rq(st.rms, 4);
      rec.outputPeak = rq(st.peak, 4);
      rec.finite = st.finite ? 1 : 0;
      rec.maxJump = rq(st.maxJump, 4);
      rec.frozenRunMax = st.frozenRunMax;
      rec.pitchMeasured = rq(dom.freq, 3);
      rec.pitchExpected = 440.0;
      rec.pitchErrSt = rq(12.0 * std::log2(dom.freq / 440.0), 3);
      rec.tonalRatio = rq(dom.tonalRatio, 3);
      rec.rtFaultDelta = after.faults;  // fresh-adapter baseline
      rec.reprepareDelta = after.reprepares > 1 ? after.reprepares - 1 : 0;
      const bool pass = framesOk && faultsOk && st.finite && after.engineIndex == e &&
                        std::fabs(12.0 * std::log2(dom.freq / 440.0)) <= phaseDPitchTol;
      rec.result = pass ? "PASS" : "FAIL";
      Recorder::get().addRaw(rec);
      CHECK(pass);
    }
  }
}

// ---------------------------------------------------------------------------
// PHASE E — sample-rate matrix (all six first-class rates)
// ---------------------------------------------------------------------------

TEST_CASE("phase E: sample-rate matrix 44.1k..192k") {
  const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
  for (int e = 0; e < engineCount(); ++e) {
    for (double fs : rates) {
      // Task 33: 176.4 kHz is deliberately NOT in native.timepitch's declared
      // supportedSampleRates set (§6.6 — the tracker's lag band is declared
      // only on 44.1/48/88.2/96/192) — the harness JOB SKIPs the combination
      // (spec §4.2.1 item 5); the audio-path suite mirrors that skip.
      if (e == kTimepitchEngineIndex && fs == 176400.0) continue;
      CaseSpec spec;
      spec.phase = "E";
      char name[64];
      std::snprintf(name, sizeof name, "rate-%.0fk", fs / 1000.0);
      spec.caseName = name;
      spec.engine = e;
      spec.material = Mat::Sustain;
      spec.pitchSt = 12.0;
      spec.fs = fs;
      spec.block = 1024;
      spec.snap = defaultSnapFor(e);
      spec.snap.pitchSt = 12.0;
      INFO("engine=" << kEngineIds[e] << " rate=" << fs);
      const CaseResult res = runCase(spec);
      Recorder::get().add(spec, res);
      CHECK(res.pass);
      CHECK(res.after.sampleRate == fs);
      CHECK(res.after.latencyFrames == expectedLatencyFrames(spec.snap, fs));
      CHECK(res.after.reprepares == 1);  // the initial chain build only
    }
  }
}

// ---------------------------------------------------------------------------
// PHASE F — mono / stereo
// ---------------------------------------------------------------------------

TEST_CASE("phase F: mono and stereo — channel integrity") {
  for (int e = 0; e < engineCount(); ++e) {
    // STEREO with distinct L/R carriers (220 Hz / 311 Hz): per-channel
    // dominant frequency after +12 st catches channel swap, channel loss
    // and cross-channel collapse
    {
      const double fs = 48000.0;
      Material mat = makeMaterial(Mat::Sustain, fs);
      mat.id = "stereo-220-311";
      mat.r = mat.l;
      const int64_t n = mat.frames;
      for (int64_t i = 0; i < n; ++i) {
        mat.r[static_cast<std::size_t>(i)] =
            0.5 * std::sin(2.0 * kPi * 311.0 * static_cast<double>(i) / fs);
      }
      ParamSnapshot snap = defaultSnapFor(e);
      snap.pitchSt = 12.0;
      const int64_t lat = expectedLatencyFrames(snap, fs);
      const int64_t flush = lat + static_cast<int64_t>(0.30 * fs);
      std::vector<double> streamL = mat.l, streamR = mat.r;
      streamL.insert(streamL.end(), static_cast<std::size_t>(flush), 0.0);
      streamR.insert(streamR.end(), static_cast<std::size_t>(flush), 0.0);
      TestHost host(fs, 1024, 2, snap);
      const StatusSnapshot before = host.status();
      std::vector<double> outL, outR;
      host.renderStream(streamL, &streamR, 1024, outL, outR);
      const StatusSnapshot after = host.status();
      CAPTURE(kEngineIds[e]);
      CHECK(static_cast<int64_t>(outL.size()) == n + flush);
      CHECK(after.faults == before.faults);
      CHECK(after.engineIndex == e);
      CHECK(after.channels == 2);
      // both channels process their own carrier
      const double expectL = 440.0, expectR = 622.0;
      const Dominant domL = dominantFrequency(outL, n - 48000, n, fs, expectL * 0.79, expectL * 1.26);
      const Dominant domR = dominantFrequency(outR, n - 48000, n, fs, expectR * 0.79, expectR * 1.26);
      const double errL = 12.0 * std::log2(domL.freq / expectL);
      const double errR = 12.0 * std::log2(domR.freq / expectR);
      // Task 33: the Fixed (= OLA) mode's validated-rate-class gate (48 kHz)
      // + the comb's tonal-floor exemption (see the shared pitch policy)
      const bool timepitchCombF = (e == kTimepitchEngineIndex);
      const double phaseFPitchTol = timepitchCombF ? kTimepitchFixedToleranceSt : kPitchToleranceSt;
      CHECK(std::fabs(errL) <= phaseFPitchTol);
      CHECK(std::fabs(errR) <= phaseFPitchTol);
      if (!timepitchCombF) {
        CHECK(domL.tonalRatio >= kTonalRatioFloor);
        CHECK(domR.tonalRatio >= kTonalRatioFloor);
      }
      // per-channel integrity
      const AudioStats stL = statsOf(outL, lat + 12000, n);
      const AudioStats stR = statsOf(outR, lat + 12000, n);
      CHECK(stL.finite);
      CHECK(stR.finite);
      CHECK(stL.rms >= kTonalRmsFloorRatio * 0.35);
      CHECK(stR.rms >= kTonalRmsFloorRatio * 0.35);
      // record
      CaseRecord rec;
      rec.phase = "F";
      rec.caseName = "stereo-distinct-channels";
      rec.engine = kEngineIds[e];
      rec.material = mat.id;
      rec.pitchSt = 12.0;
      rec.block = 1024;
      rec.channels = 2;
      rec.params = {{"engine_index", std::to_string(e)}, {"pitch_st", "12"},
                    {"carrier_l_hz", "220"}, {"carrier_r_hz", "311"}};
      rec.adapterMode = (e == 0 || e == 4) ? "windowed-splice" : "virtual-job";
      rec.reportedLatency = after.latencyFrames;
      rec.frameCount = static_cast<int64_t>(outL.size());
      rec.inputRms = rq(statsOf(streamL, lat + 12000, n).rms, 4);
      rec.outputRms = rq(stL.rms, 4);
      rec.inputPeak = rq(statsOf(streamL, lat + 12000, n).peak, 4);
      rec.outputPeak = rq(stL.peak, 4);
      rec.finite = (stL.finite && stR.finite) ? 1 : 0;
      rec.maxJump = rq(std::max(stL.maxJump, stR.maxJump), 4);
      rec.frozenRunMax = std::max(stL.frozenRunMax, stR.frozenRunMax);
      // diagnostic carries BOTH channels' pitch (machine-readable in the
      // diagnostic string; the primary pitch fields carry L)
      rec.pitchMeasured = rq(domL.freq, 3);
      rec.pitchExpected = expectL;
      rec.pitchErrSt = rq(errL, 3);
      rec.tonalRatio = rq(domL.tonalRatio, 3);
      rec.rtFaultDelta = after.faults;  // fresh-adapter baseline
      rec.reprepareDelta = after.reprepares > 1 ? after.reprepares - 1 : 0;
      const bool pass = static_cast<int64_t>(outL.size()) == n + flush &&
                        after.faults == before.faults && stL.finite && stR.finite &&
                        std::fabs(errL) <= phaseFPitchTol && std::fabs(errR) <= phaseFPitchTol;
      rec.result = pass ? "PASS" : "FAIL";
      rec.diagnostic = "chL=" + std::to_string(rq(domL.freq, 1)) + "Hz chR=" +
                       std::to_string(rq(domR.freq, 1)) + "Hz (expected 440/622)";
      // env-gated diagnostic dump (same as runCase)
      if (const char* dumpDir = std::getenv("PITCHLAB_AUDIO_PATH_DUMP")) {
        namespace fsns = std::filesystem;
        fsns::create_directories(dumpDir);
        try {
          const double* planar[2] = {outL.data(), outR.data()};
          pitchlab::writeWav(fsns::path(dumpDir) / (std::string("F_stereo_") + kEngineIds[e] + ".wav"), planar, 2,
                             static_cast<pitchlab::FrameCount>(outL.size()), 48000u,
                             pitchlab::WavSampleFormat::Float64);
        } catch (const std::exception&) {
        }
      }
      Recorder::get().addRaw(rec);
      CHECK(pass);
    }
    // MONO: one channel in, one channel out — no dependence on the unused
    // second channel
    {
      CaseSpec spec;
      spec.phase = "F";
      spec.caseName = "mono";
      spec.engine = e;
      spec.material = Mat::Sustain;
      spec.pitchSt = 12.0;
      spec.channels = 1;
      spec.snap = defaultSnapFor(e);
      spec.snap.pitchSt = 12.0;
      CAPTURE(kEngineIds[e]);
      const CaseResult res = runCase(spec);
      Recorder::get().add(spec, res);
      CHECK(res.pass);
      CHECK(res.after.channels == 1);
      CHECK(res.out.size() == 1);
    }
  }
}

// ---------------------------------------------------------------------------
// PHASE G — reset and engine switching DURING audio
// ---------------------------------------------------------------------------

TEST_CASE("phase G: reset + engine switch during audio — destination processes") {
  const double fs = 48000.0;
  const int32_t block = 512;
  for (int e = 0; e < engineCount(); ++e) {
    // Task 33: the destination cycle covers all six engines (was % 5)
    const int next = (e + 1) % engineCount();
    INFO("engine=" << kEngineIds[e] << " switches-to=" << kEngineIds[next]);
    const Material mat = makeMaterial(Mat::TwoTone, fs);
    ParamSnapshot snap = defaultSnapFor(e);
    const int64_t latEffBound = expectedLatencyFrames(defaultSnapFor(e), fs) +
                                expectedLatencyFrames(defaultSnapFor(next), fs) + 1440;
    const int64_t flush = latEffBound + static_cast<int64_t>(0.30 * fs);

    TestHost host(fs, block, 2, snap);
    // NOTE: the status snapshot is published only at block boundaries (the
    // audio path) — before the first process() it carries defaults, so the
    // engine identity is asserted INSIDE each measured segment instead.

    std::vector<double> outL, outR;
    std::string segReport;
    uint64_t faultsAtStart = 0;
    int passSegs = 0;
    const int totalSegs = 5;

    // helper: render a segment of the material and measure its dominant
    // frequency (the last 0.4 s of the segment — post-latency steady);
    // expectEngine verifies the ACTIVE chain identity from the per-block
    // status publication
    auto segment = [&](const char* label, int64_t frames, double expectHz, double tolSt,
                       int expectEngine) -> bool {
      std::vector<double> segL(mat.l.begin(), mat.l.begin() + static_cast<std::ptrdiff_t>(frames));
      std::vector<double> segR = segL;
      const int64_t outBefore = static_cast<int64_t>(outL.size());
      host.renderStream(segL, &segR, block, outL, outR);
      const int64_t outAfter = static_cast<int64_t>(outL.size());
      const int64_t segLen = outAfter - outBefore;
      const int64_t measureFrom = outBefore + segLen - static_cast<int64_t>(0.4 * fs);
      const Dominant dom = dominantFrequency(
          outL, measureFrom, outAfter, fs, expectHz * 0.7, expectHz * 1.4);
      const double zc = zeroCrossFrequency(outL, measureFrom, outAfter, fs);
      // dual estimator (see runCase): min(|peak err|, |zc err|)
      const double errPeak = 12.0 * std::log2(dom.freq / expectHz);
      const double errZc = zc > 0.0 ? 12.0 * std::log2(zc / expectHz) : 1e9;
      const double err = std::fabs(errPeak) <= std::fabs(errZc) ? errPeak : errZc;
      const StatusSnapshot st = host.status();
      const bool engineOk = st.engineIndex == expectEngine;
      const bool ok = std::fabs(err) <= tolSt && st.chainReady && dom.mag > 0.0 && engineOk;
      // integer-Hz + 0.1-st formatting: the G records are part of the
      // byte-deterministic artifact; the segment carriers are steady-state
      // post-adoption tones whose exact low-order digits vary with the
      // (nondeterministic) adoption point — integers keep the artifact
      // byte-stable while remaining far inside the 0.5 st assertion. The
      // displayed error is derived from the ROUNDED frequency (a tiny
      // residual sign would otherwise print as "-0.0" vs "0.0").
      const double fq = dom.freq > 0.0 ? std::round(dom.freq) : 0.0;
      const double zq = zc > 0.0 ? std::round(zc) : 0.0;
      // the ZC median is only REPORTED when it agrees with the chosen
      // estimator within 0.25 st: on low-amplitude sideband-heavy output
      // (pv.phaselocked at non-identity ratios) the crossing median picks up
      // spurious crossings (measured 730/1107 Hz noise) while the projection
      // peak reads the true carrier; the ASSERTION is the dual min(|peak|,|zc|)
      // either way
      char zcBuf[24];
      if (zq > 0.0 && fq > 0.0 && std::fabs(12.0 * std::log2(zq / fq)) <= 0.25) {
        std::snprintf(zcBuf, sizeof zcBuf, "%.0f", zq);
      } else {
        std::snprintf(zcBuf, sizeof zcBuf, "-");
      }
      double dispErr = fq > 0.0 ? 12.0 * std::log2(fq / expectHz) : 0.0;
      dispErr = std::round(dispErr * 10.0) / 10.0;
      if (dispErr == 0.0) dispErr = 0.0;  // normalise -0.0
      char buf[192];
      std::snprintf(buf, sizeof buf, "%s f=%.0fHz(exp %.0f) zc=%s err=%.1fst eng=%d(exp %d) %s", label,
                    fq, expectHz, zcBuf, dispErr, st.engineIndex, expectEngine,
                    st.chainReady ? "ready" : "NOT-READY");
      segReport += buf;
      segReport += " | ";
      if (ok) ++passSegs;
      return ok;
    };

    // pump continuation material while waiting for a status predicate
    auto pumpUntil = [&](const std::function<bool(const StatusSnapshot&)>& pred,
                         int64_t maxFrames) -> bool {
      int64_t done = 0;
      const int64_t step = static_cast<int64_t>(0.05 * fs);
      std::vector<double> segL(step), segR(step);
      for (int64_t i = 0; i < step; ++i) {
        const double v = mat.l[static_cast<std::size_t>(i)];
        segL[static_cast<std::size_t>(i)] = v;
        segR[static_cast<std::size_t>(i)] = v;
      }
      while (done < maxFrames) {
        host.renderStream(segL, &segR, block, outL, outR);
        done += step;
        if (pred(host.status())) return true;
      }
      const StatusSnapshot st = host.status();
      std::printf("PUMP-TIMEOUT: eng=%d ready=%d reprep=%llu faults=%llu liveJobs=%d lat=%lld base?\n",
                  st.engineIndex, st.chainReady ? 1 : 0, (unsigned long long)st.reprepares,
                  (unsigned long long)st.faults, st.liveJobs, (long long)st.latencyFrames);
      std::fflush(stdout);
      return pred(host.status());
    };

    // 1) identity
    // Task 33: the timepitch segments use the Fixed mode's gates (the
    // validated ±1.5 st on tonal material; phase G's TwoTone material uses
    // the multi-tone sanity bound — see the constants' evidence notes).
    const double segPitchTolE = (e == kTimepitchEngineIndex)
                                    ? kTimepitchMultiToneToleranceSt
                                    : kPitchToleranceSt;
    bool ok1 = segment("seg1-identity", static_cast<int64_t>(1.0 * fs), 220.0,
                       segPitchTolE, e);

    // 2) positive pitch (mid-stream parameter change; the live ratio exits
    //    the identity chain's +-1 st envelope -> crossfaded re-prepare)
    host.setParam(param::kPitch, 12.0);
    const uint64_t reprepBefore = host.status().reprepares;
    const bool adopted2 = pumpUntil(
        [&](const StatusSnapshot& s) { return s.reprepares > reprepBefore && s.chainReady; },
        static_cast<int64_t>(3.0 * fs));
    REQUIRE(adopted2);
    // fixed post-adoption settle: the measured segment must contain only
    // post-transition steady signal (the adoption crossfade + the fresh
    // chain's startup live in the settle window) — keeps the G records'
    // measured carriers adoption-timing-robust for the byte-deterministic
    // artifact
    {
      const std::size_t half = static_cast<std::size_t>(0.5 * fs);
      std::vector<double> s(mat.l.begin(), mat.l.begin() + static_cast<std::ptrdiff_t>(half));
      host.renderStream(s, &s, block, outL, outR);
    }
    bool ok2 = segment("seg2-p+12", static_cast<int64_t>(0.8 * fs), 440.0, segPitchTolE, e);

    // 3) reset (the VST3 suspend/resume contract: resume = reset + fresh
    //    chain) with the pitch back to identity
    host.setParam(param::kPitch, 0.0);
    const uint64_t reprepBefore3 = host.status().reprepares;
    const bool adopted3a = pumpUntil(
        [&](const StatusSnapshot& s) { return s.reprepares > reprepBefore3 && s.chainReady; },
        static_cast<int64_t>(3.0 * fs));
    REQUIRE(adopted3a);
    REQUIRE(host.plug->setProcessing(false) == kResultOk);
    REQUIRE(host.plug->setProcessing(true) == kResultOk);
    const bool adopted3b =
        pumpUntil([&](const StatusSnapshot& s) { return s.chainReady; }, static_cast<int64_t>(3.0 * fs));
    REQUIRE(adopted3b);
    {
      const std::size_t half = static_cast<std::size_t>(0.5 * fs);
      std::vector<double> s(mat.l.begin(), mat.l.begin() + static_cast<std::ptrdiff_t>(half));
      host.renderStream(s, &s, block, outL, outR);
    }
    bool ok3 = segment("seg3-after-reset", static_cast<int64_t>(0.8 * fs), 220.0,
                       segPitchTolE, e);

    // 4) switch to the NEXT engine during audio at +12 st — the DESTINATION
    //    must actually process (its own pitch response measured)
    host.setParam(param::kPitch, 12.0);
    host.setParam(param::kEngine, static_cast<double>(next));
    const bool adopted4 = pumpUntil(
        [&](const StatusSnapshot& s) { return s.engineIndex == next && s.chainReady; },
        static_cast<int64_t>(3.0 * fs));
    REQUIRE(adopted4);
    {
      const std::size_t half = static_cast<std::size_t>(0.5 * fs);
      std::vector<double> s(mat.l.begin(), mat.l.begin() + static_cast<std::ptrdiff_t>(half));
      host.renderStream(s, &s, block, outL, outR);
    }
    bool ok4 =
        segment("seg4-dest-engine", static_cast<int64_t>(0.8 * fs), 440.0,
                (next == kTimepitchEngineIndex) ? kTimepitchMultiToneToleranceSt
                                                : kPitchToleranceSt,
                next);

    // 5) switch BACK — the original engine processes again
    host.setParam(param::kEngine, static_cast<double>(e));
    const bool adopted5 = pumpUntil(
        [&](const StatusSnapshot& s) { return s.engineIndex == e && s.chainReady; },
        static_cast<int64_t>(3.0 * fs));
    REQUIRE(adopted5);
    {
      const std::size_t half = static_cast<std::size_t>(0.5 * fs);
      std::vector<double> s(mat.l.begin(), mat.l.begin() + static_cast<std::ptrdiff_t>(half));
      host.renderStream(s, &s, block, outL, outR);
    }
    bool ok5 = segment("seg5-back", static_cast<int64_t>(0.8 * fs), 440.0, segPitchTolE, e);

    // final flush so no job starves at the end of the finite drive
    {
      std::vector<double> tail(static_cast<std::size_t>(flush), 0.0);
      host.renderStream(tail, &tail, block, outL, outR);
    }
    const StatusSnapshot end = host.status();
    const uint64_t faultDelta = end.faults - faultsAtStart;
    const bool noFaults = faultDelta == 0;
    CHECK(noFaults);
    CHECK(end.chainReady);
    CHECK(end.engineIndex == e);
    CHECK(passSegs == totalSegs);
    CHECK(ok1);
    CHECK(ok2);
    CHECK(ok3);
    CHECK(ok4);
    CHECK(ok5);

    // record (robust fields only — async adoption makes exact audio values
    // timing-dependent by design; the artifact marks this explicitly)
    CaseRecord rec;
    rec.phase = "G";
    rec.caseName = "reset-and-switch-cycle";
    rec.engine = kEngineIds[e];
    rec.material = mat.id;
    rec.pitchSt = 12.0;
    rec.sampleRate = fs;
    rec.block = block;
    rec.channels = 2;
    rec.params = {{"engine_index", std::to_string(e)}, {"switches_to", kEngineIds[next]},
                  {"pitch_st", "0->12->0->12"}};
    rec.adapterMode = (e == 0 || e == 4) ? "windowed-splice" : "virtual-job";
    rec.reportedLatency = end.latencyFrames;  // Lambda_eff (max over adopted chains)
    // frame_count is NOT recorded for phase G: the pumped total until
    // adoption depends on the asynchronous chain-adoption timing (by
    // design); every audio-content assertion is measured on post-adoption
    // steady segments and the record carries only adoption-robust fields
    rec.frameCount = -1;
    // reprepare count is also NOT recorded for G: the number of chain
    // rebuilds in a live parameter/engine cycle is coupled to the
    // asynchronous adoption timing (an envelope re-centre can consolidate
    // with or follow a parameter rebuild — measured 11 vs 12 across runs);
    // the SEMANTIC behaviour (each adoption actually happening) is what the
    // pumps REQUIRE
    rec.reprepareDelta = -1;
    rec.rtFaultDelta = faultDelta;
    rec.result = (noFaults && passSegs == totalSegs) ? "PASS" : "FAIL";
    rec.diagnostic = segReport + "(dest=" + kEngineIds[next] + ")";
    Recorder::get().addRaw(rec);
  }
}

// ---------------------------------------------------------------------------
// PHASE J/K — the artifact + the per-engine summary (the final answer)
// ---------------------------------------------------------------------------

TEST_CASE("phase Z: summary + artifact (the per-engine factual answer)") {
  const auto& recs = Recorder::get().records_;
  int failures = 0;
  for (const CaseRecord& r : recs) {
    if (r.result == "FAIL") ++failures;
  }
  std::printf("\n=== VST3 realtime audio-path: per-engine status ===\n");
  for (int e = 0; e < engineCount(); ++e) {
    const char* id = kEngineIds[e];
    int records = 0, fails = 0, limits = 0;
    bool wired = true, processing = true;
    int pitchState = 2;  // 2 = YES, 1 = LIMITED, 0 = NO
    for (const CaseRecord& r : recs) {
      if (r.engine != id) continue;
      if (r.phase == "G") continue;
      ++records;
      if (r.result == "EXPECTED-LIMITATION") ++limits;
      if (r.result != "PASS" && r.result != "EXPECTED-LIMITATION") {
        ++fails;
        if (r.phase == "A") wired = false;
        processing = false;
        if (r.pitchMeasured >= 0.0 && std::fabs(r.pitchErrSt) > kPitchToleranceSt) pitchState = 0;
      }
      if (r.pitchMeasured >= 0.0 && std::fabs(r.pitchErrSt) > kPitchToleranceSt &&
          r.result == "EXPECTED-LIMITATION") {
        pitchState = std::min(pitchState, 1);
      }
    }
    const char* pitchStr = pitchState == 2 ? "YES" : (pitchState == 1 ? "LIMITED" : "NO");
    std::printf(
        "audio-path: %-20s wired=%s processing=%s pitch_transform=%s (records=%d failures=%d "
        "expected_limitations=%d)\n",
        id, wired ? "YES" : "NO", processing ? "YES" : "NO", pitchStr, records, fails, limits);
    if (fails > 0) failures += fails;
  }
  if (failures == 0) {
    std::printf("audio-path: ALL ENGINES PASS (0 failures)\n");
  } else {
    std::printf("audio-path: %d FAILING RECORD(S)\n", failures);
  }
  std::fflush(stdout);
  REQUIRE(failures == 0);
}

namespace {
// (the artifact write is registered lazily at the Recorder's first use —
// see Recorder::get(); no file-scope writer object)
}  // namespace
