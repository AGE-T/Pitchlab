# OCTAVOX ROUND 11 FINDINGS

**Numerical consistency audit of round 10 + independent validation of the WAV→WAV DSP demo
and its highest-value improvement**

Continued from `Octavox_RE_evidence_round10.zip` (SHA-256
`e7f9067ababaa033493b7a08730ffbc279ca2aa5295b246831daab6bf2237d9a`, verified intact on arrival).
Static analysis + numerical work only; the target was **never executed and never modified**.
Labels: **OBS** observed · **DER** derived · **UNK** open.

> **Environment note:** the original `Ox.vst3` is not present in this round's environment; the
> target digest on record (`7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`)
> is unchanged in every document, and all numerical inputs below are the OBS bit patterns
> recorded in `evidence/records/rea8_rate_constants.txt` and
> `evidence/analysis_round10/r10_decode.txt`, both of which were verified byte-exact against
> the image in rounds 8–10. No static conclusion depends on re-reading the binary this round.

---

## 1. Verdict

| question | answer |
|---|---|
| Do the round-10 numerical claims reproduce independently? | **Yes — all six**, with two label corrections and one new qualification (§2). Examples **49.9391 ¢** (depth 50) / **199.9231 ¢** (depth 200) at `F = 1`; max fit error **0.10131 ¢**; **interior** maximum; proven `F` ranges; excess absorbed at the tested depths. |
| Where is the maximum error, really? | **Interior**: `f ≈ 0.05606–0.05612` → `F ≈ 0.6727–0.6734` at depth 200 (depth ≈ 134.5–134.7 at `F = 1`). The `F = 1`/depth-200 corner carries only **0.0769 ¢**. The round-10 doc said "near `F ≈ 0.75`" and the script printed "at F=1, depth=200 corner" — mutually inconsistent, both wrong. |
| Is the residual "one-sided upward"? | **No.** As a description of the *approximation error* it was wrong: the signed residual is **negative across the bulk** of the shipped domain (achieved cents slightly *below* nominal), zero at `f = 0`, positive excursions ≤ half a float32 `P`-ulp (`+1.03e-4 ¢`) only for `f < 2.9e-5`, and it changes sign twice beyond the shipped range (`+4.03 ¢` at `f = 0.5`, `−9.25 ¢` at `f = 1`). "Upward" describes only the **modulation direction** (`4·P ≥ 1`) — a different concept. |
| Is the `2^-23` excess "zero effect / bit-exact"? | **At the two depths round 10 tested (50/200): yes, bit-exact. Universally: no** — new finding: at **11 of 201 integer depths** (61, 87, 101, 134, 141, 144, 151, 160, 162, 182, 192) `P` straddles a `cvtpd2ps` rounding midpoint and the excess flips the rounded value by exactly **one `P`-ulp** → cents change `2.04e-4 ¢` (~500× below the fit error, ~2×10⁻⁷ relative — inaudible). Correct wording: "no audible effect", not "no output value changes". |
| Does the shipped demo reproduce? | **Yes.** `example_input.wav` and all four shipped example outputs reproduce **byte-identically** on this environment (Python 3.12.15 / numpy 2.1.3 / Linux vs. the recorded 3.11.9 / 2.4.4 / Windows); the comparison metrics reproduce to ≤ 1e-10 relative (numpy FFT last-digit noise); cost rows are machine-dependent by design. |
| Does the shipped demo behave as documented? | **Almost. Two deviations found** (§4): the stereo claim was wrong for the delay line (right channel contaminated at **−0.44 dB** by the shared stateful engine), and the vocoders drop inputs shorter than `n_fft/2` (21 ms @ 48 kHz) to digital silence. Both documented and corrected in the docs; the stereo defect is fixed in the v2 CLI. |
| Highest-value improvement implemented? | **Period-aligned grain splicing** (`delayline_aligned`, `poc/octavox_v2.py`, separate from the untouched reference): pitch error **+3.27→+0.03 ¢ / −5.84→+0.00 ¢ / +9.13→+0.08 ¢ / −17.76→−0.02 ¢** and crossfade modulation **2.3 dB→0.00 dB** (period-synchronous metric) on identical deterministic signals, with the stereo fix and a 45-case gated acceptance suite (45/45 pass). |

---

## 2. Phase A — the numerical audit (script: `r11_numeric_audit.py`, results `.txt`/`.json`)

