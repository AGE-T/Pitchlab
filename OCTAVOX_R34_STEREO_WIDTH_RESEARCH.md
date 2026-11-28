# OCTAVOX / PitchLab — R34 RESEARCH RECORD: Stereo image width with energy preservation

**Round:** R34 (own design; no new RE claims — the original binary was never
executed and the original plug-in has no width control on the reconstructed
38-parameter surface).
**User mandate:** "itt a chain végére szeretnék kérni egy új funkciót.
Szeretném, ha a stereo kép szélességét lehetne állítani akár teljesen monóra
összehúzhatóvá kellene tenni. A twist itt az lenne, hogy közben a hang
energiáját meg kellene tudni tartani. Kérlek első körben menj utána annak,
hogy milyen lehetőségek vannak erre. Majd implementáljunk egyet."
(A new feature at the END of the chain: adjustable stereo image width,
collapsible all the way to mono, WITH the sound's energy maintained.
Research the options first, then implement one.)

This document is the requested options research (Part A) and the decision
record. The implementation and its verification live in
OCTAVOX_R34_STEREO_WIDTH.md (the round report).

---

## 0. Where "the end of the chain" is

The wrapper's DSP chain (see ARCHITECTURE.md / VST3_WRAPPER.md) is:

```
host in (L,R)
  -> MultichannelShifter            [one AlignedDelayLine MONO engine
       per channel; channel isolation is structural]
       per engine: input+feedback -> detector (control rate) -> spacing
       commits -> per-voice grain reads (interpolation) -> voice gains
       -> wet/dry mix (latency-aligned dry) -> bypass crossfade
  -> out (L,R)                        <-- THE CHAIN END: this round's
                                         insertion point
```

The width stage is inserted at the `MultichannelShifter::process()` output —
AFTER the per-channel engine loop, BEFORE the buffers return to the host.
Consequences:

* The mono `AlignedDelayLine` engines are NOT touched; the four r15
  reference-hash gates and every existing mono-path case are structurally
  unaffected.
* The stage sees the FINAL mix (wet voices + aligned dry), which is the
  signal whose image the user hears — the only correct place for an
  image-wide operation.
* The stage must be BIT-NEUTRAL at the default (width = 1) — an explicit
  inert early-out, the same discipline as the R31 observers.

Note a decisive property of THIS plugin: with a mono source (identical L/R
input) the engine output is exactly dual-mono (`stereo_isolation_unity` and
`stereo_isolation_shifted` pin L==R bitwise). Whatever law we choose must
collapse THAT content at unity gain, not at +3 dB.

---

## 1. The fundamental constraint (why "energy-preserving width" is a real
   design problem and not a formula)

Work in the orthonormal mid/side decomposition

```
M = (L+R)/sqrt(2),   S = (L-R)/sqrt(2)        (energy-preserving: M^2+S^2 = L^2+R^2)
```

A width control is a NON-UNITARY rescale of S:

```
S -> w*S,   w in [0,1]   (w=1: original image, w=0: full mono collapse)
```

Every such rescale (w != 1) changes the total energy for generic content:
the map is not an isometry, and no FIXED gain can restore the energy of
every possible signal simultaneously (a fixed gain that is right for one
correlation is wrong for another — see the table below). The only exact
per-sample energy-preserving 2x2 maps are rotations/reflections
(orthogonal matrices), and a rotation CANNOT produce mono at any angle
(rotating the L/R plane never aligns the two channels; only a rank-1
collapse does, and rank-1 is not orthogonal).

**Therefore energy preservation necessarily picks ONE of:**

1. a statistical assumption about the content (correlation-blind laws), or
2. a measurement of the actual content (adaptive gain), or
3. a per-sample normalization (a time-varying distortion).

This is the actual trade-off that separates the candidate options.

## 2. The energy law (the working math for the rest of the document)

Let the stage's input be stationary with total energy
`E = E[L^2] + E[R^2]` and (equal-power-normalized) correlation

```
rho = 2*E[LR] / (E[L^2]+E[R^2])      in [-1, +1]
```

(`rho = +1`: identical channels / fully correlated; `0`: uncorrelated;
`-1`: anti-correlated / out of phase.) In the orthonormal M/S frame:

```
E[M^2] = E*(1+rho)/2        E[S^2] = E*(1-rho)/2
```

After the width rescale (side gain w, mid gain k — the MID-ONLY-GAIN
structure the implementation uses; see the correction note below):

```
E_out = k^2 * E[M^2] + w^2 * E[S^2]
      = k^2 * E * (1+rho)/2 + w^2 * E * (1-rho)/2
```

so the UNIQUE gain that preserves total energy is

```
k(w, rho) = sqrt( (2 - w^2*(1-rho)) / (1+rho) )
```

