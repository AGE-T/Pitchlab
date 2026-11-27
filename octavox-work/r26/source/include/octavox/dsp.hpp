// OCTAVOX independent native DSP core (public API) -- version 16.0.0
// ============================================================================
// [R19 note, 2026-10-31: version 16.0.0 -- the round-19 real-time/click
// round. The 15.0.0 contract below is preserved VERBATIM; the round-19
// additions (decimated control-rate detector with the FullRate reference
// mode, click-free spacing commits, voice activation/deactivation ramps,
// crossfaded per-voice base-delay changes, the bypass/wet==0 read
// suppression and the stage-attribution profiler) are specified in the
// ROUND-19 CONTRACT block further below. Every 15.0.0 law that round 19
// changes is config-gated and the LEGACY configuration reproduces the
// 15.0.0 output BIT-EXACTLY (gated by the four r15 reference hashes, which
// are re-captured every round since R15-REB1).]
// [R16-P1 note, 2026-10-28: the title line previously read "rounds 13/14
// recovery"; superseded — this header is the normative contract. The
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
//
// ROUND-19 CONTRACT (16.0.0) -- real-time, clicks, and the legacy gate
// ---------------------------------------------------------------------
// (a) DETECTOR (see DetectorMode above). The estimate cadence (every
//     `detection_interval` STREAM samples), the median-history law, the
//     hysteresis/commit law and the [60,1200] Hz validity gate are IDENTICAL
//     in both modes; only the estimate VALUES differ (the decimated window
//     sees a low-passed waveform at a coarser lag resolution -- measured
//     accuracy/tracking deltas are recorded in evidence/analysis_round19).
//     detector_mode is buffer-determining for re-prepare (a mode change
//     reconfigures the detector, so it takes the allocate-and-rebuild path).
// (b) CLICK-FREE SPACING COMMITS: at a grain wrap whose hysteresis gate
//     fires, the engine snapshots the voice's read state into a frozen
//     SHADOW (S frozen at the old value; the shadow never commits) and
//     crossfades shadow -> new law over commit_crossfade_seconds (clamped
//     to 40 percent of the wrap period; a fully parked voice needs no
//     crossfade because its integer read is S-independent). The blended
//     output is continuous at the commit instant BY CONSTRUCTION (weight 0
//     on the new law at the boundary). commit_crossfade_seconds == 0
//     restores the legacy instant commit bit-exactly.
// (c) VOICE ACTIVATION/DEACTIVATION RAMPS: setVoices(n) activates voices
//     [0,n) and deactivates [n,max); each affected voice's EFFECTIVE gain
//     target (active && !mute ? user gain : 0) ramps over
//     gain_ramp_seconds through the existing linear ramp machinery, while
//     the voice-count normalisation ramps as before. A deactivated voice
//     keeps processing (DSP state stays warm) until its ramped gain reaches
//     exactly 0; re-activation is seamless by the same ramp. The settled
//     state is bit-identical to 15.0.0 (gains and norm reach their exact
//     targets); only the transition is new.
// (d) CROSSFADED BASE-DELAY CHANGES: a pending per-voice base delay now
//     blends the voice's read between the old-base and new-base read laws
//     over delay_crossfade_seconds (0 = the legacy instant re-align,
//     bit-exact). The final state equals the legacy law exactly.
// (e) READ SUPPRESSION (bypass / zero wet): when the feedback coefficient
//     is 0 AND the output is exactly the dry signal (wet == 0 with no
//     bypass blend running, or the bypass blend has fully reached 1), the
//     engine skips the interpolated READS while advancing every piece of
//     voice state, the detector, the rings and all slews. With fb == 0 the
//     suppression is BIT-IDENTICAL by construction (the rings receive the
//     same input, voice state evolution is read-independent, and the
//     output stage ignores the wet sample) -- gated by an explicit
//     bit-identity test. With fb != 0 the suppression never engages.
// (f) LEGACY BIT-EXACTNESS: the configuration {detector_mode = FullRate,
//     commit_crossfade_seconds = 0, delay_crossfade_seconds = 0,
//     voice_fade_seconds = 0} (the reference-hash configs perform no
//     voice-count transitions, so their records are unaffected either way)
//     reproduces the 15.0.0 output BIT-EXACTLY, gated by the four r15
//     reference hashes and the 85-case suite's behavioural gates.

