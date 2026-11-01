// ============================================================================
// OCTAVOX round-19 click scanner (r19_clickscan) -- offline diagnostics CLI.
// ============================================================================
// PROVENANCE: our own round-19 RE evidence tooling (task R19-P2a), written
// from scratch against the PUBLIC engine API in include/octavox/dsp.hpp and
// the harness WAV writer in tests/wav_io.hpp. No target code, target data or
// target disassembly is reproduced here -- the observed static kernel is only
// referenced indirectly through octavox::Interpolation::ObsKernel6 inside the
// prebuilt engine library.
//
// PURPOSE: an OFFLINE signal-continuity / click scanner for the R19
// real-time round. It renders deterministic mono test signals through the
// engine in a block-driven loop, measures sample-to-sample discontinuities
// (d1 = |y[k]-y[k-1]|, d2 = |y[k]-2y[k-1]+y[k-2]|), thresholds them
// adaptively against a local RMS, and correlates the surviving click events
// with (a) host BLOCK boundaries, (b) grain-wrap stream timestamps
// (VoiceStats::last_wrap_at) and (c) tap-spacing-commit timestamps
// (VoiceStats::last_commit_at). Output: machine-readable JSON report +
// human stdout summary table + optional 16-bit PCM WAV dumps.
//
// SWEEP: scenario x interpolation {Lagrange6, ObsKernel6} x sample rate
// {44100, 48000, 96000} x block {64, 128, 512} (288 renders at 2 s each).
// --legacy adds a comparison run of every selected scenario x interp at
// rate=48000 / block=128 with the LEGACY config pins (detector_mode=FullRate,
// commit_crossfade_seconds=0, delay_crossfade_seconds=0,
// voice_fade_seconds=0) so legacy click magnitudes sit side-by-side with the
// modern ones in the same report.
//
// CLI: r19_clickscan [--out <dir>] [--json <path>] [--wav] [--legacy]
//                    [--scenario <name>] [--rate <hz>] [--block <n>]
//                    [--interp lagrange6|obskernel6] [--list]
//
// DETERMINISM: every generator below is a pure closed-form formula (no RNG
// needed for this scenario set; the harness LCG spec is therefore unused).
// The engine is driven block-by-block from a precomputed signal so the
// stream is identical for every block size.
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#endif

#include "octavox/dsp.hpp"
#include "wav_io.hpp"