Method: the OBS bit patterns (`c0..c4 = 0x3E800000/0x3EB12E86/0x3E79C4BB/0x3DF569C6/0x3D0ED15E`,
scale `0x34000001`, double `1/2400` bits `4f1be8b4814e3b3f`) with the target-exact arithmetic
(double quartic in the decoded operation order `((c3f³+c4f⁴)+c2f²)+(c1f+c0)`, `f³ = f²·f`,
`f⁴ = f²·f²`, `f = float32(F)·float32(page10)`, `P` rounded by `cvtpd2ps` @ `0x1800E2408`).
Domain: `f ∈ [0, page10(200)]` with `page10(200) = 0.0833333358168602` (the float32-rounded
true corner; `2400·page10 = 200.0000060`).

### 2.1 The signed residual `1200·log2(4·float32(P(f))) − 2400·f` over the shipped domain

| quantity | value |
|---|---|
| signed **minimum** | **−0.10131099 ¢** at `f* = 0.0560623630` (deepest point on a 5e-10 grid) → `F = 0.6727483` at depth 200; depth `= 134.54967` at `F = 1` |
| signed **maximum** | `0.0` at `f = 0` exactly; over `f > 0`: **+1.0303e-4 ¢** at `f = 4.31e-8` — a pure `P`-rounding artifact (half a `P`-ulp = `1.032e-4 ¢`); positive blips only for `f < 2.89e-5` |
| **max absolute error** | **0.10131 ¢** (the interior minimum; **not** the corner) |
| residual at the corner `f = page10(200)` | **−0.076884 ¢** |
| task-claimed location `f = 0.05608058` (`F = 0.672967`) | residual **−0.10130291 ¢** at the nominal `f` (8.08e-6 ¢ from the extreme); **−0.10112730 ¢** at the target-exact `f = float32(0.672967)·page10(200)` — a `P`-rounding boundary lies between the two. Both lie inside the same rounding-step **plateau**: the argmax is grid-sensitive at the ~1e-4 level in `f` because the `float32` staircase of `P` flattens the minimum (deepest 5 separated dips all within 1.1e-7 ¢ of each other, spanning `f = 0.056046–0.056062`). |
| smooth (pure-double) reference | min −0.10121551 ¢ at `f = 0.0560615` — the staircase extreme sits one half-step below it, exactly as expected |
| beyond the shipped range | `+1.049 ¢` (f=0.25), `+4.032 ¢` (f=0.5), `+2.996 ¢` (f=0.75), `−9.251 ¢` (f=1.0) — two sign changes |

### 2.2 The `F` range proofs (reproduced)

Arithmetic: `sar ∈ [−2^23, 2^23−1]`, scale `2^-23·(1+2^-23)` ⇒ `noise ∈ [−(1+2^-23), +1.0]`
(`−2^23` end exactly representable; `+2^23−1` rounds **up** to `1.0`) ⇒ arithmetic
`F ∈ [0, 1+2^-23]`. Full-period orbit of the decoded recurrence (`M = [[17,12],[24,17]]`
mod `2^24`, seeds `(13,17)`, matrix order and vector period `2^22 = 4,194,304`): emitted
`sar ∈ [−8,388,607, +8,388,607]` — `−2^23` never visited, `+2^23−1` visited — emitted
float32 noise **exactly `[−1.0, +1.0]`** ⇒ orbit-actual `F ∈ [0,1]` with **`1.0` attainable**.

### 2.3 The excess absorption (new qualification)

`|ΔP|/P-ulp = 0.030/0.061/0.130` at depths 50/100/200 (< ½ ulp) — absorbed wherever `P` is not
within `|ΔP|` of a rounding midpoint. Depth sweep 0…200 (integers + 0.25 steps): **190/201
integer depths bit-exact**; 11 exceptions (§1) flip one ulp, max `|Δcents| = 2.040e-4` =
one `P`-ulp (`2.064e-4`). The round-10 wording is corrected in all canonical documents.

### 2.4 Corrections applied (changelog; originals preserved)

1. `r10_verify.py`: output labels fixed (docstring + `[4]` location line + `[8]` verdict line);
   the original output is preserved verbatim as `r10_verify_results_v1_superseded.txt`; the
   regenerated `r10_verify_results.txt` differs from the original **only** in those two label
   lines — every number is unchanged (verified by filtered diff).
2. `OCTAVOX_ROUND10_FINDINGS.md`: §1 verdict rows, §2.2 location ("near `F ≈ 0.75`" → the
   plateau values), §3.3 qualification, §4.1/§6/§7 wording; new §8 changelog.
