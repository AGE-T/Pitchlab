// ---------------------------------------------------------------------------
// OCTAVOX native DSP core -- m02-style pitch telemetry (R31, implementation)
// ============================================================================
// See include/octavox/telemetry.hpp for the provenance note, the id
// semantics (R28 F1.6) and the poller architecture (R30 F2.3-F2.6).
// ---------------------------------------------------------------------------
#include "octavox/telemetry.hpp"

#include <cmath>

namespace octavox {

namespace {

constexpr double kLn2 = 0.69314718055994530942;
constexpr double kQ23 = 8388608.0;  // 2^23

double centsFrom(double hz) {
    // 1200/ln2 * ln(hz / C4): the original's exact cents-from-C4 form.
    return 1200.0 / kLn2 * std::log(hz / kTelemetryC4Hz);
}

}  // namespace

void TelemetryPoller::publish(int voice, int id, double v) {
    std::atomic<double>& a = cells_[voice][id];
    const double prev = a.load(std::memory_order_relaxed);
    if (prev != v) {  // exact change detection (NaN never occurs in the
        // engine's telemetry quantities: every value is a finite product
        // of finite engine state; the sentinel is a finite constant)
        a.store(v, std::memory_order_relaxed);
        dirty_.fetch_or(static_cast<uint64_t>(1) << (voice * 8 + (id - 7)),
                        std::memory_order_release);
    }
}

void TelemetryPoller::poll(const MultichannelShifter& engine, int voices) {
    if (voices < 0) voices = 0;
    if (voices > kTelemetryMaxVoices) voices = kTelemetryMaxVoices;

    // Channel 0 = the telemetry reference engine (the original's poller
    // read the per-voice engines; PitchLab's channels are structurally
    // isolated engines, so the tracked pitch is served from channel 0 and
    // the per-voice ratios from each voice's own state on that channel).
    //
    // The current pitch: the control layer's validated tracked value when
    // the tracking gate is active and locked, else the raw detector
    // estimate (the default configuration runs the gate OFF -- the legacy
    // accept law -- where trackedHz() stays 0 and lastEstimateHz() is the
    // live pitch; the R22-P1 diagnostics chain's levels 3 and 5).
    const double tracked = engine.trackedHz();
    const double f0 = tracked > 0.0 ? tracked : engine.lastEstimateHz();
    const bool gate = f0 > 0.0;
    const double cmndfMin = engine.lastEstimateCmndfMin();
    const double periodicity = 1.0 - cmndfMin;  // the layer's own score
    double stateWeight;
    switch (engine.lastControlState()) {
        case ControlState::kLocked:   stateWeight = 1.0; break;
        case ControlState::kLocking:
        case ControlState::kHold:     stateWeight = 0.5; break;
        default:                      stateWeight = 0.0; break;
    }

    for (int v = 0; v < kTelemetryMaxVoices; ++v) {
        if (v >= voices) {
            // Parked voices: every cell to 0 (the sentinel only applies to
            // a LIVE voice without pitch; an inactive voice reads zeros).
            for (int id = 0; id < kTelIdCount; ++id) publish(v, id, 0.0);
            continue;
        }
        const VoiceStats& vs = engine.voiceStats(0, v);

        // id 7: cents from C4 of the current pitch (validated tracked value
        // when the gate is locked, else the raw estimate); the original's
        // -65535 sentinel when nothing is tracked.
        publish(v, kTelCentsFromC4, f0 > 0.0 ? centsFrom(f0) : kTelemetryNoPitchSentinel);

        // id 8: the voice's current gliding ratio (the engine's own
        // diagnostic quantity; 1.0 = unity).
        const double ratio = vs.ratio;
        publish(v, kTelFreqRatio, ratio);

        // id 9: the Q23-quantised ratio n/(n+8) (R28 F1.6 id 9 verbatim).
        const double n = std::llround(ratio * kQ23);
        publish(v, kTelQ23Ratio, n / (n + 8.0));

        // id 10: sqrt(frac * (1 - 2^-23)) with frac = the voice's grain
        // phase in [0,1) (R28 F1.6 id 10 verbatim, PitchLab's phase).
        double frac = vs.phase;
        if (!(frac >= 0.0)) frac = 0.0;  // phase is maintained in [0,1);
        if (frac > 1.0) frac = 1.0;      // defensive clamp only
        publish(v, kTelPhaseConf, std::sqrt(frac * (1.0 - 1.0 / kQ23)));

        // id 11 (PitchLab mapping -- LABELLED): a confidence-like
        // combination = periodicity x state weight. The original's exact
        // combination is engine-internal; this is the honest analogue
        // built from PitchLab's own confidence ingredients.
        publish(v, kTelConfidence, stateWeight * periodicity);

        // ids 12/13: the -cents / +cents correction twins = the voice's
        // applied shift vs unity, sign-split, gated by the correction-
        // active condition (a live pitch is being shifted). Mirrors the
        // original's gated +/-cents twin fields.
        const double shiftCents = gate ? 1200.0 / kLn2 * std::log(ratio) : 0.0;
        publish(v, kTelNegCents, -shiftCents);
        publish(v, kTelPosCents, shiftCents);
    }
}

double TelemetryPoller::cell(int voice, int id) const {
    if (voice < 0 || voice >= kTelemetryMaxVoices || id < 0 || id >= kTelIdCount)
        return 0.0;
    return cells_[voice][id].load(std::memory_order_relaxed);
}

uint64_t TelemetryPoller::dirtyBits() const {
    return dirty_.load(std::memory_order_acquire);
}

uint64_t TelemetryPoller::consumeDirtyBits() {
    return dirty_.exchange(0, std::memory_order_acq_rel);
}

void TelemetryPoller::reset() {
    for (int v = 0; v < kTelemetryMaxVoices; ++v)
        for (int id = 0; id < kTelIdCount; ++id)
            cells_[v][id].store(0.0, std::memory_order_relaxed);
    dirty_.store(0, std::memory_order_release);
}

}  // namespace octavox
