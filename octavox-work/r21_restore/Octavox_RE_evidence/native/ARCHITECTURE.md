# OCTAVOX Native DSP Core — Architecture Note (Round 12)

This note covers state, memory ownership, threading, channel isolation and
limitations of the round-12 native core (`native/`, C++17). It is part of the
round-12 evidence; the research context is in `OCTAVOX_ROUND12_FINDINGS.md`.

## What this is (and is not)

* **Is**: our own independent implementation of the round-11
  `delayline_aligned` design, ported from `poc/octavox_v2.py` (preserved,
  untouched, as the Python reference baseline), plus the round-12 unity
  engagement fix.
* **Is not**: a VST3 plugin, a wrapper for the original Eventide Octavox, or a
  claim about the original's runtime behaviour. The target was never executed.
  The single exception to "our own code" is documented **data**: the optional
  `Interpolation::ObsKernel6` mode embeds the 256×6 float kernel table
  **observed** in the target's `.rdata` (`src/obs_kernel_table.h`, extraction
  script `evidence/analysis_round12/r12_interp_table.py`) for research
  comparison. That mode is opt-in; the default interpolator is our own
  6-point Lagrange table.

## Object model

```
octavox::AlignedDelayLine      one mono engine, 1..max_voices voices
octavox::MultichannelShifter   N AlignedDelayLine engines, one per channel
```

`MultichannelShifter` owns one `AlignedDelayLine` **per channel**. Channel
isolation is therefore structural: there is no code path that moves samples or
state between engines. This is the native counterpart of the round-11
`process_multichannel` fix (fresh engine per channel) — measured in the suite:
the right channel's output is bit-identical whether or not the left channel is
processed (`stereo_independence`), and exactly zero for a silent input
(`stereo_isolation_*`).

## Lifecycle and memory ownership

| phase | allocates | notes |
|---|---|---|
| `prepare(cfg)` | yes | ring (2^18 doubles = 2 MiB/engine), voice state, detector window + work arrays, control accumulator, median scratch. Total ≈ 2.1 MiB per mono engine (≈ 4.2 MiB stereo). |
| `reset()` | no | zeroes the ring and all state; parameters keep their values. |
| `setRatio/setVoices/setSpread/setFeedback` | no | scalar writes + precomputed per-voice targets. |
| `process()` | **no** | steady state is allocation-free — enforced by the `steady_state_no_allocation` case (global `operator new` counter, 0 allocations across 24 000 samples after warm-up). The only first-touch cost is the shared interpolator table (a C++ magic static built on first use — during warm-up, not per block). |

The engine follows a **single-threaded audio thread** model: no locks, no
atomics, no blocking calls anywhere in `process()`. Parameter updates are
expected to be issued on the audio thread (or under the caller's own
synchronisation) — the same contract as the offline/test usage in this round.

Buffers are `std::vector` members allocated in `prepare()` and reused; the
`Impl` pimpl is heap-allocated once per engine. `process()` writes only into
caller-provided output ranges and engine-owned state.

## Per-voice state

Each voice carries: `ratio_t`/`ratio` (target and geometrically gliding
ratio, glide 0.9995 per sample — reference parity), `base_delay`
(256 + 32·v), `frac` (grain phase), `S` (tap spacing, committed at wraps with
0.5 % hysteresis from the median-smoothed period estimate), the round-12
**engagement pair** `e`/`a` (below), `d_park` (parked integer delay), plus wrap
and commit counters exposed via `voiceStats()`.

The ring is age-indexed with the reference's semantics: a read at delay `d`
returns the sample pushed `d−1` samples earlier (the Python
`DelayLine.read` indexing, kept for parity — see the test expectations).

## The unity engagement (round-12 fix)

The Python reference blends two taps continuously; at exact unity the blend
phase freezes at w = 0.5, producing the measured comb (−2.42 dB at 220 Hz;
16.4 dB spread across a tone battery). Static round-12 evidence (Finding 3)
shows the target rests on a **single** interpolated tap and only crossfades
transiently after a re-arm. The native core mirrors that architecture:

* Per voice, when the target ratio is **bit-exactly 1.0**, the blend
  parameter `e` slews 1→0 over `engagement_seconds` (default 10 ms) so the
  output morphs from the two-tap blend to tap A alone (`wA_eff = e·w + 1−e`,
  `wB_eff = e·(1−w)`; the weight sum stays 1 — no gain bump).
* Once `e` reaches 0, the parking parameter `a` slews 0→1, morphing tap A
  (fractional read) into `A_park` = a read at the **nearest integer delay**
  inside `[base, base+S)`. Integer delays take the direct sample path, so the
  parked state is a **bit-exact delayed copy**: `out[k] == in[k − d_park + 1]`
  (the +1 is the reference ring indexing). The suite gates this to exactly
  zero difference for tones, noise, chords, silence, −60 dB-level input, and
  an engine that arrives at unity from +400 cents.
