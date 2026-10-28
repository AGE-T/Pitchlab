// OCTAVOX independent native DSP core (public API) -- version 15.0.0
// ============================================================================
// [R16-P1 note, 2026-10-28: the title line previously read "rounds 13/14
// recovery"; superseded — this header is the normative 15.0.0 contract. The
// version-history block below records the recovery lineage.]
// Version 15.0.0 = round-15 P1 per-voice harmonizer parameter surface:
//   * per-voice ABSOLUTE ratio / gain / mute / base-delay overrides
//     (setVoiceRatio / setVoiceGain / setVoiceMute / setVoiceBaseDelay),
//   * a wet/dry mix with a latency-ALIGNED dry path (setWetDry) and
//   * a click-free, state-transparent bypass (setBypass),
// all on the audio thread with the RT/block-boundary contract below.
// With the defaults (wet = 1.0, bypass off, no per-voice overrides, gain 1)
// the engine is BIT-IDENTICAL to version 14.0.0 (gated by the suite's
// default_neutrality case against pre-change reference hashes).
// Version 14.0.0 = recovered round-13/14 scope rebuilt on the round-12
// baseline after an environment reset (see worklog R15-R0 / R15-E1); the
// original rounds-13/14 artifacts were lost. The round-12 API and behaviour
// are preserved except for the two intentional, test-driven changes of the
// rebuilt rounds (both made against a failing test written first):
//   * R13-b: the pitch detector's analysis window is now SAMPLE-RATE
//     RELATIVE (win = max(2048, ceil(2.5*tau_max)), which implies the
//     win >= 2*tau_max + 128 law with floor 2048) instead of the fixed
//     2048 samples, which made the detector blind (m < 32) above 59.5 kHz
//     and, once m was merely restored, mirror-dip unstable at m << period.
//     The law keeps the difference support m = win/2 - tau_max >= tau_max/4
//     (value-mirror lag immunity; see src/dsp.cpp for the measurement).
//     44.1/48 kHz behaviour is bit-identical to round 12 (win == 2048 there).
//   * R13-c: prepare() with an equivalent config re-uses the existing
//     buffers (zero-allocation re-prepare) instead of delete/new.
// ============================================================================
//
// HISTORICAL SALVAGE NOTE (R15-P2, 2026-10-26 ~21:00 UTC — incident record,
// kept unchanged): the live tree at /home/z/octavox/work/Octavox_RE_evidence
// was wiped by a second environment reset DURING the round-15 P2 session
// (the first minutes of the session verified 78/78 green at 15.0.0 on the
// intact tree; the reset followed). This header is restored VERBATIM from
// the session's read of the intact 15.0.0 file. See
// evidence/analysis_round15/NOTES_R15P2.md.
//
// [R16-P1 note, 2026-10-28 — final implementation status, distinguishing the
// incident record above: the header text above is the ORIGINAL 15.0.0
// contract (verbatim restore, unchanged since); the IMPLEMENTATION it
// specifies was subsequently re-built (worklog R15-REB1) and verified
// bit-neutral vs 12.0.0 by the four reference hashes
// (native/r15_reference_hashes.txt); the delivered acceptance suite is
// 85/85 (release + ASan/UBSan). The salvage note is therefore HISTORY, not
// a statement about the current tree.]
//
// PROVENANCE
// ----------
// This library is OUR OWN implementation of the round-11 "delayline_aligned"
// design (poc/octavox_v2.py, preserved untouched as the Python reference
// baseline) ported to C++17, plus the round-12 unity engagement fix.
// It contains no target code. The single exception is documented data: the
// optional Interpolation::ObsKernel6 mode uses the 256x6 float kernel table
// OBSERVED in the target's .rdata (evidence/analysis_round12/
// r12_obs_kernel_table.h, extraction script r12_interp_table.py) for research
// comparison; that mode is opt-in and clearly labelled.
//
// Nothing here is a VST3 plugin and nothing here claims to reproduce the
// original plugin's runtime behaviour. The target was never executed.
//
// ARCHITECTURE (see native/ARCHITECTURE.md for the full note)
// -----------------------------------------------------------
//  AlignedDelayLine   one mono engine, 1..8 voices, planar float in/out.
//  MultichannelShifter N AlignedDelayLine engines (one per channel) with a
//                     shared parameter surface -- channel isolation is
//                     structural, not conventional.
//  prepare()          allocates every buffer (ring, detector window, work
//                     arrays, per-voice state). A prepare() whose
//                     buffer-determining fields (sample_rate, max_voices,
//                     f0_history, detection_interval) match the current
//                     engine re-configures IN PLACE: zero allocations, zero
//                     locks, identical result to a fresh engine (verified by
//                     the acceptance suite's global-new counter). Other
//                     prepare() calls take the allocate-and-rebuild path.
//                     After prepare() returns, no allocation happens again:
//                     process(), reset() and every parameter setter are
//                     steady-state allocation-free (counter-gated tests).
//  reset()            returns the engine to the post-prepare state
//                     (allocation-free).
//  process()          block size agnostic: the control-rate detector fires
//                     every `detection_interval` STREAM samples, so the
//                     output is invariant to how the caller splits the
//                     stream into blocks (an intentional improvement over
//                     the Python reference, which restarts its 64-sample
//                     control loop at every process() call).
//
// REAL-TIME (RT) CONTRACT -- which call runs on which thread
// -----------------------------------------------------------
// * AUDIO THREAD (the process() caller): process(), reset(), setRatio(),
//   setVoices(), setSpread(), setFeedback(), voiceStats(), lastEstimateHz(),
//   latencySamples(), config(), ready(). All of these are wait-free and
//   allocation-free after the first prepare(); none of them throw; none of
//   them take locks or touch shared mutable state (the interpolator tables
//   are immutable after first use -- thread-safe magic static, built once
//   during warm-up, not per block).
// * NON-REAL-TIME / controller thread: construction/destruction, prepare()
//   in its allocate-and-rebuild path, and any first-use table construction.
//   prepare() with matching buffer-determining fields IS safe on the audio
//   thread (zero allocation) and is the supported "rate change confirmed
//   same size" fast path; a mismatched prepare() allocates and frees and
//   must be synchronised off-thread or during a pause by the host.
// * One engine = one thread at a time. MultichannelShifter::process() runs
//   its per-channel engines sequentially on the calling thread; engines are
//   independent (structural channel isolation) and MAY be farmed out to
//   other threads by the host, one engine per thread, without locks.
// * VoiceStats is returned by reference into thread-local scratch: copy the
//   value out if it must survive the next call on the same thread.
// * No exceptions escape any entry point of this library (the process path
//   is noexcept by construction; prepare() signals failure by return value).
//
// COLD-START ALIGNMENT LAW (round-13 recovery, R13-a; measured by the
// cold_start_* suite cases)
// ---------------------------------------------------------------
// Let L = latencySamples() = base_delay + grain/2 (the nominal mid-grain
// read delay at reset; every voice starts at grain phase frac = 0.5). A
// UNITY voice parks before the first sample, so its first read is exactly
// at delay L_v = base_v + grain/2; a SHIFTED voice's grain phase advances
// by (1 - r)/S BEFORE the read at every sample -- including the first one
// (processVoiceSample updates frac, then reads) -- so its first read is at
// L_v + (1 - r) (exactly L_v when r == 1).
// * Unity (ratio == 1.0 bit-exact): the voice parks on the integer delay
//   d_park = round(base + grain/2) and the output is the bit-exact delayed
//   copy out[k] == in[k - d_park + 1]; with the default config d_park == L,
//   so the FIRST non-zero output sample for an impulse at t=0 is exactly
//   k = L - 1 (the -1 is the reference ring convention: a read at delay d
//   returns the sample pushed d-1 samples ago). Output before k = L - 1 is
//   exactly zero -- the ring is pre-zeroed, standing in for "silence before
//   t = 0"; the engine behaves like the same engine running forever.
// * Ratio r != 1.0 from the very first sample (no glide at cold start: the
//   gliding ratio is initialised AT its target): before the first grain
//   wrap (S == grain, no commits yet) the read delay of output sample k is
//         d(k) = L + (1 - r) * (k + 1),
//   i.e. the output sample k interpolates the input at time
//   tau(k) = r * (k + 1) - L. In the age-indexed ring the input's first
//   sample (t = 0) sits at delay k+1 while output sample k is produced, so
//   it first appears at output k0 = the first k whose interpolation kernel
//   window (6-tap Lagrange: ring delays [floor(d)-2, floor(d)+3]; linear:
//   [floor(d), floor(d)+1]) contains the ring delay k+1 -- the window's
//   earliest reach is floor(d) - 2. The output is EXACTLY zero for every
//   k < k0: the ring is pre-zeroed and no other read exists at cold start
//   (a shifted voice starts fully engaged, e = 1 and a = 0, so the only
//   reads are the A tap at d and the B tap at d + S >= d). Up-shifts
//   contact the signal start EARLIER than unity's L - 1 (the read point
//   starts mid-grain and walks toward the write point at closing speed r),
//   down-shifts LATER; both follow the same law, and the cold_start_* cases
//   gate the measured first non-zero sample, the exact zeros before it and
//   the value at k0 against an independent re-derivation of this law inside
//   the test, at block sizes 1/64/512, for 1 and 8 voices.
// * No spurious transient: the engagement state is sane from sample 0
//   (unity starts parked, shifted starts fully engaged) and the output is
//   exactly zero everywhere before the kernel can possibly reach the input
//   start (gated to max|out| == 0 before the contact point, on step and
//   sine onsets, 1 and 8 voices).
//
// UNITY BEHAVIOUR (round-12 fix)
// ------------------------------
// The Python reference keeps a continuous complementary two-tap blend whose
// phase freezes at unity -> a measured comb (-2.42 dB @ 220 Hz). The target
// (static evidence, round 12, Finding 3) rests on a SINGLE interpolated tap
// and only crossfades transiently after a re-arm. This core mirrors that
// architecture: when a voice's target ratio is EXACTLY 1.0 (bit-exact) it
// engages a single-tap path morphed in/out over `engagement_seconds`, and
// parks on the nearest INTEGER delay so that exact unity is a bit-exact
// delayed copy of the input (out[k] == in[k - d_park], neutral processing).
// There is no bypass region: any ratio != 1.0 -- including +-1 cent -- gets
// the full dual-tap processing path. Departing unity also refreshes the tap
// spacing S from the live period estimate when the read point can be
// preserved exactly.
//
// ROUND-15 PARAMETER SURFACE (P1) -- per-voice overrides, wet/dry, bypass
// ----------------------------------------------------------------------
// Config additions (validated exactly like engagement_seconds: negative
// values are rejected by prepare()):
//   gain_ramp_seconds   per-voice gain / mute ramp time (linear-in-amplitude)
//   bypass_seconds      bypass enter/exit crossfade time
//
// PER-VOICE RATIO (setVoiceRatio(v, r)):
//   v in [0, max_voices); r > 0. Sets voice v's ABSOLUTE target ratio: the
//   geometric glide toward the target applies per voice exactly as for the
//   global ratio (a large jump is handled by the same glide -- no
//   special-casing), and a per-voice target of exactly 1.0 engages that
//   voice's unity parking, exactly as the global ratio would. Each voice
//   carries an override flag: setVoiceRatio sets it; setRatio(), setSpread()
//   and prepare() RE-DERIVE every voice's target from (ratio, spread) and
//   CLEAR all overrides (a cleared override voice loses its absolute value
//   and returns to the spread table). reset() PRESERVES overrides.
//
// PER-VOICE GAIN (setVoiceGain(v, g)): linear amplitude target, g >= 0,
//   rejected otherwise. The current gain ramps LINEARLY toward the target at
//   a rate of |target - start| / (gain_ramp_seconds * sample_rate) per sample
//   (the ramp restarts from the current gain whenever the target changes), so
//   a change of size D completes in exactly gain_ramp_seconds. The per-voice
//   gain multiplies AFTER the existing voice-count normalisation:
//   final voice gain = normalisation * user_gain[v].
//   At the default (all user gains 1.0) the multiplication is bit-neutral.
//
// PER-VOICE MUTE (setVoiceMute(v, on)): forces the voice's EFFECTIVE gain
//   target to 0 through the SAME ramp (unmute ramps back to user_gain[v]).
//   The voice's DSP state (ring phase, glide, spacing, engagement) keeps
//   running while muted; only its contribution to the mix is removed. A voice
//   whose ramped gain is exactly 0 contributes exactly nothing (its sample is
//   not even added), so a mute/unmute cycle is bit-transparent once the ramps
//   have completed.
//
// PER-VOICE BASE DELAY (setVoiceBaseDelay(v, d)): d > 0 and the voice's whole
//   read range (B tap at d + S, S <= 1.5*grain, plus the 6-tap kernel margin)
//   must stay inside the 2^18 ring -- invalid values are rejected (no-op).
//   The new delay is applied at the START of the next process() call by
//   re-aligning ONLY voice v's read position: the fractional grain phase and
//   tap spacing are preserved (the read point translates by (new - old) base
//   delay); a parked voice re-parks on the nearest integer of its new range.
//   No other voice's output is touched (isolation is structural: base_delay is
//   per-voice state). reset() preserves the override; prepare() clears it.
//
// WET/DRY MIX (setWetDry(w)): w clamped to [0, 1], default 1.0.
//   out = w * wet_signal + (1 - w) * dry_signal,
//   dry_signal[k] = the RAW input delayed to align with the wet path's
//   intrinsic latency: dry[k] = in[k - (latencySamples() - 1)] (the ring
//   convention read(d) = sample d-1 ago; implemented as an exact masked
//   integer read of a dedicated raw-input ring, so it is bit-exact).
//   MIX LAW: linear in amplitude (documented, no equal-power curve).
//   Endpoints: w = 1 is the bit-identical 14.0.0 output path (special-cased,
//   no dry read happens); w = 0 is the bit-exact delayed copy. The global
//   feedback stays WET-PATH-ONLY: it taps the pre-mix wet signal, so the
//   wet path's state evolves identically at every mix setting (and the dry
//   path never carries feedback).
//
// BYPASS (setBypass(on)): click-free output bypass. The OUTPUT crossfades
//   between the normal signal (the wet/dry mix at the current setting) and
//   the dry delayed signal over bypass_seconds, in BOTH directions
//   (fixed-slope blend, full range = exactly bypass_seconds). While bypassed
//   the engine keeps processing -- every voice's DSP state, the detector and
//   the (wet-path-only) feedback advance exactly as non-bypassed, because the
//   bypass blend only touches the final output sample -- so a full
//   enter+exit cycle is BIT-IDENTICAL to a never-bypassed run outside the two
//   ramp regions.
//
// SEMANTICS CONTRACT (all new setters, like the existing ones): audio-thread
//   safe, allocation-free, lock-free, no exceptions. A parameter change is
//   picked up at the START of the next process() call (block-boundary
//   application, not sample-accurate): the setters only store targets, and the
//   per-sample machinery reads them inside process(). Simultaneous
//   multi-parameter updates made between process() calls are therefore all
//   applied atomically at that boundary -- no partial states are observable.
//   The only quantity with an explicit pending stage is the per-voice base
//   delay, applied to the voice state at the top of process(). Per-voice
//   ratio changes ride the per-voice geometric glide; large ratio jumps are
//   handled by that same glide.