namespace {

// ---------------------------------------------------------------------------
// Spec constants
// ---------------------------------------------------------------------------
constexpr double kSeconds = 2.0;            // audio length of every render
constexpr int    kRmsWindow = 256;          // centered local-RMS window
constexpr double kAdaptiveRatio = 8.0;      // step must exceed 8x local RMS
constexpr double kAbsFloor = 1e-4;          // absolute d1 floor
constexpr long   kCorrelationRadius = 4096; // +/- search window for wrap/commit
constexpr long   kNearWrapCommit = 64;      // "near" event radius (samples)
constexpr long   kPeakRadius = 64;          // +/- window for peak-near-events
constexpr int    kMaxVoices = 8;            // Config default max_voices
constexpr double kTwoPi = 6.283185307179586476925286766559;

/// cents -> pitch ratio.
double cents(double c) { return std::pow(2.0, c / 1200.0); }

double nowSec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Deterministic signal generators (pure formulas, no RNG in this tool)
// ---------------------------------------------------------------------------
enum class Gen {
    Sine,             // plain tone
    SilenceThenSine,  // 0.5 s silence, then 1.5 s tone (phase starts at 0)
    SineThenSilence,  // 1.5 s tone, then 0.5 s silence
    Chirp,            // exponential chirp, tests/test_main.cpp gen::chirp law
    Chord,            // 110+220+330 Hz, amp each
    Bursts,           // exp decay tau=200 every 0.25 s, first sample = 1.0
    FmSine,           // 220 Hz with 5 Hz +/-3% FM (phase-integrated)
};

/// One scheduled parameter action, issued at the first BLOCK boundary at or
/// after t_s seconds (deterministic for a given block size).
struct Action {
    enum Kind { SetVoices, SetBypass, SetWetDry, SetRatio, SetVoiceBaseDelay };
    double t_s;
    Kind kind;
    int i = 0;
    double d = 0.0;
    bool b = false;
};

/// A scenario: generator + engine config mutations + action schedule.
struct Scenario {
    const char* name = "";
    Gen gen = Gen::Sine;
    double f0 = 0.0, f1 = 0.0;   // generator params (meaning depends on Gen)
    double amp = 0.5;            // generator amplitude
    double ratio = 1.0;          // cfg.ratio
    int voices = 1;              // cfg.voices   (Config default = 1)
    bool spread = true;          // cfg.spread   (Config default = true)
    std::vector<Action> actions; // chronological
};

/// The 16-scenario table (R19-P2a spec). Everything not listed stays at the
/// Config{} 16.0.0 defaults (Decimated detector, 8 ms commit crossfade,
/// 12 ms delay crossfade, 12 ms voice fade, ...).
std::vector<Scenario> buildScenarios() {
    std::vector<Scenario> v;
    auto add = [&v](Scenario s) { v.push_back(std::move(s)); };

    {   // 1. cold signal onset after silence
        Scenario s{};
        s.name = "silence_to_signal";
        s.gen = Gen::SilenceThenSine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(-400.0);
        add(s);
    }
    {   // 2. signal tail into silence
        Scenario s{};
        s.name = "signal_to_silence";
        s.gen = Gen::SineThenSilence; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        add(s);
    }
    {   // 3. steady-state tone (the canonical wrap/commit exerciser)
        Scenario s{};
        s.name = "sustained_sine_220";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(-400.0);
        add(s);
    }
    {   // 4. exponential chirp (tracking stress)
        Scenario s{};
        s.name = "swept_sine_200_800";
        s.gen = Gen::Chirp; s.f0 = 200.0; s.f1 = 800.0; s.amp = 0.5;
        s.ratio = cents(-400.0);
        add(s);
    }
    {   // 5. three-tone chord, 0.25 amp each
        Scenario s{};
        s.name = "chord_a2c3e3";
        s.gen = Gen::Chord; s.amp = 0.25;
        s.ratio = cents(-400.0);
        add(s);
    }
    {   // 6. decaying transient bursts every 0.25 s
        Scenario s{};
        s.name = "transient_bursts";
        s.gen = Gen::Bursts; s.amp = 1.0;
        s.ratio = cents(400.0);
        add(s);
    }
    {   // 7. +4 semitones
        Scenario s{};
        s.name = "shift_pos_400c";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        add(s);
    }
    {   // 8. -24 semitones (the extreme case)
        Scenario s{};
        s.name = "shift_neg_2400c";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(-2400.0);
        add(s);
    }
    {   // 9. exact unity (parked path -- trivially click-free)
        Scenario s{};
        s.name = "unity_exact";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = 1.0;
        add(s);
    }
    {   // 10. +1 cent (near-unity glide stress)
        Scenario s{};
        s.name = "near_unity_plus1c";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(1.0);
        add(s);
    }
    {   // 11. 1 -> 8 voices at t = 1.0 s
        Scenario s{};
        s.name = "voice_count_changes";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        s.voices = 1;
        s.spread = false;
        s.actions.push_back(Action{1.0, Action::SetVoices, 8, 0.0, false});
        add(s);
    }
    {   // 12. bypass on at 0.5 s, off at 1.5 s
        Scenario s{};
        s.name = "bypass_transitions";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        s.actions.push_back(Action{0.5, Action::SetBypass, 0, 0.0, true});
        s.actions.push_back(Action{1.5, Action::SetBypass, 0, 0.0, false});
        add(s);
    }
    {   // 13. wet -> dry at 0.5 s, dry -> wet at 1.5 s
        Scenario s{};
        s.name = "wetdry_transitions";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        s.actions.push_back(Action{0.5, Action::SetWetDry, 0, 0.0, false});
        s.actions.push_back(Action{1.5, Action::SetWetDry, 0, 1.0, false});
        add(s);
    }
    {   // 14. live ratio change +4st -> -12st at t = 1.0 s
        Scenario s{};
        s.name = "param_change_pitch";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        s.actions.push_back(Action{1.0, Action::SetRatio, 0, cents(-1200.0), false});
        add(s);
    }
    {   // 15. per-voice base delay jump at t = 1.0 s
        Scenario s{};
        s.name = "basedelay_change";
        s.gen = Gen::Sine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(400.0);
        s.actions.push_back(Action{1.0, Action::SetVoiceBaseDelay, 0, 5000.0, false});
        add(s);
    }
    {   // 16. vibrato (repeated spacing retunes)
        Scenario s{};
        s.name = "vibrato_spacing_retune";
        s.gen = Gen::FmSine; s.f0 = 220.0; s.amp = 0.5;
        s.ratio = cents(-400.0);
        add(s);
    }
    return v;
}

/// Render the full deterministic input signal for one scenario.
std::vector<float> makeSignal(const Scenario& sc, int sr, long n) {
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    const double T = static_cast<double>(n) / static_cast<double>(sr);
    switch (sc.gen) {
    case Gen::Sine:
        for (long i = 0; i < n; ++i)
            x[static_cast<size_t>(i)] = static_cast<float>(
                sc.amp * std::sin(kTwoPi * sc.f0 * static_cast<double>(i) / sr));
        break;
    case Gen::SilenceThenSine: {
        const long split = static_cast<long>(std::llround(0.5 * sr));
        for (long i = split; i < n; ++i)
            x[static_cast<size_t>(i)] = static_cast<float>(
                sc.amp * std::sin(kTwoPi * sc.f0 *
                                  static_cast<double>(i - split) / sr));
        break;
    }
    case Gen::SineThenSilence: {
        const long end = static_cast<long>(std::llround(1.5 * sr));
        for (long i = 0; i < std::min(end, n); ++i)
            x[static_cast<size_t>(i)] = static_cast<float>(
                sc.amp * std::sin(kTwoPi * sc.f0 * static_cast<double>(i) / sr));
        break;
    }
    case Gen::Chirp:
        // Verbatim tests/test_main.cpp gen::chirp law:
        //   f(t) = f0 * (f1/f0)^(t/T);  x(t) = amp * sin(2*pi*f(t)*t)
        for (long i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(sr);
            const double f = sc.f0 * std::pow(sc.f1 / sc.f0, t / T);
            x[static_cast<size_t>(i)] =
                static_cast<float>(sc.amp * std::sin(kTwoPi * f * t));
        }
        break;
    case Gen::Chord:
        // (110 + 220 + 330) Hz at sc.amp each (spec: 0.25).
        for (long i = 0; i < n; ++i) {
            double s = 0.0;
            for (const double f : {110.0, 220.0, 330.0})
                s += std::sin(kTwoPi * f * static_cast<double>(i) / sr);
            x[static_cast<size_t>(i)] = static_cast<float>(sc.amp * s);
        }
        break;
    case Gen::Bursts: {
        // Decaying burst every 0.25 s: at each period start ONE sample of
        // amplitude 1.0 followed by exp decay tau = 200 samples to zero.
        // The closed form exp(-t/200) covers the first sample (= 1.0).
        const long per = static_cast<long>(std::llround(0.25 * sr));
        for (long i = 0; i < n; ++i) {
            const long t = i % per;
            x[static_cast<size_t>(i)] =
                static_cast<float>(std::exp(-static_cast<double>(t) / 200.0));
        }
        break;
    }
    case Gen::FmSine: {
        // f(t) = f0 * (1 + 0.03 * sin(2*pi*5*t)); phase-integrated.
        double ph = 0.0;
        for (long i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(sr);
            const double f =
                sc.f0 * (1.0 + 0.03 * std::sin(kTwoPi * 5.0 * t));
            ph += kTwoPi * f / static_cast<double>(sr);
            x[static_cast<size_t>(i)] =
                static_cast<float>(sc.amp * std::sin(ph));
        }
        break;
    }
    }
    return x;
}

// ---------------------------------------------------------------------------
// Click analysis data model
// ---------------------------------------------------------------------------
struct ClickEvent {
    long k = 0;                  // stream sample index of the step
    double t_s = 0.0;            // k / sr
    double d1 = 0.0;             // |y[k] - y[k-1]|
    double d2 = 0.0;             // |y[k] - 2y[k-1] + y[k-2]|
    double y_prev = 0.0;         // y[k-1]
    double y = 0.0;              // y[k]
    double rms_local = 0.0;      // max(r[k], r[k-1]) at the event
    bool block_boundary = false; // k % block == 0
    long dist_to_boundary = -1;  // distance to nearest block boundary
    long dist_to_wrap = -1;      // |k - nearest last_wrap_at|, else -1
    long dist_to_commit = -1;    // |k - nearest last_commit_at|, else -1
};

struct RenderSummary {
    long n_events = 0;
    double max_d1_overall = 0.0;
    double max_d1_excluding_events = 0.0;
    double max_d2 = 0.0;
    double peak_amplitude_near_events = 0.0; // max |y| within +/-64 of events
    long events_at_block_boundaries = 0;
    long events_within_64_of_wrap = 0;
    long events_within_64_of_commit = 0;
    long total_wraps = 0;   // summed over voices 0..max_voices-1
    long total_commits = 0; // (inactive voices contribute 0)
    long analysis_start = 0;
};

struct RenderRecord {
    std::string scenario;
    std::string interp;   // "lagrange6" | "obskernel6"
    int sr = 48000;
    int block = 128;
    bool legacy = false;
    double seconds = kSeconds;
    RenderSummary summary;
    std::vector<ClickEvent> events;
};

const char* interpName(octavox::Interpolation ip) {
    switch (ip) {
    case octavox::Interpolation::Linear: return "linear";
    case octavox::Interpolation::Lagrange6: return "lagrange6";
    case octavox::Interpolation::Lagrange6Q: return "lagrange6_q";
    case octavox::Interpolation::ObsKernel6: return "obskernel6";
    }
    return "?";
}

/// Nearest |k - ts| over a SORTED timestamp list, bounded to
/// kCorrelationRadius; -1 when the list is empty or nothing is in range.
long nearestDistanceBounded(long k, const std::vector<long>& ts) {
    if (ts.empty()) return -1;
    std::vector<long>::const_iterator it =
        std::lower_bound(ts.begin(), ts.end(), k);
    long best = -1;
    if (it != ts.end()) best = static_cast<long>(*it - k);
    if (it != ts.begin()) {
        const long d = static_cast<long>(k - *(it - 1));
        if (best < 0 || d < best) best = d;
    }
    return (best >= 0 && best <= kCorrelationRadius) ? best : -1;
}

void applyAction(octavox::AlignedDelayLine& eng, const Action& a) {
    switch (a.kind) {
    case Action::SetVoices: eng.setVoices(a.i); break;
    case Action::SetBypass: eng.setBypass(a.b); break;
    case Action::SetWetDry: eng.setWetDry(a.d); break;
    case Action::SetRatio: eng.setRatio(a.d); break;
    case Action::SetVoiceBaseDelay: eng.setVoiceBaseDelay(a.i, a.d); break;
    }
}

// ---------------------------------------------------------------------------
// One full render: engine drive + wrap/commit timestamp collection + click
// analysis (+ optional WAV dump). Returns the RenderRecord for report/table.
// ---------------------------------------------------------------------------
RenderRecord runRender(const Scenario& sc, int sr, int block,
                       octavox::Interpolation interp, bool legacy,
                       bool wantWav, const std::string& wavPath) {
    RenderRecord rec;
    rec.scenario = sc.name;
    rec.interp = interpName(interp);
    rec.sr = sr;
    rec.block = block;
    rec.legacy = legacy;
    rec.seconds = kSeconds;

    const long n = static_cast<long>(std::llround(kSeconds * sr));
    const std::vector<float> in = makeSignal(sc, sr, n);
    std::vector<float> y(static_cast<size_t>(n), 0.0f);

    // -- engine config: Config{} 16.0.0 defaults + scenario mutations ------
    octavox::Config cfg{};
    cfg.sample_rate = static_cast<double>(sr);
    cfg.interpolation = interp;
    cfg.ratio = sc.ratio;
    cfg.voices = sc.voices;
    cfg.spread = sc.spread;
    if (legacy) {
        // LEGACY pins: bit-exact 15.0.0 behaviour (instant commits, instant
        // base-delay re-align, instant voice activation).
        cfg.detector_mode = octavox::DetectorMode::FullRate;
        cfg.commit_crossfade_seconds = 0.0;
        cfg.delay_crossfade_seconds = 0.0;
        cfg.voice_fade_seconds = 0.0;
    }

    octavox::AlignedDelayLine eng;
    if (!eng.prepare(cfg))
        throw std::runtime_error(std::string("engine prepare() failed (") +
                                 sc.name + ")");

    // -- block-driven render loop ------------------------------------------
    // Wrap/commit timestamps: after every process() block, snapshot every
    // voice's VoiceStats BY VALUE (voiceStats() returns thread-local scratch)
    // and append last_wrap_at / last_commit_at whenever the monotone counter
    // increased -- sample-accurate event times for the correlation below.
    std::vector<long> wrapTimes, commitTimes;
    long totalWraps = 0, totalCommits = 0;
    octavox::VoiceStats prev[kMaxVoices]; // zero-init (wraps=0, commits=0)
    size_t nextAction = 0;
    for (long start = 0; start < n; start += block) {
        const int nb = static_cast<int>(
            std::min<long>(static_cast<long>(block), n - start));
        // Scheduled actions are issued at the first block boundary at or
        // after their target time (before that block is processed).
        while (nextAction < sc.actions.size() &&
               start >=
                   static_cast<long>(std::llround(
                       sc.actions[nextAction].t_s * static_cast<double>(sr)))) {
            applyAction(eng, sc.actions[nextAction]);
            ++nextAction;
        }
        eng.process(in.data() + start, y.data() + start, nb);
        for (int v = 0; v < kMaxVoices; ++v) {
            const octavox::VoiceStats st = eng.voiceStats(v); // COPY (scratch)
            if (st.wraps > prev[v].wraps) {
                totalWraps += st.wraps - prev[v].wraps;
                if (st.last_wrap_at >= 0) // -1 = none yet
                    wrapTimes.push_back(st.last_wrap_at);
            }
            if (st.commits > prev[v].commits) {
                totalCommits += st.commits - prev[v].commits;
                if (st.last_commit_at >= 0) // -1 = none yet
                    commitTimes.push_back(st.last_commit_at);
            }
            prev[v] = st;
        }
    }
    std::sort(wrapTimes.begin(), wrapTimes.end());
    std::sort(commitTimes.begin(), commitTimes.end());

    rec.summary.total_wraps = totalWraps;
    rec.summary.total_commits = totalCommits;

    // -- click analysis ----------------------------------------------------
    // Skip the cold-start region: engine latency + 4096 samples.
    const long latency = static_cast<long>(eng.latencySamples());
    const long analysisStart = std::min(n, latency + 4096);
    rec.summary.analysis_start = analysisStart;

    // Cumulative energy -> O(n) centered RMS via prefix sums.
    std::vector<double> csum(static_cast<size_t>(n) + 1, 0.0);
    for (long i = 0; i < n; ++i) {
        const double s = static_cast<double>(y[static_cast<size_t>(i)]);
        csum[static_cast<size_t>(i) + 1] = csum[static_cast<size_t>(i)] + s * s;
    }
    const long half = kRmsWindow / 2;
    auto rmsAt = [&](long k) -> double {
        const long lo = std::max(0L, k - half);
        const long hi = std::min(n, k + half);
        const long cnt = hi - lo;
        if (cnt <= 0) return 0.0;
        return std::sqrt((csum[static_cast<size_t>(hi)] -
                          csum[static_cast<size_t>(lo)]) /
                         static_cast<double>(cnt));
    };

    std::vector<ClickEvent>& events = rec.events;
    double maxD1 = 0.0, maxD1NoEv = 0.0, maxD2 = 0.0;
    for (long k = analysisStart; k < n; ++k) {
        const double y0 = static_cast<double>(y[static_cast<size_t>(k)]);
        const double y1 = static_cast<double>(y[static_cast<size_t>(k) - 1]);
        const double y2 = static_cast<double>(y[static_cast<size_t>(k) - 2]);
        const double d1 = std::fabs(y0 - y1);
        const double d2 = std::fabs(y0 - 2.0 * y1 + y2);
        if (d1 > maxD1) maxD1 = d1;
        if (d2 > maxD2) maxD2 = d2;

        const double rLocal = std::max(rmsAt(k), rmsAt(k - 1));
        const double threshold = std::max(kAbsFloor, kAdaptiveRatio * rLocal);
        if (d1 > threshold) {
            ClickEvent ev;
            ev.k = k;
            ev.t_s = static_cast<double>(k) / static_cast<double>(sr);
            ev.d1 = d1;
            ev.d2 = d2;
            ev.y_prev = y1;
            ev.y = y0;
            ev.rms_local = rLocal;
            ev.block_boundary = (k % static_cast<long>(block) == 0);
            const long rb = k % static_cast<long>(block);
            ev.dist_to_boundary = std::min(rb, static_cast<long>(block) - rb);
            ev.dist_to_wrap = nearestDistanceBounded(k, wrapTimes);
            ev.dist_to_commit = nearestDistanceBounded(k, commitTimes);
            events.push_back(ev);
        } else if (d1 > maxD1NoEv) {
            maxD1NoEv = d1; // largest step that did NOT qualify (context)
        }
    }
    rec.summary.n_events = static_cast<long>(events.size());
    rec.summary.max_d1_overall = maxD1;
    rec.summary.max_d1_excluding_events = maxD1NoEv;
    rec.summary.max_d2 = maxD2;

    // Peak amplitude near events (max |y| within +/-64 samples of an event;
    // 0.0 when there are no events).
    double peakNear = 0.0;
    for (const ClickEvent& ev : events) {
        const long lo = std::max(0L, ev.k - kPeakRadius);
        const long hi = std::min(n - 1, ev.k + kPeakRadius);
        for (long j = lo; j <= hi; ++j)
            peakNear = std::max(
                peakNear,
                std::fabs(static_cast<double>(y[static_cast<size_t>(j)])));
    }
    rec.summary.peak_amplitude_near_events = peakNear;

    for (const ClickEvent& ev : events) {
        if (ev.block_boundary) ++rec.summary.events_at_block_boundaries;
        if (ev.dist_to_wrap >= 0 && ev.dist_to_wrap <= kNearWrapCommit)
            ++rec.summary.events_within_64_of_wrap;
        if (ev.dist_to_commit >= 0 && ev.dist_to_commit <= kNearWrapCommit)
            ++rec.summary.events_within_64_of_commit;
    }

    // -- optional 16-bit PCM dump ------------------------------------------
    if (wantWav)
        octavox::wav::writeWav(wavPath, y.data(), static_cast<int>(n), 1, sr);

    return rec;
}

// ---------------------------------------------------------------------------
// Environment info for the JSON report
// ---------------------------------------------------------------------------
struct EnvInfo {
    std::string os = "unknown";
    std::string cpu = "unknown";
    unsigned cores = 0;
};

EnvInfo queryEnv() {
    EnvInfo e;
    e.cores = std::thread::hardware_concurrency();
#if !defined(_WIN32)
    struct utsname u;
    if (::uname(&u) == 0)
        e.os = std::string(u.sysname) + " " + u.release + " " + u.machine;
#endif
#if defined(__linux__)
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("model name", 0) == 0) {
            const size_t c = line.find(':');
            if (c != std::string::npos) {
                std::string m = line.substr(c + 1);
                const size_t a = m.find_first_not_of(" \t");
                const size_t b = m.find_last_not_of(" \t\r\n");
                if (a != std::string::npos && b != std::string::npos &&
                    b >= a)
                    m = m.substr(a, b - a + 1);
                else
                    m.clear();
                if (!m.empty()) e.cpu = m;
            }
            break;
        }
    }