**R34 CORRECTION (recorded honestly; caught by the r34_energy_uncorrelated
gate at w=0.5, exactly the class of error the verification plan exists to
catch):** the first derivation of this document folded k into BOTH paths
(`E_out = k^2*(E[M^2] + w^2*E[S^2])`), which yields
`k = sqrt(2)/sqrt(1+rho+w^2(1-rho))` — the correct law for a GLOBAL-gain
structure (width first, then a single output gain), but NOT for the
mid-only structure implemented here. The two laws agree only at w=0 and
w=1 (which is why every quick sanity pin passed) and deviate by up to
~0.35 dB in between (measured: -0.343 dB at w=0.5, rho=0 — matching the
wrong law's prediction `(2/(1+w^2)+w^2)/2 = 0.925` to the digit). The
mid-only structure is kept deliberately: w remains the EXACT side ratio
(`L'-R' = w*(L-R)`) and k the pure mid compensation; the corrected law
above is what the implementation and the tests now pin.

Sanity pins of the law (each is an independent check):

| case | k(w,rho) | meaning |
|---|---|---|
| w=1, any rho | exactly 1 | unity width needs no compensation |
| w=0, rho=+1 | exactly 1 | dual-mono content collapses at UNITY (no +3 dB mistake) |
| w=0, rho=0 | sqrt(2) = +3.01 dB | the classic uncorrelated-sum law |
| w=0.5, rho=0 | sqrt(1.75) = +2.46 dB | the mid-path-heavy intermediate (old law said +2.04 dB) |
| w=0, rho=-1 | diverges | the mid signal IS zero: no finite gain preserves energy (out-of-phase content cannot survive a mono collapse — physics, not a defect) |

For w <= 1 and rho >= -1 the law always yields k >= 1: narrowing with
compensation never attenuates.

## 3. The option space

### Option A — naive M/S width (no compensation)

`S -> w*S` and nothing else. Energy ratio `((1+rho)+w^2(1-rho))/2`:
up to −3.01 dB lost for uncorrelated content at full collapse; unity only
for correlated content. **Fails the user's stated twist outright.**
Rejected (kept as the reference point every other option is measured
against).

### Option B — static equal-power law (correlation-blind)

`k(w) = sqrt(2/(1+w^2))` — i.e. the law of section 2 evaluated at rho=0
for ALL content. Stateless, deterministic, one line of code. BUT:

* uncorrelated content: exact (by construction);
* correlated content (rho=+1): +3.01 dB TOO LOUD at collapse — and
  dual-mono is precisely THIS plugin's own default output for mono sources
  (section 0), so the error hits the most common case in this product;
* anti-correlated content: still loses energy.

A width control that gets LOUDER as you narrow a mono source is the
classic "width knob pumps" complaint. **Rejected as the primary law** for
a product whose dominant content class is dual-mono.

### Option C — correlation-adaptive static gain (the quasi-static law)

Measure `rho` on the stage's input (a smoothed, block-rate measurement of
the three running energies `E[L^2], E[R^2], E[LR]`), then apply
`k(w, rho_measured)` from section 2 (the corrected mid-only law). Properties:

* exact for ANY stationary content at ANY width (the derivation above
  literally solves for the measured rho);
* for the plugin's own dual-mono output the measurement converges to
  rho=+1 and the collapse is per-sample UNITY — the correct behaviour;
* the measurement is SLOW (a one-pole smoother, ~200 ms) so the gain is
  quasi-static: it does not track program dynamics, it tracks the IMAGE
  (correlation), which is exactly what the user changes with the knob.
  No pumping class (an AGC matches level; this matches a second-order
  statistic);
* edge cases need explicit engineering:
  - rho -> -1 with w -> 0: k diverges. CLAMP at kMax = +6 dB and document
    honestly (anti-correlated content has zero mid; the stage outputs the
    side at the clamped gain rather than exploding);
  - digital silence: the 0/0 correlation is undefined; freeze the last
    measurement below a tiny energy floor (output is silence anyway;
    the guarantee needed is "no NaN, no drift");
  - measurement must be FEED-FORWARD (on the stage input), never on the
    stage output — an output-side measurement would create a feedback
    loop through the gain;
  - cold start: seed the smoother with the first active block's own sums
    so the compensation is correct from the first post-activation sample
    (no warm-up dip).

### Option D — dynamic RMS-matching AGC (feed-forward or feedback)

Continuously measure input and output RMS and servo the gain
(attack/release). Exact for non-stationary content as well, BUT: it is a
level tracker — it MUST pump on transients (that is its job), it adds
tuning parameters (attack/release/window), and for stationary content it
converges to exactly Option C's gain while being strictly more complex
and strictly more audible. For a width knob the audible-moving-parts
budget should be spent on the image change, not on a level rider.
**Rejected** (noted: Option C is the quasi-static limit of D — the
equivalence is the reason C is "correct" for stationary content).

### Option E — Blumlein/Gerzon stereo shuffler (frequency-dependent width)

