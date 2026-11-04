# R22 static findings (original-target reverse engineering)

Round 22 additions to the original-target (`Ox.vst3`) static evidence. The
binary was NEVER executed; every statement below is a code/byte observation
(OBS) on the extracted module records and decompiles, or is labelled
DER/UNK. Historical findings from R9–R21 are preserved verbatim; where R22
corrects a wording, the correction is logged here, not silently edited.

Sources this round: `evidence/records/full_records.txt` (module records,
mimo profile unless noted), `evidence/decompiles/method01_annotated.txt`,
`evidence/decompiles/method05_annotated.txt`, plus the R9/R10/R11/R21
findings documents.

---

## R22-1. Randomize output polarity — the R9/R11 vs R21 contradiction RESOLVED

**The contradiction.** Rounds 9–11 state the random pitch modulation is
ONE-SIDED UPWARD (`|noise| ≥ 0 ⇒ f ≥ 0 ⇒ 4·P ≥ 1`; R9 §"random modulation",
R11 §"one-sided-upward direction with F attaining 1"). The R21 static
findings document, item P7(e), instead words the depth mapping as
"**±0..8.33%** rate modulation" — a sign phrasing that implies a
bidirectional (±) modulation. Both cannot be right.

**The chain re-verified link by link (all OBS, addresses included):**

| stage | evidence | range |
|---|---|---|
| 1. random source `rdm noise [212]` | `FUN_180139B40`: `(state >> 8)·2^-23·(1+2^-23)`, mask `0xFFFFFF00`, seeds `0xD00`/`0x1100` (R9/R10) | **signed** `[−(1+2^-23), +1.0]` |
| 2. absolute value | **`andps` with the mask `0x7FFFFFFF`, constant at `0x180412C60`, sites `0x1800F55…` / `0x180103F16–0x180103F78`** (R9, re-checked R22) | `[0, 1+2^-23]` — **non-negative** |
| 3. sample&hold `rdm sh N` | `FUN_180139970` (R9) | held targets non-negative |
| 4. slew `rdm slew N` | `FUN_180139790`, walks toward the held target, clamped AT the target on crossing (R9) | walk value `F ∈ [0, 1+2^-23]` |
| 5. prepare-side scale | `f = F · slot_40F8[v]`, `slot_40F8[v] = clamp(rdep,0,200)/2400` (R21 P7) | `f ≥ 0` |
| 6. polynomial map | `4·P(f) = 1 + 1.3842323·f + 0.9756581·f² + 0.4793226·f³ + 0.1394705·f⁴` (R9; all coefficients POSITIVE ⇒ monotone increasing on `f ≥ 0`, `4·P(0) = 1` exactly) | multiplier `≥ 1` |
| 7. final multiplier | `rate_v = 4·P(f)·ratio·correction` (R12) | voice-rate multiplier `≥ 1` |

**Resolution.** The R21 "±0..8.33%" phrasing is a **wording error** and is
hereby corrected (correction-log entry, R22): it conflated the polynomial's
**fit domain** — `f` is "the pitch deviation in units of 2400 cents
(±2 octaves)", i.e. the DOMAIN is signed (R9's own words) — with the
**driven range**, which is strictly `f ∈ [0, clamp(depth,0,200)/2400]`:
`8.33%` is the value of `f` at the depth-200 corner (`200/2400 = 1/12`),
not a ± excursion. The OBSERVED chain (stages 1–7) admits only
**one-sided, upward** modulation of the voice ratio:

* in **cents**: `walk·depth ∈ [0, +200]` cents (never negative; at the
  shipped maxima +49.939 ¢ / +199.923 ¢, below nominal by the quartic's
  fit error `≤ 0.101 ¢`, R10/R11);
* in **ratio**: the multiplier `4·P(f) ∈ [1, ≈1.1224]` over the shipped
  depth range (up to **+12.24 %** upward at depth 200 — NOT ±8.33 %; the
  8.33 % figure is the `f`-scale fraction);
* at the extreme `f = 1` (unreachable through the depth knob — only via a
  corrupted page slot): `4·P(1) ≈ 3.979` (R9's measured table).

The R9/R10/R11 findings STAND unmodified. No runtime behaviour is claimed
(the target was never executed); this is the static-chain verdict.
**Class: CLOSED (static); the R21 P7(e) sentence is superseded by this
entry.**

---

## R22-2. Pan — a new concrete mix-law data point (law still UNK)

R21-P1 established: `pan1..8` knobs, **range −1..+1, res 0.01, default 0**
(records `[28]` etc.); consumer `stereomixer3` (record `[338]`,
`ninputs=8`, `gain1..8 = m switch N-out`, `pan1..8 = panN-out`,
`time_constant=25`; add_fn `0x180137720`, composite wrapper — the L/R
coefficient law UNK).

R22 addition (OBS, mimo profile records):

* **`monoSumWetChans` (record `[357]`, add `0x180138710`, slot `0x1E990`):
  `in1=feedback-out, in2=feedback1-out, amp1=0.707, amp2=0.707`.** The
  original's mono fold of the two per-channel feedback/wet signals uses an
  **equal-power pair (1/√2, 1/√2)**. This is the first CONCRETE
  equal-power constant pair observed in the output/feedback neighbourhood
  of the pan path. It is ADJACENT EVIDENCE for an equal-power pan family —
  it does NOT prove the stereomixer3 pan law (the mono fold is a different
  module). The pan law itself remains UNK (R21 verdict unchanged).
* The output-stage selectors around the fold: `iswitch9` (`in1 =
  monoSumWetChans-out, in2 = feedback1-out`, select `c_subtract3-out`) and
  `iswitch11` (`in1 = monoSumWetChans-out, in2 = feedback-out`) feed
  `multiply2/multiply1` (× `c_fltr_a2/c_fltr_a1`) — the mono/stereo
  output-mode switching of the wet path (records `[360]`–`[363]`).

For OUR independent product the R22 pan law (√2·cos/√2·sin,
unity-centred) is documented in `native/include/octavox/dsp.hpp`; the
original's law is NOT claimed reproduced. Noteworthy (coincidence, not
evidence): our chosen knob domain [−1,+1] default 0 matches the original's
knob record exactly.

