# R22 — Original-target static findings (PRIORITY 5)

**Method:** read-only static disassembly of the original Eventide Octavox
binary (`original/Ox.vst3`, never executed; capstone 5.0.9, the same
PE-mapping discipline as R12's `r12_prepare_disasm.py`). Evidence classes:
OBS = static observation of the target, DER = independent measurement of
our engine, UNK = unresolved. Historical findings preserved; corrections
are ADDITIVE records.

Scans (all in this directory):
* `r22_randomize_trace.txt/.json` + the script
  (`../../../octavox-work/r22/r22_randomize_trace.py` preserved outside the
  evidence tree in the work area; a copy of the METHOD note is in the txt).
* `r22_pan_scan.txt` (stages 1–2).
* `r22_minpitch_scan.txt`.

---

## 1. Randomize sign and modulation direction — **RESOLVED** (the R9/R11 vs
## R21 contradiction; the round's special requirement)

**The contradiction:** R9/R11 documented a nonnegative modulation input
(`F ∈ [0,1]`, one-sided upward modulation); R21's static findings item P7(e)
said "depth 0..200 maps to ±0..8.33% rate modulation" (sign-symmetric).

**The resolution (every link re-derived instruction-by-instruction,
`r22_randomize_trace.txt`):**

| Link | Address | Law (OBS) | Sign consequence |
|---|---|---|---|
| Random source | `FUN_180139B40` (`rdm noise [212]`) | LCG-style state, `sar` + float conversion, ×2⁻²³-scale | **signed** output, orbit [−1.0, +1.0] (float32) |
| Absolute value | `andps xmm, mask` at **0x180103F33/0x180103F46/0x180103F5A/0x180103F75**, mask constant `0x7FFFFFFF` at **0x180412C60** (bytes `ffffff7fffffff7f...`, verified) | `\|noise\|` applied to the noise samples **BEFORE** the sample&hold | targets become **nonnegative** |
| Sample&hold | `call 0x180139970` at **0x180103F7D** (`rdm sh N`) | holds a snapshot of the absolutised stream | held target ≥ 0 |
| Slew | `call 0x180139790` at **0x180103F82** (`rdm slew N`, module at `rbx+0x8518`) | bounded walk toward the target, clamped AT the target (the 0x1801397D3 region, R9) | state stays ≥ 0 (zero init, nonneg targets) ⇒ **F ∈ [0, 1]** orbit-actual (1.0 attainable; 1+2⁻²³ arithmetic bound) |
| Scale conversion | page[10] at 0x1800E772D: `mulsd` by the double **1/2400 = 0.0004166666666666667** at **0x1804120B0** (verified), clamped | `page10 = clamp(modamt/2400, −1, 1)` | ≥ 0 for modamt ≥ 0 |
| Final multiplier | 0x1800E23DD–0x1800E243B: `f = F·page10`; `step = (float)P(f)·slot_40F8[v]`; `mulsd` by **64.0** at **0x180412490** | `step = 4·P(f)·(ratio·correction)` | **4·P(f) ≥ 1 for f ≥ 0** |

**The five required quantities:**

1. **Noise sign:** signed at the source ([−1, +1] float32 orbit).
2. **Modulation input range:** `F ∈ [0, 1]` orbit-actual — nonnegative by
   construction (abs → sh → slew).
3. **Rate multiplier range:** `4·P(f) ∈ [1, 1.122412]` at depth ≤ 200 —
   **one-sided, +0% .. +12.24% rate**, never below unity.
4. **Cents deviation from the baseline:** `0 .. +199.92` cents (walk·depth
   to within the quartic fit), upward only.
5. **Polynomial approximation error:** `|achieved − nominal| ≤ 0.1012` cents
   over the shipped domain f ∈ [0, 1/12] (signed −0.1012 c at f ≈ 0.0561;
   negative in the bulk; the R11 quantification re-verified numerically:
   depth 50 → −0.0610 c, depth 200 → −0.0769 c).

**The correction record (historical findings preserved, not edited):** the
R21 static findings' item P7(e) wording "±0..8.33% rate modulation" is
**superseded**: the "±" is wrong (no sign reintroduction exists anywhere in
the live chain — every link above is re-derived at the instruction level),
and "8.33%" conflates the f-domain ceiling (200/2400) with the rate
multiplier (4·P ∈ [1, 1.1224] = +0..+12.24%). The R9/R11 one-sided law
STANDS. R21's own chain description ("→ |noise| → rdm sh → rdm slew") was
already consistent with R9/R11; only the (e) summary wording regressed.

## 2. Per-voice Pan arithmetic inside the stereo mixer — bounded progress
## (still UNK at the arithmetic level; the composite structure mapped)

The R21 baseline: `stereomixer3` (record `ninputs=8`, `gain1..8`,
`pan1..pan8`, `time_constant=25` ms), add_fn **0x180137720**, consumer of
the mode flag `[obj+0x8C]` selecting per-class coefficient tables
(`dsp+0xA3CC..0xA41C`). R22 stage 1–2 (`r22_pan_scan.txt`):

* The composite add_fn 0x180137720 is a thin wrapper: it calls
  **0x1800D8230** (with `rdx = inputs+0x260`) and **0x1800D7400** (with
  `rcx = obj+0xF0`) — the real sub-modules.
* Sub-fn 0x1800D8230 is a CONSTRUCTOR-style initializer: module id
  **0x152** at `[obj+8]`, bound fields `0xC7000000`/`0x46FFFE00`
  (float32 −32768.0/+32766-ish: an int16-range clip pair) and `0x3F800000`
  (1.0f) at `[obj+0x40..0x48]`, `−1` markers at `+0x70/+0x78`.
* A whole-`.rdata` scan for pan-law table shapes (interleaved L/R pairs
  with power-sum ≈ 1 or linear-sum ≈ 1) found **no** clean pan table: the
  only structured candidate (0x180416360) is a 77-entry monotonic ramp
  0.3408→0.6931 (≈ ln 2) — a math-function table, not a pan law. The
  equal-power/linear law is therefore COMPUTED, not tabled, and lives
  deeper in the 0x1800D7400-family sub-modules — the concrete next-round
  target, now with the exact call structure mapped.
* Verdict: **OBS (routing + the composite call structure + the sub-module
  identities) / UNK (the mix arithmetic)** — upgraded from R21's "the mix
  law lives in a sub-module" by naming the sub-functions and excluding the
  table hypothesis for the whole .rdata.

## 3. Minimum Pitch: the tracker-domain conversion + sample-rate
## interaction — **NEW OBS findings** (`r22_minpitch_scan.txt`)

R21 closed the parameter (48 choices C0..B3, default index 24 = C2,
consumer `T+0x5F70` = the period-search base, `T+0x5F74` = the max index).
R22's disp32 scan of `.text` for `+0x5F70/+0x5F74` consumers found the
tracker's own arithmetic:

* **A low-pitch tolerance regime (0x18013CF3C):**
  `cmp dword ptr [r15+0x5F70], 0xD2` — when the period-search base index
  exceeds **210** (period > 210 samples ≈ below ~228 Hz at 48 kHz — inside
  the Minimum-Pitch-selected low region), the acceptance tolerance switches
  from **0.09375 (3/32)** to **0.03125 (1/32)** (constants at 0x180411F80 /
  0x180411F44, verified): the tracker tightens its comparison tolerance 3×
  for low pitches. OBS.
* **A rate-scaled clamped derivation (0x18013C97C–0x18013C9C9):**
  `eax = [r15+0x5F70] − (index distance)` → `cvtdq2ps` → `mulss [r15+0x6000]`
  → clamped into **[2.0, 18.0]** (constants at 0x1804123B0/0x18041261C) →
  `cvttss2si` → stored at `[r15+0x5F40]`. The multiplier at **T+0x6000** is
  the rate-dependent factor — **the sample-rate interaction**: the derived
  integer (a period-column/crossfade-style count, cf. R9's page[11]
  crossfade limit `(int)r13d + const`) scales with the sample rate through
  the domain base. OBS for the structure; the exact choice→index write
  path (the minp module's binder) is still to be traced (the record's
  "T+0x5F70 = x" link is runtime-machinery, not a static disp32 write).
* Cross-check with our engine: our `Config::f0_min` gates the DETECTOR
  domain coherently (R22-P1); the original's base-index regime at 210 and
  the tolerance tightening are target-specific behaviours we do NOT claim
  to reproduce (recorded as OBS only).

## 4. Items carried unchanged from R21 (this round's re-check only)

Per-voice feedback routing (parameter CLOSED / routing UNK), Crossfade
binding (two-ends OBS), Pitch Tracking Enable transitions, Instrument
anti-alias tables, MIDI dispatch (precise negative result), Tempo/meter
conversion, Preset/Snapshot semantics — see
`evidence/analysis_round21/r21_static_findings.md` (the matrix below folds
the R22 upgrades in).