#pragma once

#include <cstddef>
#include <cstdint>

namespace octavox {

/// Interpolation for the fractional delay reads.
enum class Interpolation : int {
    Linear = 0,     ///< 2-point linear (the reference CLI's "linear")
    Lagrange6 = 1,  ///< 6-point Lagrange, 2048 bin-centre phases (v2 default)
    Lagrange6Q = 2, ///< 6-point Lagrange, 256 bin-centre phases ("lagrange6_q")
    ObsKernel6 = 3, ///< target's observed static 256x6 .rdata kernel (OBS data)
};

/// Engine configuration (all values validated by prepare()).
struct Config {
    double sample_rate = 48000.0;
    double ratio = 1.0;               ///< target pitch ratio (1.0 = unity)
    int voices = 1;                   ///< active voices (1..max_voices)
    int max_voices = 8;               ///< allocated voices
    bool spread = true;               ///< musical spread table for voices > 1
    double feedback = 0.0;            ///< wet feedback in (-1, 1), 0 = off
    Interpolation interpolation = Interpolation::Lagrange6;
    double grain = 4096.0;            ///< G, samples (delay-line grain)
    double base_delay = 256.0;        ///< first voice base delay, samples
    double fmin = 55.0;               ///< spacing-accept gate (Hz)
    double fmax = 1200.0;             ///< spacing-accept gate (Hz)
    double s_hysteresis = 0.005;      ///< S commit hysteresis (relative)
    int f0_history = 8;               ///< period median window
    int detection_interval = 64;      ///< control-rate period, stream samples
    double engagement_seconds = 0.010;///< unity enter/exit morph time
    double gain_ramp_seconds = 0.015; ///< per-voice gain/mute ramp (linear
                                       ///< in amplitude; validated >= 0)
    double bypass_seconds = 0.010;    ///< bypass enter/exit crossfade time
                                       ///< (validated >= 0)
    /// Geometric glide multiplier per sample (reference parity: 0.9995).
    static constexpr double kGlideMult = 0.9995;
};

/// Per-voice diagnostics (snapshot after processing).
struct VoiceStats {
    long wraps = 0;    ///< grain-phase wraps
    long commits = 0;  ///< tap-spacing commits (wrap commits + unity-exit ones)
    int engaged = 0;   ///< 1 = normal dual-tap voice, 0 = unity single-tap
    int parked = 0;    ///< 1 = parked on an integer delay (bit-exact unity)
    double spacing = 0.0;      ///< current S
    double phase = 0.0;        ///< current frac
    int parked_delay = -1;     ///< d_park when parked, else -1
    double ratio = 0.0;        ///< current gliding ratio (diagnostics; round-14
                               ///< recovery: exposes the geometric glide for
                               ///< the tracker/rate-limiter parity harness)
};

/// One mono engine with 1..max_voices voices (port of the Python
/// AlignedDelayLineShifter + the round-12 unity engagement).
class AlignedDelayLine {
public:
    AlignedDelayLine();
    ~AlignedDelayLine();
    AlignedDelayLine(const AlignedDelayLine&) = delete;
    AlignedDelayLine& operator=(const AlignedDelayLine&) = delete;