#pragma once

#include <cstddef>
#include <cstdint>

#include "octavox/pitch_control.hpp"  // R31: ControlState (telemetry observers)
#include "octavox/simd.hpp"

namespace octavox {

/// Interpolation for the fractional delay reads.
enum class Interpolation : int {
    Linear = 0,     ///< 2-point linear (the reference CLI's "linear")
    Lagrange6 = 1,  ///< 6-point Lagrange, 2048 bin-centre phases (v2 default)
    Lagrange6Q = 2, ///< 6-point Lagrange, 256 bin-centre phases ("lagrange6_q")
    ObsKernel6 = 3, ///< target's observed static 256x6 .rdata kernel (OBS data)
};

/// Pitch-detector architecture (R19). Both modes implement the same
/// YIN-family estimator (identical threshold law, cumulative-mean
/// normalisation, parabolic refinement and the [60,1200] Hz validity
/// gate); they differ in the rate at which the analysis window is sampled.
enum class DetectorMode : int {
    /// Full-rate reference detector (the R12--R18 behaviour, verbatim):
    /// the sliding window holds RAW input samples at the host rate and the
    /// analysis window follows the R13-b law win = max(2048, ceil(2.5*tau))
    /// with tau at the host rate. Retained unchanged as the reference mode.
    FullRate = 0,
    /// R19 DEFAULT: a properly low-pass filtered, decimated control-rate
    /// detector. The input is filtered by a 6th-order Butterworth low-pass
    /// (three TDF2 biquads, fc = 0.35 * sr_dec) and decimated by D to a
    /// control rate near 24 kHz (D = max(1, lround(sr/24000)); at the
    /// canonical rates 44.1/48/88.2/96/176.4/192 kHz this is 2/2/4/4/7/8,
    /// giving sr_dec in [22050, 25200]). The analysis window then holds
    /// DEcimated samples with the time-equivalent law
    ///     win_dec = max(1024, ceil(2.5 * tau_dec)),  tau at sr_dec,
    /// which preserves the R13-b difference-support immunity
    ///     m = win/2 - tau_max >= tau_max/4
    /// at every rate and keeps the SAME ~42.7 ms window TIME SPAN and the
    /// SAME detection-update cadence (one estimate every
    /// `detection_interval` STREAM samples) as the full-rate detector at
    /// 44.1/48 kHz. The audio path (ring, voices, interpolators, output)
    /// always runs at the host's original sample rate; only the detector's
    /// input is filtered and decimated. Valid detection range stays
    /// 60..1200 Hz. Cost at 96 kHz drops by ~14x (see
    /// evidence/analysis_round19 for the measured profile).
    Decimated = 1,
    /// R20-P2: coarse-to-fine hybrid. Stage 1 (COARSE) runs the EXACT
    /// decimated estimator above (same anti-alias cascade, same decimated
    /// window, same threshold/search law, same d-based parabolic refine)
    /// and produces a candidate period tau_dec at the control rate. The
    /// candidate is mapped back to the HOST stream grid:
    ///     tau_c = tau_dec * D   (stream samples).
    /// Stage 2 (FINE) evaluates the ORIGINAL, UNFILTERED full-rate signal's
    /// difference function in a candidate neighborhood
    ///     [tau_c - W, tau_c + W],  W = D + 2  host samples,
    /// (clamped into the [60, 1200] Hz lag domain), takes the argmin and
    /// refines it with a 3-point parabola on the RAW difference values --
    /// full host-rate lag resolution. The refinement window support uses
    /// the full-rate window law win_raw = max(2048, ceil(2.5*tau_max)) at
    /// the HOST rate with m_raw = win_raw/2 - tau_max difference support
    /// (the FullRate detector's own law), so both stages see the SAME
    /// ~42.7 ms analysis time span and the estimate cadence, the median
    /// history, the hysteresis/commit law and the validity gate are
    /// IDENTICAL to the other two modes. The coarse candidate's accuracy
    /// (<= ~0.2 dec-lag ~ 0.2*D host samples after its own parabolic
    /// refine) is fully covered by the W = D+2 half-width; a pathological
    /// waveform whose decimated argmin differs from the full-rate argmin
    /// by more than W locks the refinement to the LOCAL minimum near the
    /// candidate (an honest, measured behavioural difference vs FullRate,
    /// not a defect). Cost: the decimated coarse stage + ~(2W+1)*m_raw
    /// MACs -- at 96 kHz roughly 12% over Decimated and ~9x under
    /// FullRate (measured; see evidence/analysis_round20).
    Hybrid = 2,
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
    double fmin = 55.0;               ///< spacing-accept gate lower bound (Hz;
                                       ///< see spacingGateHz() for the R22
                                       ///< UNIFIED effective bound)
    double fmax = 1200.0;             ///< spacing-accept gate (Hz)
    /// R21: the DETECTOR's lower validity gate and lag-domain floor (Hz).
    /// Distinct from fmin/fmax (the ENGINE's spacing-accept gate on the
    /// tracker's committed estimate): f0_min sizes the detector's lag
    /// domain (tau_max = int(rate / f0_min), both the full-rate window law
    /// win = max(2048, ceil(2.5*tau_max)) and the decimated law
    /// win_dec = max(1024, ceil(2.5*tau_dec))) and lower-bounds the
    /// estimate validity gate [f0_min, 1200] Hz. Default 60.0 = the
    /// R12-R20 fixed law BIT-EXACTLY (every existing hash and behaviour
    /// unchanged). Buffer-determining for re-prepare (a change re-sizes the
    /// detector windows). Validated into [20, 200] Hz and < fmax. NOT
    /// serialized in the VST3 state blob (v2 layout frozen; a restored
    /// session keeps the default -- the knob is an engine-configuration
    /// API for the low-note investigation harness, not a host parameter).
    /// R22-P1 (Minimum Pitch signal path, level 4 unification): the
    /// ENGINE's spacing-accept lower bound applied to the tracker's
    /// committed estimate is the UNIFIED floor
    ///     spacingGateHz() = max(fmin, f0_min)
    /// (see AlignedDelayLine::spacingGateHz()). PROOF OF BIT-EXACTNESS vs
    /// the R12--R21 law (gate = fmin alone): every NONZERO committed
    /// estimate already satisfies f0 >= f0_min (the detector zeroes
    /// anything below its own floor), so for any fmin <= f0_min the two
    /// gates accept exactly the same estimates -- lowering fmin below the
    /// detector floor has ALWAYS been dead configuration, and the default
    /// (fmin 55 < f0_min 60) is unchanged. The unification makes the
    /// dominance EXPLICIT: the engine can never track spacing on a period
    /// the detector refuses to commit. THE DISTINCTION (detectable vs
    /// spacing-trackable) survives at the other edge: when f0_min is
    /// LOWERED below fmin (e.g. 40 Hz), the detector commits tones in
    /// [f0_min, fmin) that the engine STILL refuses to lock spacing onto
    /// (the engine's own floor fmin governs -- a tone may be detectable
    /// yet not safely spacing-trackable); lastEstimateSpacingAccepted()
    /// exposes the decision per estimate.
    double f0_min = 60.0;
    double s_hysteresis = 0.005;      ///< S commit hysteresis (relative)
    int f0_history = 8;               ///< period median window
    int detection_interval = 64;      ///< control-rate period, STREAM samples
    /// R19: detector architecture (see DetectorMode). Default = the
    /// decimated control-rate detector; FullRate reproduces the R12--R18
    /// reference behaviour bit-exactly. Buffer-determining for re-prepare.
    DetectorMode detector_mode = DetectorMode::Decimated;
    /// R23 (Gate 8): enable the Pitch Control Layer as the spacing-gate
    /// front end (confidence/outlier/octave gating BEFORE the engine's
    /// 8-entry period median -- integration Strategy A, see
    /// PITCHLAB_R23_TRACKING_CONTROL_SPEC.md). Default FALSE = the legacy
    /// R12-R22 accept law BIT-EXACTLY (every default-config hash and the
    /// 100/100 suite unchanged). Engine-internal configuration (NOT a host
    /// parameter; NOT serialized in the VST3 state blob -- a restored
    /// session keeps the default, exactly like f0_min).
    bool tracking_gate = false;
    /// R24 (SIMD round): the SIMD acceleration level for the YIN kernels
    /// (difference function + window peak; see include/octavox/simd.hpp for
    /// the full numerical-semantics and RT-safety contract). Default
    /// Scalar = the engine output is BIT-EXACT against every recorded
    /// reference hash of the R12--R23 record (the accelerated levels are
    /// OPT-IN). AVX2 resolves to the lag-axis kernel, which is bit-identical
    /// to the scalar reference by construction (gated by a hard exact-
    /// equality battery); AVX2+FMA additionally fuses the per-term rounding
    /// (order preserved; gated as a bounded deviation). An explicitly
    /// FORCED level (AVX2 / AVX2FMA) that the processor or build cannot
    /// honour is REJECTED by prepare() (loud failure; only Auto degrades
    /// silently). Engine-internal configuration: NOT a host parameter and
    /// NOT serialized in the VST3 state blob (a restored session keeps the
    /// default -- exactly like f0_min / tracking_gate; the state blob stays
    /// the frozen v2 layout).
    simd::Mode simd = simd::Mode::Scalar;
    double engagement_seconds = 0.010;///< unity enter/exit morph time
    double gain_ramp_seconds = 0.015; ///< per-voice gain/mute ramp (linear
                                       ///< in amplitude; validated >= 0)
    double bypass_seconds = 0.010;    ///< bypass enter/exit crossfade time
                                       ///< (validated >= 0)
    /// R19: click-free spacing-commit crossfade (seconds, >= 0; validated
    /// exactly like the other time fields). When a grain wrap commits a new
    /// tap spacing S, the engine crossfades from the old grain law (a frozen
    /// shadow voice) to the new law over this duration instead of stepping
    /// the read position (the 15.0.0 step was the dominant click source:
    /// every initial spacing lock and every retune produced a step of
    /// (S_new - S_old) samples in read position). 0 = the legacy instant
    /// commit (bit-exact 15.0.0 behaviour, clicks included). The length is
    /// additionally clamped to 40 percent of the current wrap period so a
    /// second commit can never begin while a crossfade is still running.
    double commit_crossfade_seconds = 0.008;
    /// R19: crossfaded per-voice base-delay changes (seconds, >= 0). A
    /// pending per-voice base delay is applied by crossfading the voice's
    /// read between the old and the new base delay (the voice's grain
    /// phase/spacing are shared) instead of translating the read point
    /// instantly; 0 = the legacy instant re-align (bit-exact 15.0.0). The
    /// eventual state (base delay, phase, spacing, park position) is
    /// IDENTICAL to the legacy law; only the transition differs.
    double delay_crossfade_seconds = 0.012;
    /// R19: voice activation/deactivation fade (seconds, >= 0; validated
    /// like the other time fields). setVoices() transitions ramp the
    /// affected voices' contributions over this duration through the
    /// per-voice gain ramp machinery (the settled state is bit-identical
    /// to 15.0.0). 0 = the legacy INSTANT activation/deactivation
    /// (bit-exact 15.0.0, including its voice-count transition step: new
    /// voices entered at full amplitude while the normalisation had not
    /// yet ramped).
    double voice_fade_seconds = 0.012;
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
    long last_wrap_at = -1;    ///< R19: stream sample index of the most recent
                               ///< grain wrap (-1 = none yet; diagnostics)
    long last_commit_at = -1;  ///< R19: stream sample index of the most recent
                               ///< tap-spacing commit (-1 = none; diagnostics)
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
    /// R24: the SIMD level resolved by the LAST successful prepare()
    /// (diagnostics only; Level::Scalar whenever the configured mode
    /// resolved to the scalar kernels -- the default configuration).
    /// Returns Scalar when the engine is not prepared.
    simd::Level simdLevel() const;