#endif
    return e;
}

// ---------------------------------------------------------------------------
// JSON emission (hand-rolled, no deps; numbers via %.9g)
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                o += buf;
            } else {
                o += c;
            }
        }
    }
    return o;
}

/// Finite double -> JSON number text; non-finite -> null (flags a bug).
std::string num(double x) {
    if (!std::isfinite(x)) return "null";
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", x);
    return std::string(buf);
}

void writeRenderArray(std::FILE* f, const char* key,
                      const std::vector<RenderRecord>& rs, bool lastKey) {
    std::fprintf(f, "  \"%s\": [", key);
    if (rs.empty()) {
        std::fprintf(f, "]%s\n", lastKey ? "" : ",");
        return;
    }
    std::fprintf(f, "\n");
    for (size_t i = 0; i < rs.size(); ++i) {
        const RenderRecord& r = rs[i];
        std::fprintf(f, "    {\n");
        std::fprintf(f, "      \"scenario\": \"%s\",\n",
                     jsonEscape(r.scenario).c_str());
        std::fprintf(f, "      \"interp\": \"%s\",\n",
                     jsonEscape(r.interp).c_str());
        std::fprintf(f, "      \"sample_rate\": %d,\n", r.sr);
        std::fprintf(f, "      \"block\": %d,\n", r.block);
        std::fprintf(f, "      \"legacy\": %s,\n", r.legacy ? "true" : "false");
        std::fprintf(f, "      \"seconds\": %s,\n", num(r.seconds).c_str());
        std::fprintf(f, "      \"summary\": {\n");
        std::fprintf(f, "        \"n_events\": %ld,\n", r.summary.n_events);
        std::fprintf(f, "        \"max_d1_overall\": %s,\n",
                     num(r.summary.max_d1_overall).c_str());
        std::fprintf(f, "        \"max_d1_excluding_events\": %s,\n",
                     num(r.summary.max_d1_excluding_events).c_str());
        std::fprintf(f, "        \"max_d2\": %s,\n", num(r.summary.max_d2).c_str());
        std::fprintf(f, "        \"peak_amplitude_near_events\": %s,\n",
                     num(r.summary.peak_amplitude_near_events).c_str());
        std::fprintf(f, "        \"events_at_block_boundaries\": %ld,\n",
                     r.summary.events_at_block_boundaries);
        std::fprintf(f, "        \"events_within_64_of_wrap\": %ld,\n",
                     r.summary.events_within_64_of_wrap);
        std::fprintf(f, "        \"events_within_64_of_commit\": %ld,\n",
                     r.summary.events_within_64_of_commit);
        std::fprintf(f, "        \"total_wraps\": %ld,\n", r.summary.total_wraps);
        std::fprintf(f, "        \"total_commits\": %ld,\n",
                     r.summary.total_commits);
        std::fprintf(f, "        \"analysis_start\": %ld\n",
                     r.summary.analysis_start);
        std::fprintf(f, "      },\n");
        if (r.events.empty()) {
            std::fprintf(f, "      \"events\": []\n");
        } else {
            std::fprintf(f, "      \"events\": [\n");
            for (size_t j = 0; j < r.events.size(); ++j) {
                const ClickEvent& e = r.events[j];
                std::fprintf(
                    f,
                    "        {\"k\": %ld, \"t_s\": %s, \"d1\": %s, \"d2\": %s, "
                    "\"y_prev\": %s, \"y\": %s, \"rms_local\": %s, "
                    "\"block_boundary\": %s, \"dist_to_boundary\": %ld, "
                    "\"dist_to_wrap\": %ld, \"dist_to_commit\": %ld}%s\n",
                    e.k, num(e.t_s).c_str(), num(e.d1).c_str(), num(e.d2).c_str(),
                    num(e.y_prev).c_str(), num(e.y).c_str(),
                    num(e.rms_local).c_str(), e.block_boundary ? "true" : "false",
                    e.dist_to_boundary, e.dist_to_wrap, e.dist_to_commit,
                    (j + 1 < r.events.size()) ? "," : "");
            }
            std::fprintf(f, "      ]\n");
        }
        std::fprintf(f, "    }%s\n", (i + 1 < rs.size()) ? "," : "");
    }
    std::fprintf(f, "  ]%s\n", lastKey ? "" : ",");
}