    /// Validates the config, allocates all state, resets the engine.
    /// Returns false (and leaves the engine unusable) on invalid input.
    bool prepare(const Config& cfg);

    /// Back to the freshly-prepared state (parameters keep their values).
    void reset();

    // -- parameter updates (audio thread; no allocation, no locks) ---------
    void setRatio(double ratio);      ///< target ratio; exact 1.0 engages unity
    void setVoices(int n);            ///< 1..max_voices (normalisation ramps)
    void setSpread(bool on);
    void setFeedback(double fb);      ///< clamped to (-1, 1)

    // -- round-15 P1 parameter surface (see the header contract above) ----
    /// Voice v's ABSOLUTE target ratio (v in [0, max_voices), ratio > 0);
    /// rides the per-voice geometric glide; overrides the spread table for
    /// that voice until the next setRatio()/setSpread()/prepare().
    void setVoiceRatio(int v, double ratio);
    /// Voice v's linear amplitude gain target (>= 0), ramped over
    /// gain_ramp_seconds; multiplies AFTER the voice normalisation.
    void setVoiceGain(int v, double gain);
    /// Mute voice v: effective gain target 0 via the SAME ramp; the voice's
    /// DSP state keeps running while muted.
    void setVoiceMute(int v, bool mute);
    /// Voice v's base delay in samples (> 0, ring-range validated); applied
    /// at the next process() boundary, re-aligning ONLY voice v.
    void setVoiceBaseDelay(int v, double delay);
    /// Wet/dry mix, wet in [0, 1] (default 1.0 = the 14.0.0 output).
    /// Linear law: out = wet*wet + (1-wet)*dry, dry latency-aligned,
    /// feedback wet-path-only.
    void setWetDry(double wet);
    /// Click-free bypass: crossfades the OUTPUT to/from the dry delayed
    /// signal over bypass_seconds; engine state keeps advancing.
    void setBypass(bool on);

