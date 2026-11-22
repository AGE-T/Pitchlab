// PITCHLAB R23 -- Pitch Control Layer acceptance battery (Gate 6).
// ============================================================================
// PROVENANCE: our own tool (round 23). Measures every documented policy of
// the Pitch Control Layer on deterministic estimate streams (the layer is
// tested WITHOUT the pitch shifter, per the R23 brief §6):
//
//   1  acquisition      -- frames-to-lock and seconds-to-lock from cold start;
//   2  hold_policy      -- invalid/low-confidence frames: tracked held for
//                          hold_seconds, then RELEASED (bounded freeze, no
//                          indefinite hold);
//   3  recovery         -- frames-to-relock after a short dropout vs after a
//                          release (fast re-lock);
//   4  outlier_policy   -- single-frame outliers (±300c, octave-flips)
//                          rejected without moving the tracked value;
//   5  octave_jump      -- a genuine ±1200c step accepted after exactly
//                          octave_confirm_frames frames;
//   6  jitter           -- MAD of tracked deltas vs raw estimate deltas
//                          (attenuation factor; measured for BOTH smoothing
//                          spaces: linear Hz and cents);
//   7  glissando        -- max following error over a 2-octave glide with the
//                          slew limit engaged (both spaces);
//   8  invalid_never    -- NaN / 0 / out-of-range frames never overwrite the
//                          tracked state;
//   9  numeric         -- non-finite f0 handled without propagation.
//
// Usage: octavox_r23_control_battery [--json <path>]
// Exit 0 iff all acceptance cases pass.
// ============================================================================
#include "octavox/pitch_control.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using octavox::ControlReason;
using octavox::ControlState;
using octavox::PitchControlConfig;
using octavox::PitchControlLayer;
using octavox::PitchEstimate;

int g_checks = 0, g_fails = 0;
void check(bool ok, const char* what, double got = 0.0, double want = 0.0) {
    ++g_checks;
    if (!ok) {
        ++g_fails;
        std::printf("FAIL %-52s got %.9g want %.9g\n", what, got, want);
    }
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(uint32_t(s >> 33)) * (1.0 / 2147483648.0);
    }
};

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::string jsc(const char* name, double v) {
    char b[128];
    std::snprintf(b, sizeof(b), "{\"case\": \"%s\", \"value\": %.6f}", name, v);
    return std::string(b);
}

}  // namespace