The classical extension: split-band M/S processing ("shuffling", Blumlein
1931; Gerzon's 1986-92 stereo shuffler papers) allows e.g. bass kept mono
while highs narrow — powerful for mastering. Cost: a crossover filter pair
(a whole new DSP surface with its own latency/phase/smoothing questions),
per-band energy bookkeeping, more parameters. Completely out of scale for
"one slider at the end of the chain". **Deferred as future work** (the
stage's insertion point and state slot are compatible with a later
frequency-dependent extension).

### Option F — per-sample energy normalization

Gain recomputed per sample. Preserves instantaneous energy but IS a
time-varying distortion (amplitude modulation riding the signal's own
envelope — audibly broken on anything with dynamics). **Rejected.**

## 4. Decision

**Option C** (correlation-adaptive static gain) with:

* `w` in [0,1] exposed as ONE new host parameter "Stereo Width"
  (id 38, appended after the per-voice block so all existing ids/automation
  stay stable), default 1.0 = bit-exact pass-through;
* `k(w,rho)` from section 2, measured feed-forward on the stage input,
  one-pole Euler smoother, tau = 200 ms (nominal), block-size-independent
  (per-sample pole, `lambda^n` by repeated multiplication — no
  transcendental in the block path; `sqrt`/division only, both IEEE-exact
  operations, so the ACTIVE stage is deterministic and reproducible);
* kMax clamp = 2.0 (+6.02 dB), documented with the rho->-1 honesty note;
* the width transformation expressed as the symmetric 2x2 matrix

```
L' = a*L + b*R        a = (k+w)/2
R' = b*L + a*R        b = (k-w)/2
```

  which gives two exact structural properties worth pinning in tests:
  `L'-R' = w*(L-R)` (the side scaling is EXACT, so w=0 collapses to mono
  BITWISE: L'==R'), and `L'+R' = k*(L+R)` (the mid scaling);
* (a,b) slew-rate-limited at ~10 ms full-scale (the engagement-ramp
  discipline of the core's other transitions) so width drags and bypass
  toggles are click-free;
* the stage is gated OFF when the engine's own bypass is engaged (bypass
  = the plugin does nothing; the stage ramps to identity through the same
  slew limiter, then goes fully inert);
* channels==1: structural no-op. channels>=2: operates on channels 0/1
  (L/R), other channels pass through (documented; the wrapper exposes a
  stereo bus).

## 5. Verification plan (the claims above become gates)

Core (new `r34` group, all on the real `MultichannelShifter` path):

1. `r34_width_default_bitexact` — default width: output BIT-IDENTICAL to
   two hand-driven `AlignedDelayLine` engines (the multichannel-level
   neutrality gate; the four r15 reference hashes stay structurally
   untouched — mono engines unchanged).
2. `r34_mono_collapse_exact` — w=0, arbitrary stereo input: L'==R'
   bitwise (the `L'-R' = w*(L-R)` law at w=0).
3. `r34_energy_correlated_unity` — dual-mono input, w=0: steady-state
   output EXACTLY equals the input per sample (rho measures exactly +1,
   k computes exactly 1, (L+R)/2 with L==R is exact — full-derivation
   bitwise identity, no tolerance).
4. `r34_energy_uncorrelated` — independent L/R noise: steady-state total
   energy within +-0.1 dB at w=0 (k -> sqrt(2)) and at w=0.5.
5. `r34_partial_correlation_gain` — constructed rho = +0.5 and -0.5
   content: the converged gain matches k(w,rho) within 2%.
6. `r34_anticorrelated_clamp` — L=-R: finite output, k pinned at kMax,
   mono output = clamped side (documented behaviour).
7. `r34_width_ramp_clickfree` — a mid-stream 1->0 width step: bounded
   per-sample output delta (the slew law), no discontinuity.
8. `r34_bypass_gates_width` — bypass on: the stage is identity regardless
   of w (bit-equal to the never-touched bypass path).
9. `r34_width_determinism` — a scripted width automation sequence, run
   twice: bit-identical.
10. `r34_width_rt_allocfree` — setters + process under the allocation
    counter: zero allocations.
11. `r34_silence_stability` — digital silence in: exact silence out, no
    NaN, measurement frozen (no drift, no 0/0).

Wrapper: the 39th parameter's surface (id/domain/default/round-trip),
state blob v3 round-trip + v2 back-compat (v2 restores with width=1 =
bit-neutral), the sample-accurate queue path.
UI: hit-map 39/39, the WIDTH slider drag protocol on the quantized grid,
a CONSCIOUS golden-frame re-pin (documented; the layout gains one slider).
Module: an 18th integration case driving the SHIPPED module through a
real parameter queue (mono collapse + energy preservation + bypass gating
on the actual .so/.dll).
Validator: 47/47 on the fresh bundle.

## 6. Honest limits (pre-declared)

* Compensated narrowing can raise PEAK levels (energy is preserved, not
  peaks): +3 dB RMS on uncorrelated content can translate to higher
  sample peaks. The float pipeline has no headroom issue internally; a
  host's final integer conversion is the host's domain. Documented, not
  limited (a peak-limiter belongs to a different round).
* The compensation is exact for STATIONARY content; slow image changes
  (content correlation drifting faster than ~200 ms) transiently deviate
  by the measurement's lag. This is the quasi-static trade-off chosen
  over AGC pumping (section 3, Option D).
* Anti-correlated content cannot be energy-preserved through a mono
  collapse by ANY gain (section 2); the clamp is the honest behaviour.
* macOS: unchanged from R33 (not implemented).