void writeJson(const std::string& path, const EnvInfo& env,
               const std::vector<RenderRecord>& renders,
               const std::vector<RenderRecord>& legacyRenders) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
        throw std::runtime_error("cannot write JSON report to " + path);
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"tool\": \"octavox_r19_clickscan\",\n");
    std::fprintf(f, "  \"version\": \"%s\",\n",
                 jsonEscape(octavox::versionString()).c_str());
    std::fprintf(f, "  \"environment\": {\"os\": \"%s\", \"cpu\": \"%s\", "
                    "\"cores\": %u},\n",
                 jsonEscape(env.os).c_str(), jsonEscape(env.cpu).c_str(),
                 env.cores);
    std::fprintf(f, "  \"params\": {\"adaptive_ratio\": %s, \"abs_floor\": %s, "
                    "\"rms_window\": %d},\n",
                 num(kAdaptiveRatio).c_str(), num(kAbsFloor).c_str(),
                 kRmsWindow);
    writeRenderArray(f, "renders", renders, false);
    writeRenderArray(f, "legacy_renders", legacyRenders, true);
    std::fprintf(f, "}\n");
    std::fclose(f);
}

// ---------------------------------------------------------------------------
// Human stdout table
// ---------------------------------------------------------------------------
void printTableHeader() {
    std::printf("%-23s %-11s %6s %6s %7s %11s %11s %7s %8s  %s\n",
                "scenario", "interp", "sr", "block", "events", "max_d1",
                "max_d2", "wraps", "commits", "verdict");
    std::printf("%-23s %-11s %6s %6s %7s %11s %11s %7s %8s  %s\n",
                "-----------------------", "-----------", "------", "------",
                "-------", "-----------", "-----------", "-------", "--------",
                "------");
}