    // -- R22-P1: Minimum Pitch signal-path diagnostics ----------------------
    // The 8-level chain (detector lag domain -> estimate validity ->
    // committed estimate -> ENGINE spacing gate -> period history ->
    // grain spacing S -> read position -> voice pitch ratio) is now
    // observable per engine. Levels 1-3 live in the detector
    // (lastEstimateHz); levels 4-5 here; levels 6-8 via voiceStats() and
    // the (estimate, ratio) product the caller computes.
    /// The ENGINE's effective spacing-accept lower bound (Hz) =
    /// max(config().fmin, config().f0_min) -- the R22 unified floor (see
    /// Config::f0_min's bit-exactness proof). Diagnostics only.
    double spacingGateHz() const;
    /// Current spacing target S_target in samples (= config().grain while
    /// no estimate has been accepted into the period history). Diagnostics
    /// only.
    double spacingTargetSamples() const;
    /// Whether the LAST committed detector estimate ALSO passed the
    /// ENGINE's unified spacing gate [spacingGateHz(), fmax] -- the
    /// explicit "detectable (lastEstimateHz() > 0) vs spacing-trackable
    /// (this flag)" distinction. False when the last estimate was 0 or
    /// was rejected by the engine floor (e.g. f0_min lowered below fmin:
    /// tones in [f0_min, fmin) are detectable but NOT spacing-trackable).
    /// Diagnostics only.
    bool lastEstimateSpacingAccepted() const;
    /// Accepted periods currently in the median history (0..f0_history).
    /// Diagnostics only.
    int spacingHistoryCount() const;

