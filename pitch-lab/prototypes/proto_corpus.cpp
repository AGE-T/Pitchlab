// Pitch Lab — Task 28 deterministic sound-design corpus implementation.
// See proto_corpus.h for the frozen contract. All synthesis in double;
// PCG64 consumer streams per material (seed 1, tag "task28.corpus.<id>").

#include "prototypes/proto_corpus.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/rng.h"

namespace pitchlab::proto {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Raised-cosine edge ramp (fade length in frames, applied at both ends).
void applyEdges(std::vector<double>& x, int64_t fadeFrames) {
  const int64_t n = static_cast<int64_t>(x.size());
  for (int64_t i = 0; i < fadeFrames && i < n; ++i) {
    const double g = 0.5 * (1.0 - std::cos(kPi * static_cast<double>(i) /
                                           static_cast<double>(fadeFrames)));
    x[static_cast<std::size_t>(i)] *= g;
    x[static_cast<std::size_t>(n - 1 - i)] *= g;
  }
}

void normalisePeak(std::vector<std::vector<double>>& ch, double targetPeak) {
  double peak = 0.0;
  for (const auto& c : ch) {
    for (double v : c) peak = std::max(peak, std::abs(v));
  }
  if (peak <= 0.0) return;
  const double g = targetPeak / peak;
  for (auto& c : ch) {
    for (double& v : c) v *= g;
  }
}

// Two-pole digital resonator (band-pass), peak-normalised by its own
// impulse-response maximum (deterministic, measured in double).
struct Resonator {
  double a1 = 0.0, a2 = 0.0, b0 = 0.0;
  double y1 = 0.0, y2 = 0.0;
  double x1 = 0.0;

  static Resonator make(double fs, double f0, double q) {
    Resonator r;
    const double bw = f0 / q;
    const double rr = std::exp(-kPi * bw / fs);
    r.a1 = 2.0 * rr * std::cos(2.0 * kPi * f0 / fs);
    r.a2 = -(rr * rr);
    // Impulse-response peak gain (measured over 8/(1-r) samples).
    double g = 1.0;
    {
      double y1 = 0.0, y2 = 0.0;
      double peak = 0.0;
      const int64_t n = static_cast<int64_t>(8.0 / (1.0 - rr));
      for (int64_t i = 0; i < n; ++i) {
        const double x = (i == 0) ? 1.0 : 0.0;
        const double y = x + r.a1 * y1 + r.a2 * y2;
        y2 = y1;
        y1 = y;
        peak = std::max(peak, std::abs(y));
      }
      if (peak > 1.0e-12) g = 1.0 / peak;
    }
    r.b0 = g;
    return r;
  }