void printRenderLine(const RenderRecord& r) {
    std::printf("%-23s %-11s %6d %6d %7ld %11.4g %11.4g %7ld %8ld  %s\n",
                r.scenario.c_str(), r.interp.c_str(), r.sr, r.block,
                r.summary.n_events, r.summary.max_d1_overall,
                r.summary.max_d2, r.summary.total_wraps,
                r.summary.total_commits,
                r.summary.n_events == 0 ? "CLEAN" : "CLICKS");
}

struct Totals {
    long renders = 0, clean = 0, clicks = 0;
};

Totals totalsOf(const std::vector<RenderRecord>& rs) {
    Totals t;
    for (const RenderRecord& r : rs) {
        ++t.renders;
        if (r.summary.n_events == 0) ++t.clean;
        else ++t.clicks;
    }
    return t;
}

void printBreakdown(const std::vector<RenderRecord>& rs) {
    // Per-scenario breakdown in first-seen order.
    std::vector<std::string> names;
    for (const RenderRecord& r : rs)
        if (std::find(names.begin(), names.end(), r.scenario) == names.end())
            names.push_back(r.scenario);
    for (const std::string& nm : names) {
        long total = 0, clean = 0;
        for (const RenderRecord& r : rs) {
            if (r.scenario != nm) continue;
            ++total;
            if (r.summary.n_events == 0) ++clean;
        }
        std::printf("  %-23s %4ld render(s)  %4ld clean  %4ld clicks\n",
                    nm.c_str(), total, clean, total - clean);
    }
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
struct Cli {
    std::string outDir = "click_reports";
    std::string jsonPath; // resolved to <out>/clickscan.json when empty
    bool wav = false;
    bool legacy = false;
    bool list = false;
    std::vector<std::string> scenarios; // repeatable filter
    int rate = 0;                       // 0 = all {44100,48000,96000}
    int block = 0;                      // 0 = all {64,128,512}
    std::string interp;                 // "" = both lagrange6+obskernel6
};

void usage(std::FILE* f) {
    std::fprintf(f,
        "usage: r19_clickscan [--out <dir>] [--json <path>] [--wav] [--legacy]\n"
        "                    [--scenario <name>] [--rate <hz>] [--block <n>]\n"
        "                    [--interp lagrange6|obskernel6] [--list]\n"
        "\n"
        "  --out       output directory (default click_reports)\n"
        "  --json      JSON report path (default <out>/clickscan.json)\n"
        "  --wav       also write 16-bit PCM dumps <out>/<scenario>_<interp>_<sr>_<block>.wav\n"
        "              (legacy renders get a _legacy suffix)\n"
        "  --legacy    add comparison renders at 48000 Hz / block 128 with the\n"
        "              legacy pins (FullRate detector, zero crossfades)\n"
        "  --scenario  select one scenario (repeatable)\n"
        "  --rate      select one sample rate (default 44100/48000/96000)\n"
        "  --block     select one block size (default 64/128/512)\n"
        "  --interp    select one interpolation (default both)\n"
        "  --list      print the scenario table and exit\n");
}

Cli parseCli(int argc, char** argv) {
    Cli cli;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        if (a == "--out") cli.outDir = next();
        else if (a == "--json") cli.jsonPath = next();
        else if (a == "--wav") cli.wav = true;
        else if (a == "--legacy") cli.legacy = true;
        else if (a == "--list") cli.list = true;
        else if (a == "--scenario") cli.scenarios.push_back(next());
        else if (a == "--rate") {
            cli.rate = std::atoi(next().c_str());
            if (cli.rate <= 0)
                throw std::runtime_error("--rate expects a positive integer");
        } else if (a == "--block") {
            cli.block = std::atoi(next().c_str());
            if (cli.block <= 0)
                throw std::runtime_error("--block expects a positive integer");
        } else if (a == "--interp") {
            cli.interp = next();
            if (cli.interp != "lagrange6" && cli.interp != "obskernel6")
                throw std::runtime_error(
                    "--interp expects lagrange6 or obskernel6");
        } else if (a == "--help" || a == "-h") {
            usage(stdout);
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown flag: %s\n\n", a.c_str());
            usage(stderr);
            std::exit(2);
        }
    }
    if (cli.jsonPath.empty()) cli.jsonPath = cli.outDir + "/clickscan.json";
    return cli;
}