    // -- R31: m02-style telemetry observers (read-only) --------------------
    // The R26-R30 static audit fully decoded the original's m02 pitch-
    // correction telemetry (ids 7..13) and its per-voice block-rate
    // poller/dirty-bit fanout (R28 F1.6, R30 F2). These three observers
    // expose the PitchLab-side ingredients so the SDK-free telemetry
    // component (include/octavox/telemetry.hpp) can serve the equivalent
    // readouts. PURE OBSERVERS: they change no arithmetic (the 110-case
    // suite and the four reference hashes gate this round).
    /// CMNDF value at the selected minimum of the LAST estimate
    /// (1.0 = no dip/degenerate, 0 = perfect periodicity). The detector's
    /// own confidence ingredient (= 1 - periodicity). Diagnostics only.
    double lastEstimateCmndfMin() const;
    /// The R23 Pitch Control Layer's state for the LAST frame (the public
    /// ControlState enum; kNoLock when the tracking gate is off).
    ControlState lastControlState() const;
    /// The control layer's validated+smoothed tracked Hz (0 = none).
    /// Distinct from the raw detector estimate (lastEstimateHz): this is
    /// the post-gate value the spacing path consumes when locked.
    double trackedHz() const;

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

    // -- R31: telemetry observers, delegated to channel 0 (the reference
    // engine; per-voice DSP state stays per-channel via voiceStats()).
    double lastEstimateHz() const;
    double lastEstimateCmndfMin() const;
    ControlState lastControlState() const;
    double trackedHz() const;
    bool lastEstimateSpacingAccepted() const;

private:
    struct MImpl;
    MImpl* mimpl_ = nullptr;
};

/// Library version string ("octavox-native 16.0.0").
const char* versionString();

}  // namespace octavox

// ---------------------------------------------------------------------------
// ROUND-19 STAGE ATTRIBUTION PROFILER (opt-in, compile-time gated)
// ---------------------------------------------------------------------------
// The core can be compiled with -DOCTAVOX_STAGE_TIMERS to accumulate
// per-stage cycle counters (detector buffering / estimate / median update,
// per-voice loop, interpolation reads, output stage, input+feedback stage).
// Collection is a pure observer: it never touches the DSP data flow, and
// the plain (macro-off) build is bit-identical to a build whose sources
// never contained the instrumentation. See include/octavox/stageprof.hpp.
// ---------------------------------------------------------------------------
#include "stageprof.hpp"