**Class: OBS (new data point); pan mix law still UNK.**

---

## R22-3. Global feedback — full routing topology (new)

The `mfbk` knob (record: `feedback: %f %`, **0..100 %, res 1, default 0**,
add `0x1800CA340`) is the GLOBAL feedback amount. R22 traced its consumer
topology through the records (all OBS):

```
stereomixer3-left ──┐                       stereomixer3-right ──┐
 (POST-PAN wet L)   │                                        │
                    ▼                                        ▼
             [345] feedback   ── amp1=1 ───────────►  [339] feedback1
                    │ in2=subtract-out                        │ in2=subtract1-out
                    │ amp2=mfbk-out (0..100%, tc=20 ms)       │ amp2=mfbk-out
                    ▼                                        ▼
             [346+] softclip / softclip2 (select=1, drive=0, order=1)
                    ▼
             delay_xf / delay_xf1 (maxdelay=2400, xfadetime=20,
                    │  tapdls = c_minmax-maximum (fbkd knob, 0..5000 ms, vs 50))
                    ▼
             "just for the filter" (maxdelay=1) → highcut (freq=5) → subtract
                    └──────────────► back into feedback/feedback1 in2 ◄──┘
```

New facts beyond R21:

* the global feedback taps the **POST-PAN stereo wet mix**
  (`stereomixer3-left/right`) — per-CHANNEL, not a mono sum (the mono sum
  `monoSumWetChans` appears at the OUTPUT stage, after the feedback taps);
* each channel's feedback path contains a **softclip** (record `[340]`:
  `softclip2`, select 1, drive 0, order 1) — the original's feedback bus is
  SATURATING by construction;
* the feedback path contains a **crossfaded delay** (`delay_xf1`,
  maxdelay 2400, xfadetime 20) driven by the `fbkd` knob ("fback dly:
  %f ms", **0..5000 ms, res 1, default 0**) through `c_minmax`
  (`in1=fbkd-out, in2=50`; the exact unit semantics of the 50 — samples
  vs ms vs block — is UNK from the records alone);
* a **high-cut + subtract cleanup** loop (`just for the filter2`,
  maxdelay 1; `highcut1`, freq=5; `subtract1`) precedes the recirculation
  into the feedback modules' `in2`;
* the per-voice feedback chain (R21-P2, CLOSED) is confirmed and its
  conversion modules now carry addresses: `fback switch N` (in1 = −100
  const, in2 = `fbkN-out`, select = `mswN` mute) → `c_divide18..25`
  (`in2=20`, add `0x18013A110`) → `c_power9..16` (`in1=10`, add
  `0x18013A110`) = **10^(dB/20)**, feeding the wet-path filters
  (`c_fltr_a*`).

**Class: OBS (topology fully traced at the record level); the softclip's
transfer curve and the c_minmax unit semantics are UNK. No runtime claim.**

---

## R22-4. Crossfade range and grain-commit conversion (status)

R21-P4 verdict stands (two-ends OBS, internal build DER). No new static
evidence this round; the R22 ENGINE work (P1's spacing-gate linkage and
the 10-point battery) is OUR OWN behaviour and is documented in the R22
findings, never as the original's.

## R22-5. Minimum Pitch exact range conversion (status)

R21-CLOSED (48 choices C0..B3, default C2 ≈ 65.4 Hz; static). The R22
engine-side f0_min work (detection floor vs spacing gate) is OUR design;
the original's knob→lag-domain conversion arithmetic remains untraced
(UNK). Note: the original's C0 floor (≈ 16.35 Hz nominal) is BELOW our
engine's validated f0_min domain [20, 200] Hz — a documented difference of
the independent implementation, not a claimed parity.

## R22-6. MIDI dispatch, tempo/meter, snapshots (status)

R21 verdicts stand: MIDI = parameter OBS / routing UNK (no DSP-side MIDI
modules — precise negative result); tempo/sync = parameters + chunk tags
OBS / conversion UNK (host-side); snapshots = chunk system OBS /
semantics UNK. No new evidence this round.

---

## Correction log (R22 entries)

| id | superseded statement | correction | basis |
|---|---|---|---|
| C-R22-1 | R21 `r21_static_findings.md` P7(e): "depth 0..200 maps to **±**0..8.33% rate modulation" | The modulation is ONE-SIDED UPWARD: cents `∈ [0, +200]`, ratio multiplier `∈ [1, +12.24 %]`; `8.33 %` is the `f`-scale fraction at depth 200, not a ± excursion | R22-1 chain re-verification (this file) |
| C-R22-2 | (none — addition) | `monoSumWetChans` equal-power 0.707/0.707 mono fold observed (record `[357]`) | R22-2 |
| C-R22-3 | (none — addition) | Global feedback topology: post-pan stereo taps, softclip, crossfaded fbkd delay, high-cut cleanup (records `[339]`–`[345]`, `[357]`–`[363]`) | R22-3 |