/// mkdir -p equivalent (POSIX / Win32 best effort), throws on real failure.
void ensureDir(const std::string& dir) {
    if (dir.empty()) return;
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' || i + 1 == dir.size()) {
            std::string d = (dir[i] == '/') ? cur.substr(0, cur.size() - 1) : cur;
            if (d.empty() || d == "." || d == "/") continue;
#if defined(_WIN32)
            _mkdir(d.c_str());
#else
            ::mkdir(d.c_str(), 0755); // EEXIST (or failure) is tolerated here
#endif
        }
    }
#if !defined(_WIN32)
    struct stat st;
    if (::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        throw std::runtime_error("cannot create output directory: " + dir);
#endif
}

std::string wavPathFor(const std::string& dir, const Scenario& sc,
                       const std::string& interp, int sr, int block,
                       bool legacy) {
    return dir + "/" + sc.name + "_" + interp + "_" + std::to_string(sr) + "_" +
           std::to_string(block) + (legacy ? "_legacy" : "") + ".wav";
}

// ---------------------------------------------------------------------------
// Tool entry point (main only wraps this in try/catch)
// ---------------------------------------------------------------------------
int runTool(int argc, char** argv) {
    const Cli cli = parseCli(argc, argv);
    const std::vector<Scenario> all = buildScenarios();

    if (cli.list) {
        std::printf("r19_clickscan scenarios (%zu):\n", all.size());
        for (const Scenario& s : all) std::printf("  %s\n", s.name);
        return 0;
    }

    // -- scenario selection --------------------------------------------------
    std::vector<const Scenario*> selected;
    if (cli.scenarios.empty()) {
        for (const Scenario& s : all) selected.push_back(&s);
    } else {
        for (const std::string& want : cli.scenarios) {
            const Scenario* hit = nullptr;
            for (const Scenario& s : all)
                if (want == s.name) { hit = &s; break; }
            if (!hit) {
                std::fprintf(stderr, "unknown scenario: %s\nvalid names:\n",
                             want.c_str());
                for (const Scenario& s : all)
                    std::fprintf(stderr, "  %s\n", s.name);
                return 2;
            }
            if (std::find(selected.begin(), selected.end(), hit) ==
                selected.end())
                selected.push_back(hit); // dedupe repeats
        }
    }

    // -- sweep dimensions ----------------------------------------------------
    std::vector<octavox::Interpolation> interps;
    if (cli.interp.empty() || cli.interp == "lagrange6")
        interps.push_back(octavox::Interpolation::Lagrange6);
    if (cli.interp.empty() || cli.interp == "obskernel6")
        interps.push_back(octavox::Interpolation::ObsKernel6);
    const std::vector<int> rates =
        cli.rate ? std::vector<int>{cli.rate}
                 : std::vector<int>{44100, 48000, 96000};
    const std::vector<int> blocks =
        cli.block ? std::vector<int>{cli.block} : std::vector<int>{64, 128, 512};

    ensureDir(cli.outDir);

    std::printf("octavox_r19_clickscan  engine=%s  (our own RE evidence tool)\n",
                octavox::versionString());
    std::printf("%zu scenario(s) x %zu interp(s) x %zu rate(s) x %zu block(s) "
                "= %zu render(s)%s\n",
                selected.size(), interps.size(), rates.size(), blocks.size(),
                selected.size() * interps.size() * rates.size() * blocks.size(),
                cli.legacy ? " + legacy comparison" : "");
    if (cli.wav)
        std::printf("wav dumps: %s/<scenario>_<interp>_<sr>_<block>.wav\n",
                    cli.outDir.c_str());
    std::printf("\n");

    const EnvInfo env = queryEnv();
    const double t0 = nowSec();

    // -- main sweep ----------------------------------------------------------
    std::vector<RenderRecord> renders;
    renders.reserve(selected.size() * interps.size() * rates.size() *
                    blocks.size());
    printTableHeader();
    for (const Scenario* sc : selected) {
        for (octavox::Interpolation ip : interps) {
            for (int sr : rates) {
                for (int blk : blocks) {
                    const std::string wavp =
                        cli.wav ? wavPathFor(cli.outDir, *sc, interpName(ip),
                                             sr, blk, false)
                                : std::string();
                    RenderRecord r =
                        runRender(*sc, sr, blk, ip, false, cli.wav, wavp);
                    printRenderLine(r);
                    renders.push_back(std::move(r));
                }
            }
        }
    }

    // -- legacy comparison sweep (rate 48000, block 128, legacy pins) -------
    // Legacy runs every selected scenario at 48000/128 for each interpolation
    // in the (possibly filtered) interp set, so each entry sits side-by-side
    // with its modern counterpart in the same report.
    std::vector<RenderRecord> legacyRenders;
    if (cli.legacy) {
        std::printf("\nlegacy comparison (detector=FullRate, "
                    "commit/delay/voice crossfades = 0) at 48000 Hz, "
                    "block 128:\n\n");
        printTableHeader();
        for (const Scenario* sc : selected) {
            for (octavox::Interpolation ip : interps) {
                const std::string wavp =
                    cli.wav ? wavPathFor(cli.outDir, *sc, interpName(ip), 48000,
                                         128, true)
                            : std::string();
                RenderRecord r =
                    runRender(*sc, 48000, 128, ip, true, cli.wav, wavp);
                printRenderLine(r);
                legacyRenders.push_back(std::move(r));
            }
        }
    }

    // -- totals ---------------------------------------------------------------
    const Totals t = totalsOf(renders);
    std::printf("\n---- totals: %ld render(s), %ld clean, %ld with clicks "
                "----\n",
                t.renders, t.clean, t.clicks);
    std::printf("per-scenario breakdown:\n");
    printBreakdown(renders);
    if (cli.legacy) {
        const Totals lt = totalsOf(legacyRenders);
        std::printf("\n---- legacy totals: %ld render(s), %ld clean, %ld with "
                    "clicks ----\n",
                    lt.renders, lt.clean, lt.clicks);
        std::printf("per-scenario breakdown (legacy):\n");
        printBreakdown(legacyRenders);
    }

    // -- JSON report -----------------------------------------------------------
    writeJson(cli.jsonPath, env, renders, legacyRenders);
    std::printf("\nJSON report: %s\n", cli.jsonPath.c_str());

    const double elapsed = nowSec() - t0;
    std::printf("done: %zu render(s) in %.1f s\n",
                renders.size() + legacyRenders.size(), elapsed);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// main: nothing may escape (spec); nonzero exit on failure.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    try {
        return runTool(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r19_clickscan: fatal: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "r19_clickscan: fatal: unknown exception\n");
        return 1;
    }
}