    const Config& config() const { return cfg_; }
    bool ready() const { return ready_; }
    /// Nominal intrinsic latency in samples: base_delay + grain/2 (the
    /// mid-grain read delay every voice starts at). At exact unity the
    /// parked copy appears at sample latencySamples()-1 (reference ring
    /// convention: read(d) = sample d-1 ago); for a shifted voice the output
    /// sample k tracks input time r*(k+1) - latencySamples() before the
    /// first grain wrap -- see the COLD-START ALIGNMENT LAW above.
    int latencySamples() const;
    int voices() const { return active_voices_; }

    /// Process a planar mono block of n samples. in/out may not alias.
    /// Any n >= 0; the control rate follows the stream, not the call size.
    void process(const float* in, float* out, int n);

    const VoiceStats& voiceStats(int v) const;
    /// Last detector estimate (Hz; 0 = none). Diagnostics only.
    double lastEstimateHz() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    Config cfg_{};
    bool ready_ = false;
    int active_voices_ = 1;
};

/// N independent mono engines behind one parameter surface.
/// Channel isolation is structural: engine per channel, own ring/state.
class MultichannelShifter {
public:
    MultichannelShifter();
    ~MultichannelShifter();
    MultichannelShifter(const MultichannelShifter&) = delete;
    MultichannelShifter& operator=(const MultichannelShifter&) = delete;

    bool prepare(const Config& cfg, int channels);
    void reset();
    void setRatio(double ratio);
    void setVoices(int n);
    void setSpread(bool on);
    void setFeedback(double fb);
    // round-15 P1 surface: broadcast to every channel engine (identical
    // semantics per channel; see AlignedDelayLine and the header contract).
    void setVoiceRatio(int v, double ratio);
    void setVoiceGain(int v, double gain);
    void setVoiceMute(int v, bool mute);
    void setVoiceBaseDelay(int v, double delay);
    void setWetDry(double wet);
    void setBypass(bool on);

    bool ready() const;
    int channels() const;
    int latencySamples() const;

    /// Planar multichannel block: in[c] / out[c] are per-channel pointers.
    void process(const float* const* in, float* const* out, int channels, int n);

    const VoiceStats& voiceStats(int channel, int voice) const;

private:
    struct MImpl;
    MImpl* mimpl_ = nullptr;
};

/// Library version string ("octavox-native 15.0.0").
const char* versionString();

}  // namespace octavox