* Leaving unity (any ratio ≠ 1.0 — including ±1 cent) reverses the morphs.
  At the departure instant the engine also **refreshes the tap spacing**
  `S ← S_target` when the read point can be preserved exactly
  (`frac_new = frac·S_old/S_new ∈ [0,1)`), which re-aligns the taps for
  periodic input immediately (measured: gain within ±0.001 dB after a
  unity→±50-cent departure, vs the −2.4 dB static comb of a fresh
  near-unity engine).
* There is **no bypass region**: `e_target` keys on bit-exact `ratio_t == 1.0`
  only. The `no_bypass_region` case verifies ±1 cent really shifts the pitch.

## Control rate and block-size invariance

The pitch detector (YIN-style CMND, 2048-sample window — the Python reference
detector, ported) fires every `detection_interval` **stream** samples, so the
output is invariant to how the caller splits the stream into blocks
(`block_size_invariance` gates this to exactly 0 difference across block sizes
1/7/16/64/128/512/1024/4096 and a mixed pattern). The Python reference
restarts its control loop at every `process()` call and is therefore **not**
invariant — an intentional, documented improvement.

Documented delta vs the Python: the reference estimates with the full
64-sample block in the detector window *before* processing that block; a
streaming API that writes every output on arrival cannot reproduce that
look-ahead, so with aligned calls the native control timing lags the Python's
by at most one interval (64 samples). Class-level behaviour (pitch accuracy,
modulation, alignment) is unaffected — measured in the cross-language metrics
(worst pitch error 0.50 cents across the cent battery vs the Python v2's
0.08 cents at the same settings; both far inside the 2-cent gate).

## Threading

One engine = one thread. `MultichannelShifter::process` processes channels
sequentially on the calling thread; the engines are independent and could be
parallelised by the host (not done — out of scope this round). The shared
interpolator tables are immutable after construction (thread-safe magic
static). `voiceStats()` returns a reference to per-thread scratch — copy the
value if you need it across calls.

## Limitations (honest list)

* The YIN detector's 2048-sample window cannot see 60 Hz at 96 kHz
  (`tau_max` exceeds the window), so at that rate the spacing falls back to
  `G` — identical to the Python reference at the same settings (measured
  pitch error −13.8 cents at +400 cents/96 kHz, gated loosely and reported).
  At 44.1/48 kHz the detector works (0.18-cent error measured).
* The detector dominates the cost at the default 64-sample control interval
  (~73 % of runtime): full-rate mono runs at ≈ 15× realtime, while the voice
  path alone (control interval 512) runs at ≈ 51× realtime. This mirrors the
  reference design; a decimated or incremental detector is future work.
* Near-unity engines that never touched unity keep the static two-tap comb
  for periodic input until the first wrap commits an aligned spacing (the
  v2's documented limitation); the unity-exit commit removes it for
  departures from exact unity. Measured and reported, not gated away.
* Multi-voice unison at unity (voices > 1, spread off) parks each voice at
  its own integer delay, so the mix is an exact 8-tap FIR sum of delayed
  copies — each voice is transparent, the mix is a legitimate unison
  thickening (gated to the exact mean in `unity_8voice_unison_exact_mix`).
* Feedback at unity feeds the single-tap path — a stable feedback comb for
  |fb| < 1 (documented behaviour, not transparency).
* No dither in the CLI's 16-bit writer (reference parity), and the write/read
  asymmetry (round(y·32767) then ÷32768) is the reference's own convention.
* `setVoices` ramps the voice-count normalisation over
  `engagement_seconds`; the underlying per-voice gains do not ramp (the v2
  has no gain ramps in the aligned engine — parity).

## Build and test

See `native/README.md`. Release tests: 85/85 (51 frozen round-12 + 19
R13-recovery + 7 P1 + 7 r15acc acceptance); ASan+UBSan build: 85/85 with
zero sanitizer reports; release benchmark (12 cases) and the acceptance
results are machine-readable (`evidence/analysis_round15/`).
[R16-P1 correction, 2026-10-28: previously "51/51" (round-12 state) —
superseded to the delivered 15.0.0 counts.]

## Round-15 parameter surface (15.0.0)

The normative contract is the "ROUND-15 PARAMETER SURFACE" comment block in
include/octavox/dsp.hpp. Mechanisms (src/dsp.cpp): VoiceState carries
ratio_override/ratio_ov, gain_t/gain/ramp_tgt/gain_rate, mute,
base_delay_ov/base_pending; Impl carries the dry ring (pushDry/readDry; dry
tap at latencySamples() under the read(d)=d−1 convention), wet, bypass
blend state (bypass_on/bypass_b/b_step), gain_ramp_n, dry_delay.
`applyPendingSurface()` applies pending base-delay re-alignments and arms
gain ramps at process() block starts; `slewGain()` is the linear-in-amplitude
ramp with exact snap; `resetParamSurface()` restores fresh-engine defaults
on prepare (reset preserves overrides and snaps ramps). The output stage
special-cases `wet==1 && !bypass` as the bit-exact legacy path — the
neutrality guarantee (four reference hashes, native/r15_reference_hashes.txt).
R13-b detector window law: `win = max(2048, ceil(2.5·τmax))`, τmax = sr/fmin
(the round-12 fixed 2048 window was blind at 96 kHz).
