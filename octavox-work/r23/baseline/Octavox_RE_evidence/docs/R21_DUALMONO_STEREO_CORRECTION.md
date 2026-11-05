# R21 — Corrected Dual-Mono / Stereo / Voice-Delay Interpretation

**Supersedes the R20 interpretation** (r20_dualmono.cpp's inaccurate "equal
default base delays" comment — corrected in place this round) and completes the
R21-specified deterministic matrix. New evidence:
`evidence/analysis_round21/r21_stereo.json` + `logs/r21_stereo_stdout.txt`
(octavox_r21_stereo: 2 rates × 2 interpolators × 8 scenarios + the WAV option).

## The four distinguished behaviour classes (all measured)

**(1) Exact dual-mono transparency.** Identical L/R input + identical
per-channel state → **bit-identical L/R output in every configuration**
(1/2/8 voices × spread on/off × default/equal delays × both interpolators ×
48/96 kHz — the gate "dual-mono transparency violations: 0"). Channel isolation
is structural (one engine per channel). No widening exists and none was added.

**(2) Same-pitch voices with different base delays.** With the Harmony Map OFF
and all Transposes 0, every voice sounds the SAME PITCH (V1_RATIO = 1.00000
measured), but the core's DEFAULT base-delay table `256 + 32·v` spaces
adjacent voices by exactly **32 samples** (measured inter-voice lag via
voice-isolation cross-correlation: **32.0 samples** = 0.667 ms @48k /
0.333 ms @96k) — a TEMPORAL inter-voice doubling inside the Haas fusion
window, with NO L/R divergence. With delays FORCED equal the measured lag is
**0.0** — the doubling vanishes (true unison doubling, amplitude-only).

**(3) Different-pitch voices via the Harmony Map.** With the Harmony Map ON and
zero explicit Transposes, voice 2 follows the spread table (+7 semitones:
measured V1_RATIO = 1.49831 = 2^(7/12)) — still with the 32-sample inter-voice
base-delay spacing, still bit-identical L/R. (The cross-correlation lag metric
is only meaningful for same-pitch scenarios; with a +7st pitch difference the
correlation peak reflects harmonic alignment, not the delay — recorded.)

**(4) True L/R divergence — only from non-identical channel input.**
1-sample R input offset → NOT bit-identical (maxdiff 0.0163 @48k, RMS −23 dB,
side/mid −29 dB) — the per-channel trackers diverge in grain timing; 0.1%
amplitude imbalance → −60 dB RMS; decorrelated stereo → −3.9 dB RMS /
−9.9 dB side/mid. Real stereo recordings (via --input WAV) fall in this class.

## The reconciliation with the user's Haas observation

The user-observed "second zero-offset voice doubles the effect" = **class (2)
+ (3) combined**: the default base-delay law (256+32·v) makes the second voice
a 32-sample-delayed copy (temporal doubling in the Haas fusion window), and
with the Harmony Map ON (the default) it is ALSO +7 semitones. It is an
INTER-VOICE temporal mechanism — the L/R channels remain bit-identical for
identical input. A Haas-like L/R effect requires class (4) — non-identical
channels (e.g. a 1-sample input offset through independent per-channel
trackers).

## The defaults, precisely

`base_delay = 256 + 32·voice_index` samples (voice 1 = 256, voice 2 = 288, …);
adjacent-voice spacing 32 samples = 0.667 ms @48 kHz / 0.333 ms @96 kHz.
Spread (Harmony Map) is ON by default: two zero-offset voices = voice 1 unison
+ voice 2 at +7 semitones — **two zero-offset voices do NOT produce two
unison signals.**

## The optional linked-stereo tracker — evaluation (design, not implemented)

Design: L/R audio processing stays fully separate (rings, voices,
interpolators, output — preserving the exact dual-mono transparency of class
1), while the PITCH-CONTROL estimate is shared: one tracker fed (e.g.) the
mid signal (or the louder channel), its committed estimate fanning to both
channels' spacing laws.

* **What it would fix:** the class (4) divergence from small channel
  differences — the 1-sample-offset scenario's −23 dB L/R RMS difference and
  the tracker commit-count divergence would vanish (both channels' voices
  would commit spacing changes at identical sample positions).
* **What it costs:** estimate COUPLING on anti-phase or genuinely decorrelated
  stereo material — the tracker would see a mid signal whose pitch can differ
  from either channel (e.g. a hard-panned dual-mono source mix); the
  per-channel independence that class (4) demonstrates is a real behaviour,
  not a defect.
* **Decision:** NOT implemented; NOT the default. The measured basis is
  recorded (class 4's magnitude); introducing it requires controlled evidence
  that coupling is preferable — a future round's decision with its own
  battery. No artificial stereo widening was introduced at any point.