3. `OCTAVOX_OPEN_QUESTIONS.md` (Q-7 row, §4 summary), `OCTAVOX_DSP_ARCHITECTURE.md` (S5 walk
   range, §8 law block), `OCTAVOX_CONTROL_FLOW_MATRIX.md` (§11/§12 notes),
   `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (R-50/R-54 wording, R-49 superseded marker, new
   round-11 rows R-59–R-61 + round-11 correction log), `OCTAVOX_ROUND9_FINDINGS.md` (round-11
   audit note added to the correction box; the slew-range wording qualified), `README.md`
   (round-10 summary rows).
4. Historical values (`50.13/201.68 ¢`, `F ∈ [0,1)`) remain only inside clearly-marked
   correction/superseded contexts. No raw evidence file was rewritten or deleted.

---

## 3. Phase B1 — baseline reproduction of the WAV→WAV demo

* **Byte-identical reproduction** of `example_input.wav` (SHA-256 `30596b4c…`) and of all four
  shipped example outputs (`delayline`/`pv`/`pv_transient`/`pvt_44k`) on this environment —
  the demo's audio path is deterministic across Python/numpy/OS versions to the last bit of
  the 16-bit output.
* `--compare` (18 s here): all metric fields reproduce to ≤ 1e-10 relative (last-digit float
  differences from the numpy FFT version on the recorded machine); the cost columns are
  machine-dependent by design.
* Class-level probes (fresh engines per signal — the documented comparison surface): silence →
  exact digital silence (all three designs); −60 dBFS tone → level preserved (0.80/0.95× rms);
  unison → vocoders ±0.00 dB, delay line **−2.42 dB** at 220 Hz (the documented two-tap comb,
  analytic `|cos(π·f·G/sr)|` ≈ −3.1 dB); 24/32-bit PCM reads exact; −5 st pitch: delay line
  −5.84 ¢ (the known carrier artifact), `pv` +0.03 ¢.

## 4. Phase B1 — documentation deviations found (measured, then corrected in the docs)

1. **Stereo contamination (defect).** `OCTAVOX_DSP_DEMO_USAGE.md` §4.3 claimed
   "channels processed independently". For the **delay line via the reference CLI** this is
   false: `main()` builds one engine and calls `process()` per channel, and the engine is
   stateful (ring buffer + grain phases), so the right channel reads the left channel's ring
   residue. Measured with `L = 440 Hz tone, R = silence` through the actual CLI: **right-channel
   peak −0.44 dB below the left** (i.e. nearly full-level left content in the "silent" right
   channel). The vocoders are stateless and clean. **Fixed in the v2 CLI** (fresh engine per
   channel): contamination **−234 dB** (float zero). The reference files are intentionally left
   untouched (baseline preservation); the docs carry the correction.
2. **Short-input silence drop (edge behaviour).** `_pv_shift_mono` prepends an `n_fft/2` causal
   delay and truncates to the input length, so any input shorter than `n_fft/2` (1024 samples
   = 21 ms at 48 kHz) comes back as **pure digital silence**. Documented as a known edge
   (gated in the suite only for finiteness/length on that input class).
3. **Float-domain overshoot on percussive input** (≤ 1.14 peak from ~0.85-input bursts) —
   legitimate interpolation/vocoder transient response, absorbed by the documented ±1.0
   clip-guard at the 16-bit write path; the suite gates it at a sane bound and reports the
   clipped-sample count.

## 5. Phase B2/B3 — the acceptance suite and the improvement

**Suite** (`evidence/analysis_round11/r11_acceptance_tests.py`; run:
`python3 evidence/analysis_round11/r11_acceptance_tests.py`; ~2.5 min; exit 0 = pass):
**45/45 gated cases** — I/O (16/24/32-bit read, clip-guarded write), 28 reference-design cases
(pitch ±shifts, duration, silence, −60 dB level, unison, chord, percussive, 8 voices, impulse
latency, feedback stability), 12 v2 cases (aligned pitch/modulation gates at 2 ¢ / 0.75 dB,
long-run, stereo integrity/differential) and 4 CLI end-to-end cases (mono, stereo, 44.1 kHz +
keep-rate, 24-bit). Machine-readable results: `r11_acceptance_results.json` (+ the captured
text run). Tests **fail** (exit 1) on invalid output; wall-clock cost is reported ungated.

**Improvement** (full note: `evidence/analysis_round11/r11_dsp_improvement.md`; implementation:
`poc/octavox_v2.py`, separate from the reference as required): chosen from the measured
weakness table — the delay line's few-cents carrier error and crossfade modulation share one
mechanism (the fixed tap spacing `G`), so **period-aligned grain splicing** (`S = k·T̂`
committed at wraps, hysteresis-gated, falling back to `G` without a reliable period) removes
both at once, and the delay line is the family that wins transients and cost. Before/after on
identical signals (220 Hz, 48 kHz):

| ratio | pitch error (YIN) | crossfade modulation (period-synchronous metric) |
|---|---|---|
| +4 st | +3.274 ¢ → **+0.030 ¢** | 2.36 dB → **0.00 dB** |
| −5 st | −5.835 ¢ → **+0.001 ¢** | 2.32 dB → **0.00 dB** |
| +1 oct | +9.131 ¢ → **+0.082 ¢** | 2.27 dB → **0.01 dB** |
| −1 oct | −17.757 ¢ → **−0.016 ¢** | 1.99 dB → **0.00 dB** |

Latency unchanged (102.5 ms), cost unchanged (~3.8–4.3 s/s at 8 voices), causality unchanged
(−234 dB pre-onset), stability unchanged (bounded with feedback 0.35). Documented trade-offs:
unison comb remains (no wraps ⇒ no commit — the Q-14 analog for our demo); very small
`|1−r|` commits rarely; one start-up commit transient per voice (~170 ms); polyphonic input
aligns to the detected fundamental only. The reference implementation stays the regression
baseline — its shipped artifacts still reproduce byte-identically.

**Not treated as the correct design by resemblance:** the comparison table stands — the
vocoder wins steady-state pitch accuracy, the delay line wins transients and cost, and the
aligned delay line now wins steady-state accuracy *and* transients within its family. The
vocoder pre-echo (14–29 ms onset delay) remains the strongest open direction for a future
round (a transient-bypass hybrid), measured and recorded, not implemented this round.

---

## 6. Static findings versus runtime unknowns (unchanged policy)

* The original target was **never executed** in any round; nothing in this round changes that.
  All Phase-A numbers are evaluations of recorded OBS bit patterns; all Phase-B numbers are
  measurements of **our own** demo. No PoC result is substituted for a target measurement.
* Static tracker data flow (rounds 9–10) vs **actual runtime tracker output**: still UNK.
* Host latency (Q-8: dead `LatencySamples` writer, non-virtual getter absent from `.text`)
  vs our demo's measured latency (102.5 ms delay line / 34–53 ms vocoders): different
  quantities; the target's host-reported latency remains UNK.
* The reconstructed unity-ratio comb is a property of **our** reconstruction; whether the
  original plugin exhibits it audibly is Q-14 (open).
* Controlled tests that would close the runtime questions (a suitable Windows host being
  unavailable here): (1) drive the plugin at unity ratio with noise and measure the output
  spectrum for comb notches at `sr/G`-like spacings (Q-14); (2) set `rnd dpth` 50/200 on a
  steady tone and long-run capture, then histogram the instantaneous pitch to observe the
  `walk·depth`-cent random modulation and its one-sided-upward direction with F attaining 1
  (round-9/10 law); (3) poll params 12/13 with normalized material to test the double-conversion
  gate prediction (read-outs stuck at 0); (4) measure the plugin's reported latency and
  compare with the dead-writer finding (Q-8); (5) feed a stereo signal and check
  channel-independence of the engine (our defect is ours; the target's engine-per-profile
  architecture suggests independence, UNK).

---

## 7. Status and artifacts

* Round-11 artifacts (all under `evidence/analysis_round11/` unless noted):
  `r11_numeric_audit.py` + `r11_numeric_audit_results.txt` + `r11_numeric_audit_results.json`
  (the independent audit; exit-code gated); `r11_acceptance_tests.py` +
  `r11_acceptance_results.txt` + `r11_acceptance_results.json` (45/45); `r11_dsp_improvement.md`
  (design + trade-offs + before/after); plus `poc/octavox_v2.py` and
  `poc/example_output_aligned.wav` + `poc/example_aligned.json` (deterministic regression
  artifact of the improvement).
* Canonical docs updated: `OCTAVOX_ROUND10_FINDINGS.md` (corrections + §8 changelog),
  `OCTAVOX_OPEN_QUESTIONS.md`, `OCTAVOX_DSP_ARCHITECTURE.md`, `OCTAVOX_CONTROL_FLOW_MATRIX.md`,
  `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (R-59–R-61 + round-11 correction log),
  `OCTAVOX_ROUND9_FINDINGS.md` (audit note), `OCTAVOX_DSP_DEMO_USAGE.md` (defect corrections +
  v2), `README.md`.
* Provenance: originals preserved — round-10 zip `e7f9067a…` (intact on arrival, re-verified at
  packaging), target digest `7bb260f6…` on record (binary not present in this environment),
  `r10_verify_results_v1_superseded.txt` added for the preserved original output.
* No completeness claim is made; the open questions and UNK items above are the honest residue.