  double process(double x) {
    const double y = b0 * (x - x1) + a1 * y1 + a2 * y2;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

Material makeSine() {
  Material m;
  m.id = "sine";
  m.description = "440 Hz pure sine, 1.0 s";
  m.tonal = true;
  m.carrierHz = 440.0;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  for (int64_t i = 0; i < 48000; ++i) {
    m.ch[0][static_cast<std::size_t>(i)] =
        std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 48000.0);
  }
  applyEdges(m.ch[0], 96);
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeHarmStack() {
  Material m;
  m.id = "harmstack";
  m.description = "220 Hz harmonic stack, harmonics 1..6 at 1/h, 1.0 s";
  m.tonal = true;
  m.carrierHz = 220.0;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  for (int64_t i = 0; i < 48000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = 0.0;
    for (int h = 1; h <= 6; ++h) {
      v += std::sin(2.0 * kPi * 220.0 * h * t) / static_cast<double>(h);
    }
    m.ch[0][static_cast<std::size_t>(i)] = v;
  }
  applyEdges(m.ch[0], 96);
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeMetallic() {
  Material m;
  m.id = "metallic";
  m.description = "inharmonic metallic strike (partial ratios 1/1.41/1.93/2.76/3.35 x 300 Hz), 1.2 s";
  m.tonal = true;
  m.carrierHz = 300.0;
  m.ch.assign(1, std::vector<double>(57600, 0.0));
  const double ratios[5] = {1.0, 1.41, 1.93, 2.76, 3.35};
  const double taus[5] = {0.40, 0.30, 0.24, 0.18, 0.15};
  const double amps[5] = {1.0, 0.8, 0.6, 0.45, 0.3};
  for (int64_t i = 0; i < 57600; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = 0.0;
    for (int p = 0; p < 5; ++p) {
      const double f = 300.0 * ratios[p];
      v += amps[p] * std::exp(-t / taus[p]) *
           std::sin(2.0 * kPi * f * t + 0.7 * static_cast<double>(p));
    }
    m.ch[0][static_cast<std::size_t>(i)] = v;
  }
  applyEdges(m.ch[0], 48);
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeImpulse() {
  Material m;
  m.id = "impulse";
  m.description = "two single-frame impulses at 0.25 s and 0.55 s in silence, 0.8 s";
  m.tonal = false;
  m.ch.assign(1, std::vector<double>(38400, 0.0));
  m.ch[0][12000] = 1.0;
  m.ch[0][26400] = 1.0;
  return m;
}

Material makeDrum() {
  Material m;
  m.id = "drum";
  m.description = "four kick-like hits (150->45 Hz sweep + noise burst), 2.0 s";
  m.tonal = false;
  m.ch.assign(1, std::vector<double>(96000, 0.0));
  Pcg64 rng = makeConsumerStream(1, "task28.corpus.drum");
  const double hitTimes[4] = {0.1, 0.6, 1.1, 1.6};
  const int64_t hitLen = 5760;  // 120 ms
  for (double hitT : hitTimes) {
    const int64_t start = static_cast<int64_t>(hitT * 48000.0);
    double phase = 0.0;
    for (int64_t j = 0; j < hitLen; ++j) {
      const double u = static_cast<double>(j) / 48000.0;
      const double f = 150.0 * std::pow(45.0 / 150.0, u / 0.08);
      phase += 2.0 * kPi * f / 48000.0;
      double v = std::sin(phase) * std::exp(-u / 0.045);
      if (j < 576) {
        v += 0.4 * (2.0 * rng.nextDouble01() - 1.0) * std::exp(-u / 0.012);
      }
      m.ch[0][static_cast<std::size_t>(start + j)] += v;
    }
  }
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makePluck() {
  Material m;
  m.id = "pluck";
  m.description = "decaying 220 Hz pluck (harmonics 1..12 at 1/h^1.5, 2 ms attack), 1.0 s";
  m.tonal = true;
  m.carrierHz = 220.0;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  for (int64_t i = 0; i < 48000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = 0.0;
    for (int h = 1; h <= 12; ++h) {
      const double a = 1.0 / std::pow(static_cast<double>(h), 1.5);
      v += a * std::sin(2.0 * kPi * 220.0 * h * t);
    }
    v *= std::exp(-t / 0.35);
    if (t < 0.002) {
      v *= t / 0.002;
    }
    m.ch[0][static_cast<std::size_t>(i)] = v;
  }
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeBass() {
  Material m;
  m.id = "bass";
  m.description = "110 Hz bass (harmonics 1..24 at 1/h^2, 10 ms attack), 1.0 s";
  m.tonal = true;
  m.carrierHz = 110.0;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  for (int64_t i = 0; i < 48000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = 0.0;
    for (int h = 1; h <= 24; ++h) {
      const double a = 1.0 / (static_cast<double>(h) * static_cast<double>(h));
      v += a * std::sin(2.0 * kPi * 110.0 * h * t);
    }
    double env = 1.0;
    if (t < 0.01) env = t / 0.01;
    if (t > 0.995) env = (1.0 - t) / 0.005;
    m.ch[0][static_cast<std::size_t>(i)] = v * env;
  }
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeDistorted() {
  Material m;
  m.id = "distorted";
  m.description = "hard-clipped 330 Hz sine (clip at +/-0.3), 1.0 s";
  m.tonal = true;
  m.carrierHz = 330.0;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  for (int64_t i = 0; i < 48000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = std::sin(2.0 * kPi * 330.0 * t);
    if (v > 0.3) v = 0.3;
    if (v < -0.3) v = -0.3;
    m.ch[0][static_cast<std::size_t>(i)] = v;
  }
  applyEdges(m.ch[0], 96);
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeNoise() {
  Material m;
  m.id = "noise";
  m.description = "full-band white noise (PCG64), 1.0 s";
  m.tonal = false;
  m.ch.assign(1, std::vector<double>(48000, 0.0));
  Pcg64 rng = makeConsumerStream(1, "task28.corpus.noise");
  for (auto& v : m.ch[0]) v = 2.0 * rng.nextDouble01() - 1.0;
  applyEdges(m.ch[0], 96);
  return m;
}

Material makeVocal() {
  Material m;
  m.id = "vocal";
  m.description = "140 Hz glottal pulse train through 700/1220/2600 Hz resonators, 1.2 s";
  m.tonal = true;
  m.carrierHz = 140.0;
  m.ch.assign(1, std::vector<double>(57600, 0.0));
  Resonator r1 = Resonator::make(48000.0, 700.0, 9.0);
  Resonator r2 = Resonator::make(48000.0, 1220.0, 11.0);
  Resonator r3 = Resonator::make(48000.0, 2600.0, 14.0);
  const double f0 = 140.0;
  const double period = 48000.0 / f0;
  const double duty = 0.25;
  for (int64_t i = 0; i < 57600; ++i) {
    const double ph = std::fmod(static_cast<double>(i), period) / period;
    double pulse = 0.0;
    if (ph < duty) {
      pulse = 0.5 * (1.0 - std::cos(2.0 * kPi * ph / duty));
    }
    const double y = r1.process(pulse) + 0.5 * r2.process(pulse) +
                     0.25 * r3.process(pulse);
    m.ch[0][static_cast<std::size_t>(i)] = y;
  }
  applyEdges(m.ch[0], 720);
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makePad() {
  Material m;
  m.id = "pad";
  m.description = "stereo detuned chord pad (A3/C#4/E4, +/-0.7% pairs, 300 ms attack), 1.5 s";
  m.tonal = true;
  m.carrierHz = 220.0;
  m.channels = 2;
  m.ch.assign(2, std::vector<double>(72000, 0.0));
  const double base[3] = {220.0, 277.182631, 329.627557};
  const double detune = 0.007;
  // Channel c uses the +/-detuned voice of each pair plus a slow amp LFO
  // (decorrelated phase per channel).
  for (int c = 0; c < 2; ++c) {
    for (int64_t i = 0; i < 72000; ++i) {
      const double t = static_cast<double>(i) / 48000.0;
      double v = 0.0;
      for (int d = 0; d < 3; ++d) {
        const double f = base[d] * (1.0 + (c == 0 ? detune : -detune));
        for (int h = 1; h <= 10; ++h) {
          v += std::sin(2.0 * kPi * f * h * t) / static_cast<double>(h);
        }
      }
      const double lfo =
          1.0 + 0.1 * std::sin(2.0 * kPi * 0.3 * t +
                               (c == 0 ? 0.0 : 1.9));
      double env = 1.0;
      if (t < 0.3) env = 0.5 * (1.0 - std::cos(kPi * t / 0.3));
      if (t > 1.45) env = (1.5 - t) / 0.05;
      m.ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
          v * lfo * env;
    }
  }
  normalisePeak(m.ch, 0.25);
  return m;
}

Material makeDense() {
  Material m;
  m.id = "dense";
  m.description = "dense polyphony (chord + 110 Hz bass + noise + 440/523/659 Hz pluck bursts), 1.5 s";
  m.tonal = false;
  m.ch.assign(1, std::vector<double>(72000, 0.0));
  Pcg64 rng = makeConsumerStream(1, "task28.corpus.dense");
  const double chord[3] = {220.0, 277.182631, 329.627557};
  const double pluckF[3] = {440.0, 523.251131, 659.255114};
  for (int64_t i = 0; i < 72000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    double v = 0.0;
    // Detuned chord (mono mix of 6 voices).
    for (int d = 0; d < 3; ++d) {
      for (int s = 0; s < 2; ++s) {
        const double f = chord[d] * (1.0 + (s == 0 ? 0.004 : -0.004));
        for (int h = 1; h <= 8; ++h) {
          v += std::sin(2.0 * kPi * f * h * t) /
               (static_cast<double>(h) * 6.0);
        }
      }
    }
    // Sustained bass.
    for (int h = 1; h <= 16; ++h) {
      v += std::sin(2.0 * kPi * 110.0 * h * t) /
           (static_cast<double>(h) * static_cast<double>(h) * 2.0);
    }
    // Noise bed.
    v += 0.08 * (2.0 * rng.nextDouble01() - 1.0);
    // Pluck bursts every 0.25 s (cycling pitches, 2 ms attack, 0.15 s decay).
    for (int k = 0; k < 6; ++k) {
      const double t0 = 0.1 + 0.25 * static_cast<double>(k);
      const double dt = t - t0;
      if (dt >= 0.0 && dt < 0.15) {
        const double f = pluckF[k % 3];
        double pv = 0.0;
        for (int h = 1; h <= 8; ++h) {
          pv += std::sin(2.0 * kPi * f * h * dt) /
                std::pow(static_cast<double>(h), 1.5);
        }
        pv *= std::exp(-dt / 0.05);
        if (dt < 0.002) pv *= dt / 0.002;
        v += 0.5 * pv;
      }
    }
    double env = 1.0;
    if (t < 0.02) env = t / 0.02;
    if (t > 1.48) env = (1.5 - t) / 0.02;
    m.ch[0][static_cast<std::size_t>(i)] = v * env;
  }
  normalisePeak(m.ch, 0.25);
  return m;
}

}  // namespace

const std::vector<Material>& corpusMaterials() {
  static const std::vector<Material> mats = [] {
    std::vector<Material> v;
    v.push_back(makeSine());
    v.push_back(makeHarmStack());
    v.push_back(makeMetallic());
    v.push_back(makeImpulse());
    v.push_back(makeDrum());
    v.push_back(makePluck());
    v.push_back(makeBass());
    v.push_back(makeDistorted());
    v.push_back(makeNoise());
    v.push_back(makeVocal());
    v.push_back(makePad());
    v.push_back(makeDense());
    return v;
  }();
  return mats;
}

const Material* findMaterial(const std::string& id) {
  for (const Material& m : corpusMaterials()) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

}  // namespace pitchlab::proto