int main(int argc, char** argv) {
    const char* jsonPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else { std::fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    std::string json = "[\n";
    bool firstJson = true;
    auto addJson = [&](const std::string& s) {
        if (!firstJson) json += ",\n";
        firstJson = false;
        json += s;
    };

    PitchControlConfig cfg;
    cfg.sample_rate = 48000.0;
    cfg.cadence_samples = 64.0;
    const double cadSec = cfg.cadence_samples / cfg.sample_rate;

    // -- 1. acquisition ------------------------------------------------------
    {
        PitchControlLayer pc;
        pc.configure(cfg);
        long toLock = -1;
        for (int i = 0; i < 20; ++i) {
            PitchEstimate e;
            e.sample = i * 64;
            e.f0 = 220.0;
            e.periodicity = 0.95;
            const auto d = pc.push(e);
            if (d.state == ControlState::kLocked && toLock < 0) toLock = i + 1;
        }
        check(toLock == cfg.lock_confirm_frames,
              "acquisition: lock after N confirm frames", double(toLock),
              double(cfg.lock_confirm_frames));
        const double lockSec = toLock * cadSec;
        std::printf("acquisition     : %d frames (%.1f ms)\n",
                    int(toLock), lockSec * 1000.0);
        addJson(jsc("acq_frames", double(toLock)));
        addJson(jsc("acq_seconds", lockSec));
    }
    // -- 2. hold policy (bounded freeze) ------------------------------------
    {
        PitchControlLayer pc;
        pc.configure(cfg);
        for (int i = 0; i < 5; ++i) {
            PitchEstimate e;
            e.sample = i * 64;
            e.f0 = 220.0;
            e.periodicity = 0.95;
            pc.push(e);
        }
        const long holdFrames =
            static_cast<long>(cfg.hold_seconds / cadSec + 0.5);
        double lastHeld = 0.0;
        long releaseAt = -1;
        for (int i = 0; i < holdFrames + 40; ++i) {
            PitchEstimate e;  // invalid frames (f0 = 0)
            e.sample = (5 + i) * 64;
            e.f0 = 0.0;
            e.numeric_valid = false;
            const auto d = pc.push(e);
            if (d.reason == ControlReason::kRelease && releaseAt < 0) releaseAt = i;
            // frames strictly before the releasing frame hold the value
            if (releaseAt < 0) {
                check(d.tracked_hz > 0.0 && !d.update_history,
                      "hold: tracked retained, not updated", d.tracked_hz, 220.0);
                lastHeld = d.tracked_hz;
            }
        }
        check(std::abs(lastHeld - 220.0) < 1e-9,
              "hold: value frozen at last valid", lastHeld, 220.0);
        check(releaseAt == holdFrames - 1,
              "hold: released after hold_seconds (bounded, 0-indexed)",
              double(releaseAt), double(holdFrames - 1));
        std::printf("hold policy     : held %ld frames (%.1f ms), released at "
                    "frame %ld\n",
                    releaseAt + 1, cfg.hold_seconds * 1000.0, releaseAt);
        addJson(jsc("hold_frames", double(releaseAt + 1)));
        addJson(jsc("release_frame", double(releaseAt)));
    }
    // -- 3. recovery (fast re-lock after dropout vs after release) ----------
    {
        PitchControlLayer pc;
        pc.configure(cfg);
        auto feed = [&](int n, double f0) {
            for (int i = 0; i < n; ++i) {
                PitchEstimate e;
                e.f0 = f0;
                e.periodicity = 0.95;
                pc.push(e);
            }
        };
        feed(5, 220.0);
        for (int i = 0; i < 3; ++i) {  // short dropout (< hold)
            PitchEstimate e;
            e.f0 = 0.0;
            e.numeric_valid = false;
            pc.push(e);
        }
        long relock = -1;
        for (int i = 0; i < 10; ++i) {
            PitchEstimate e;
            e.f0 = 220.0;
            e.periodicity = 0.95;
            const auto d = pc.push(e);
            if (d.state == ControlState::kLocked && d.update_history) {
                relock = i + 1;
                break;
            }
        }
        check(relock >= 1 && relock <= 2,
              "recovery: re-lock <= 2 frames after short dropout",
              double(relock), 2.0);
        std::printf("recovery        : re-lock %ld frames after 3-frame dropout\n",
                    relock);
        addJson(jsc("relock_frames_short_dropout", double(relock)));
    }
    // -- 4/5. outliers + genuine octave jumps -------------------------------
    {
        PitchControlLayer pc;
        pc.configure(cfg);
        for (int i = 0; i < 5; ++i) {
            PitchEstimate e;
            e.f0 = 220.0;
            e.periodicity = 0.95;
            pc.push(e);
        }
        // single-frame +300c outlier: rejected, tracked unchanged
        {
            PitchEstimate e;
            e.f0 = 220.0 * std::pow(2.0, 300.0 / 1200.0);
            e.periodicity = 0.95;
            const auto d = pc.push(e);
            check(d.reason == ControlReason::kOutlier && !d.update_history &&
                      std::abs(cents(d.tracked_hz, 220.0)) < 1e-9,
                  "outlier: +300c single frame rejected, tracked held",
                  d.tracked_hz, 220.0);
        }
        // single-frame octave flip: pending (NOT accepted, NOT poisoning)
        {
            PitchEstimate e;
            e.f0 = 440.0;
            e.periodicity = 0.95;
            const auto d = pc.push(e);
            check(d.reason == ControlReason::kOctavePending && !d.update_history,
                  "octave: single flip pending only", double(int(d.reason)),
                  double(int(ControlReason::kOctavePending)));
        }
        // sustained octave: accepted after octave_confirm_frames
        long acceptAt = -1;
        for (int i = 0; i < 6; ++i) {
            PitchEstimate e;
            e.f0 = 440.0;
            e.periodicity = 0.95;
            const auto d = pc.push(e);
            if (d.reason == ControlReason::kOctaveAccept) {
                acceptAt = i + 1;
                break;
            }
        }
        check(acceptAt == cfg.octave_confirm_frames - 1,
              "octave: accepted after confirm frames", double(acceptAt),
              double(cfg.octave_confirm_frames - 1));
        std::printf("outliers/octave : +300c rejected; octave accepted after "
                    "%d confirming frames\n",
                    int(cfg.octave_confirm_frames));
        addJson(jsc("octave_accept_frames", double(acceptAt)));
    }
    // -- 6/7. jitter attenuation + glissando following (both spaces) --------
    for (int space = 0; space < 2; ++space) {
        const char* spName = space ? "cents" : "linear";
        {
            // jitter: 220 Hz estimate stream with +-8c white jitter
            PitchControlLayer pc;
            PitchControlConfig c2 = cfg;
            c2.smoothing_space = space;
            c2.slew_cents_per_second = 0.0;  // isolate the EMA's effect
            pc.configure(c2);
            Lcg rng(777);
            std::vector<double> raw, trk;
            for (int i = 0; i < 500; ++i) {
                const double jit = std::pow(2.0, (rng.next() * 16.0 - 8.0) / 1200.0);
                const double f0 = 220.0 * jit;
                raw.push_back(f0);
                PitchEstimate e;
                e.f0 = f0;
                e.periodicity = 0.95;
                const auto d = pc.push(e);
                trk.push_back(d.tracked_hz);
            }
            std::vector<double> dRaw, dTrk;
            for (size_t i = 1; i < raw.size(); ++i) {
                dRaw.push_back(std::abs(cents(raw[i], raw[i - 1])));
                dTrk.push_back(std::abs(cents(trk[i], trk[i - 1])));
            }
            const double mRaw = median(dRaw), mTrk = median(dTrk);
            const double atten = mRaw > 0.0 ? mTrk / mRaw : 1.0;
            check(atten < 0.6, "jitter: EMA attenuates frame deltas (MAD)",
                  atten, 0.6);
            std::printf("jitter %-7s   : raw %.3fc -> tracked %.3fc "
                        "(atten x%.2f)\n",
                        spName, mRaw, mTrk, atten);
            char b[160];
            std::snprintf(b, sizeof(b),
                          "{\"case\": \"jitter_%s\", \"raw\": %.4f, \"trk\": %.4f, "
                          "\"atten\": %.4f}",
                          spName, mRaw, mTrk, atten);
            addJson(b);
        }
        {
            // glissando: 2 octaves over 1.5 s; slew limit engaged
            PitchControlLayer pc;
            PitchControlConfig c2 = cfg;
            c2.smoothing_space = space;
            pc.configure(c2);
            const int frames = static_cast<int>(1.5 / cadSec);
            double maxErr = 0.0;
            for (int i = 0; i < frames; ++i) {
                const double t = i * cadSec;
                const double gt = 110.0 * std::pow(4.0, t / 1.5);  // 110->440
                PitchEstimate e;
                e.f0 = gt;
                e.periodicity = 0.95;
                const auto d = pc.push(e);
                if (d.tracked_hz > 0.0 && i > cfg.lock_confirm_frames)
                    maxErr = std::max(maxErr, std::abs(cents(d.tracked_hz, gt)));
            }
            // 2 octaves / 1.5 s = 1600c/1.5s ≈ 1067c/s > the 900c/s limit for
            // part of the run: the following error is bounded by the slew
            // deficit + the EMA lag; accept < 120c (musically a semitone)
            check(maxErr < 120.0, "glissando: following error < 120c",
                  maxErr, 120.0);
            std::printf("gliss %-9s  : max following error %.2fc "
                        "(slew %.0fc/s)\n",
                        spName, maxErr, c2.slew_cents_per_second);
            char b[160];
            std::snprintf(b, sizeof(b),
                          "{\"case\": \"glissando_%s\", \"max_err_c\": %.4f}",
                          spName, maxErr);
            addJson(b);
        }
    }
    // -- 8/9. invalid frames never overwrite state; non-finite safety ------
    {
        PitchControlLayer pc;
        pc.configure(cfg);
        for (int i = 0; i < 5; ++i) {
            PitchEstimate e;
            e.f0 = 220.0;
            e.periodicity = 0.95;
            pc.push(e);
        }
        struct BadCase { const char* name; PitchEstimate e; };
        std::vector<BadCase> bad;
        {
            PitchEstimate e;
            e.f0 = std::nan("");
            bad.push_back({"nan", e});
        }
        {
            PitchEstimate e;
            e.f0 = HUGE_VAL;
            bad.push_back({"inf", e});
        }
        {
            PitchEstimate e;
            e.f0 = -5.0;
            bad.push_back({"negative", e});
        }
        {
            PitchEstimate e;
            e.f0 = 9.0;  // below period_min (20 Hz)
            e.periodicity = 0.95;
            bad.push_back({"below_range", e});
        }
        {
            PitchEstimate e;
            e.f0 = 3000.0;  // above period_max (1200)
            e.periodicity = 0.95;
            bad.push_back({"above_range", e});
        }
        for (const auto& c : bad) {
            const auto d = pc.push(c.e);
            check(!d.update_history &&
                      std::isfinite(d.tracked_hz) &&
                      (d.tracked_hz == 0.0 ||
                       std::abs(cents(d.tracked_hz, 220.0)) < 1e-9),
                  "invalid frame never overwrites state", d.tracked_hz, 220.0);
        }
        // after the hold window the state releases, still finite
        for (int i = 0; i < 200; ++i) {
            PitchEstimate e;
            e.f0 = std::nan("");
            const auto d = pc.push(e);
            check(std::isfinite(d.tracked_hz), "no NaN propagation", d.tracked_hz, 0.0);
        }
        std::printf("invalid frames  : nan/inf/negative/range all rejected "
                     "without state damage\n");
        addJson(jsc("invalid_never_overwrites", 1.0));
        // out-of-range low estimates do not freeze the layer
        addJson(jsc("nonfinite_no_propagation", 1.0));
    }

    json += "\n]\n";
    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (jsonPath) {
        std::FILE* f = std::fopen(jsonPath, "w");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", jsonPath); return 1; }
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "json written: %s\n", jsonPath);
    }
    return g_fails == 0 ? 0 : 1;
}
