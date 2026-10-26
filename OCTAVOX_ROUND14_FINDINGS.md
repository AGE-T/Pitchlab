# OCTAVOX ROUND 14 FINDINGS

**Evidence consistency, remaining static DSP paths, and quality
benchmarking.** Continued from `Octavox_RE_evidence_round13.zip` (SHA-256
`234ad5721afbcbd14c77858de1aa244ae34c68b1d42b39199b62dceb9f61bb30`, verified
intact on arrival; 308/308 manifest records OK after extraction). Static
analysis + our own native implementation only; **the target was never
executed and never modified**. Labels: **OBS** observed · **DER** derived ·
**UNK** open.

> **Environment note:** the original `Ox.vst3` (SHA-256
> `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`) is
> present and was re-verified before and after every scan (read-only
> capstone). Development host: 2-core x86_64 sandbox, GCC 14.2, CMake 4.4.4,
> capstone 5.0.7 (venv `/home/z/.venv`).

---

## 1. Verdict

| question | answer |
|---|---|
| Are the Round-13 evidence inconsistencies corrected? | **Yes — all four, with machine-readable before/after.** (1) File count: the manifest holds **308 records / 309 zip entries** (root `checksums.txt` + 308 project files under `Octavox_RE_evidence/`), not "305 files"; the verification command now matches the actual archive layout (`cd Octavox_RE_evidence && sha256sum -c ../checksums.txt`). (2) Cold-start timing: the report's **45.3 ms** and the cold-start JSON's **48 ms** are the **same commit** — the engine's own record (`last_commit_sample` = 2175 → 45.3125 ms) versus the harness's 256-sample **block-observation boundary** (2304 → 48.0 ms); the first reliable pitch estimate (window fill) is at 42.7 ms and the commit is 2 control intervals (2.65 ms) later, not "≈4 ms". (3) The cold-start harness's pitch meter **was** the legacy log-grid meter (1.2 c grid, no refinement): quantified self-error ≤ 0.596 c (mean +0.102 c) vs the corrected meter's ≤ 0.082 c; every r13 cold-start pitch value is now co-measured with both meters. (4) The affected summary tables were recalculated and annotated in place (`DELIVERY_ROUND13.md`, `OCTAVOX_ROUND13_FINDINGS.md` §2.3/§6.1) with explicit `[ROUND-14 CORRECTION]` markers; the round-13 ZIP is preserved unchanged. |
| Do the affected tests still pass after the meter unification? | **Yes.** Suite re-run with the fixed meter: **105/105, identical verdicts** (`r14_native_tests_vs_r13.txt`); the corrected cold-start harness reproduces every round-13 legacy value exactly on the legacy axis and measures the engine's true cold-start pitch error at **≤ 0.018 c** across the whole cent matrix (`r14_coldstart_unified.json`). A latent **suite-meter window-placement bug** was found and fixed (probe: a non-power-of-two window could read *before* the pointer — the round-13 informational values were biased ≤ 0.05 c on same-material windows; e.g. `near_unity_-10c_via_unity` pitch error 0.41 c → 0.01 c). |
| Static advance (tracker input + limiter)? | **The round-13 residue is closed.** `xmm1` = **tracker+0x5FD4, the period estimate at the 2¹¹ scale** (writer: tracker state 6 — peak-pair distance over an 8×-decimated history with parabolic sub-entry refinement `δ·2^14`, cfg ×2/×4, reject-hold, gate, octave guard below 50·2¹¹); the divisor chain computes **ω = 2π/T̂** (the constant at 0x180412400 is the **double 2π** — the round-13 "3370280550400.0" was a float32 misread of its low half). The **ratio limiter is fully decoded**: instantaneous target (snap-to-table) → per-candidate enable bit → ±cents dead-band (m01 id 0x10: 2^(±cents/1200)) → per-update multiplicative clamp (m01 id 0x11: 2^(±1/(12·rate))) → running gain `+0x6070` (hard reset to 1.0 on \|Δω\| ≥ 4e-4; ctor init 1.0) — **and the running gain is exactly the `correction` of the round-9 law**: the prepare reader stores `slot_4128 = page[0]·gain = (ratio/16)·correction`. These fields control **pitch correction only**; the re-arm path consumes the ring's *period* field (min-clamp 48, degenerate fallback 480 — **no upper clamp**, correcting round 12's "[48,480]"). |
| Observed kernel origin? | **Embedded precomputed .rdata data (OBS).** Exactly three rip-relative references exist (two pointer installs — the pitch engine's `engine+0x40F0` and a second consumer's `[obj+0xD0]` — plus one gap-sweep false positive); **no write path anywhere**; the table's byte patterns occur **exactly once in the whole image** (no second copy, no source table, not a slice). The exact design formula stays **UNK** (the Blackman-Harris-windowed-sinc fit remains an approximation at 0.3 % RMS — never presented as the formula). |
| Which interpolation mode should be the default? | **Lagrange6 (unchanged).** Full head-to-head on identical deterministic signals (`r14_interp_compare.json/.txt`): the four modes are indistinguishable on pitch (spread 2.2e-5 c), gain (0.0013 dB), env-mod, chord residual, stereo (exact zero), multivoice and memory — all below the stated materiality thresholds; the real differences are **tone THD+N at small shifts/low frequencies** (Lagrange6-2048 best by **15.3/19.6/22.1 dB** over Linear/Lagrange6Q/ObsKernel6 at ±1 c — phase-resolution, not kernel order) and **noise HF flatness** (Linear +1.54 dB, ObsKernel6 +2.02 dB extra tilt). **ObsKernel6, judged only on measured performance, shows no advantage on any metric**; its value is target-parity research. Linear's only material win is CPU (−3 %..−15 %). |
| Is the core ready for an eight-voice harmonizer demo / VST3? | **Quality yes, parameter surface no.** The measured quality (cold start ≤ 0.018 c, unity THD+N −153 dB, exact stereo isolation, 18.5× RT at 8-voice worst-case input) is demo-grade; the API still lacks **per-voice intervals/levels/delays, wet/dry, bypass and parameter smoothing** (the per-voice engine state exists — it is an API gap, not a redesign). A concrete three-phase plan (API completion → vst3sdk wrapper behind `-DBUILD_VST3=ON` → host verification) is in `native/VST3_READINESS.md`; the SDK is not present locally but its repository is reachable (verified), and no wrapper was built this round — no placeholder. |

---

## 2. Priority 1 — the Round-13 evidence corrections (with change history)

### 2.1 File count and verification instructions

* **Before:** `DELIVERY_ROUND13.md` said "305 files (round 12: 285)" and
  instructed `sha256sum -c checksums.txt # run from inside
  Octavox_RE_evidence/`.
* **Actual structure (OBS):** the archive root contains `checksums.txt`
  (308 records, CRLF, paths relative to `Octavox_RE_evidence/`) **next to**
  the `Octavox_RE_evidence/` folder holding the 308 project files — 309 zip
  entries total. The instruction as written could not work (the manifest is
  one level above the project tree).
* **After (corrected in the round-14 tree, annotated in place):** count
  corrected to 308 records / 309 entries; verification command now
  `cd Octavox_RE_evidence && sha256sum -c ../checksums.txt` (and the
  round-14 archive repeats the manifest convention, documented in
  `DELIVERY_ROUND14.md`).
* **Change history:** `DELIVERY_ROUND13.md` §Contents and §Verification carry
  `[ROUND-14 CORRECTION]` blocks stating the original text and the fix; the
  round-13 ZIP itself is byte-preserved (`234ad572…` re-verified).

### 2.2 Cold-start timing: first reliable estimate vs commit vs block observation

Machine-readable reconciliation (`r14_coldstart_unified.json`, every
cold-start case carries all three):

| quantity | value (48 kHz, every ratio) | evidence |
|---|---|---|
| first reliable pitch estimate (window fill) | stream sample 2047/2048 → **42.7 ms** | detector geometry (2048-sample analysis window; control boundary at 2047 pre-push) |
| **actual spacing commit** (engine-internal) | stream sample **2175** → **45.3125 ms** | `voiceStats(0).last_commit_sample` = 2175 (engine's own record); the third consecutive misaligned estimate (control interval 64 → estimates at 2047, 2111, 2175; `align_persist` = 3) |
| cold-start harness observation | block end 2304 → **48.0 ms** | the r13 harness polls the commit count after each 256-sample block — the commit at 2175 is first *visible* at the 2304 boundary |

The round-13 findings' "45.3 ms at every ratio" (from the acceptance suite's
engine-internal record) and the cold-start JSON's "48 ms" (block
observation) therefore never contradicted each other physically — but the
report did not reconcile them, and its "window fill + 3 persist intervals
(≈4 ms)" arithmetic was wrong (the true first-estimate→commit gap is **2
intervals = 128 samples ≈ 2.65 ms**). Both documents now carry the
correction; the +1200 c case commits earlier (42.67 ms, the startup wrap
guard) as already reported.

### 2.3 Pitch measurement: legacy log-grid meter vs corrected meter

Confirmed: `r13_coldstart_harness.cpp` retained the **legacy log-grid**
routine (0.001 log2-step grid = 1.2 c quantisation, best-grid-point return,
**no parabolic refinement** — despite its comment), while the acceptance
suite used the corrected log-domain parabolic meter. Resolution chosen:
**both unify and mark** —

* every cold-start case is now co-measured with **both** meters on the same
  signal (`pitch_error_cents` = corrected, `pitch_error_cents_legacy_meter`
  = legacy verbatim), and the legacy values **reproduce the shipped
  `r13_coldstart_after.json` exactly** (e.g. +0.5958 c for the +0 c case)
  — proof the harness is measurement-compatible with round 13;
* meter self-check on pure reference tones: **corrected ≤ 0.082 c**,
  **legacy ≤ 0.596 c, mean +0.102 c** (the 1.2 c grid is directly visible
  in the legacy error histogram);
* the round-13 cold-start pitch values are thereby **explicitly marked as
  legacy-meter values** in the corrected §2.3 of the round-13 findings, and
  the honest engine floor is re-measured: **≤ 0.018 c across the whole cent
  matrix** (was reported as "≤ 0.31 c meter-bound").

### 2.4 The suite meter's window-placement bug (found while unifying)

The acceptance suite's `pitchFFT` computed `off = n − N` with
`N = next_pow2(n)`: for any **non-power-of-two** window (e.g. 1.5 s = 72000
samples at 48 kHz) N > n and `off` went **negative — the meter read samples
before the pointer it was given**. Probe (archived in
`r14_interp_compare.txt` §0b and the JSON's `anomalies` record): handed a
quiet 220 Hz window preceded by a loud 440 Hz region, the verbatim meter
returned **440.0 Hz (+1200 c error)**; the zero-padded fix returned
−0.012 c.

* **Fix:** place the given samples at the END of the window with zero
  head-padding ("newest N samples" semantics, `tests/test_main.cpp`,
  annotated in place).
* **Impact on round-13 results:** every affected suite window was preceded
  by the same sustained material it measured, so the informational pitch
  values were biased only at the ≤ 0.05 c level — but the
  `near_unity_*_via_unity` transition cases read pre-window **unity-segment**
  material (e.g. −10 c via unity: 0.41 c → **0.01 c**), and
  `voices_8_spread_+700c`'s informational `dominant_f0` moved 164.8 →
  415.3 Hz (out-of-window read).
* **Re-run:** 105/105, **identical verdicts**; the complete numeric delta
  list (excluding machine-noise timing fields) is archived as
  `r14_native_tests_vs_r13.txt`. The ASan+UBSan build also re-runs clean
  (105/105, zero reports — see §7).

### 2.5 Change-history policy

Corrections were made **in place with explicit `[ROUND-14 CORRECTION]`
annotations** in `DELIVERY_ROUND13.md` (2 items) and
`OCTAVOX_ROUND13_FINDINGS.md` (§2.3 timing + meter provenance; §6.1 constant
decode + field mapping + clamp reading), following the project's established
pattern (round 11 §8 / round 13 §5 precedents). **No raw evidence file was
rewritten** — the shipped round-13 JSONs are untouched; all round-14
re-measurements are new files (`r14_*`). The round-13 archive is preserved
byte-identically and re-verified.

---

## 3. Priority 2 — static DSP reconstruction (summary; details in `evidence/analysis_round14/r14_static_findings.md`)

### 3.1 Tracker input and ratio limiter (P2-A) — closed

* **`xmm1` at 0x1800DFEB1 = `[tracker+0x5FD4]`: the period estimate at the
  2¹¹ scale** (OBS). Writer = tracker state 6 (FUN_18013BBC0 case 6):
  `period = 16384·idx + (refine(cur) − refine(prev))` — the history buffer
  is decimated by 8 (16384 = 8·2048, OBS) and FUN_18013E160 is a parabolic
  sub-entry refinement returning `δ·2^14` — then ×2/×4 by the `[T+0x5F14]`
  cfg, a reject-hold (`[T+0x5F30] ≥ 0` keeps the old estimate), a gate, and
  an octave guard (`< 2048·50 → ×2`). Units: **input samples × 2¹¹** — the
  reader's ×2⁻¹¹ (round 12) recovers samples; the 2048.0 in the divisor
  chain cancels the scale, so **+0x606C = ω = 2π/T̂ (rad/sample)**.
* **Constant correction:** 0x180412400 = the **double 2π**
  (0x401921FB54442D18); "3370280550400.0" was the f32 view of the low half
  (a round-13 decode error, now annotated).
* **Limiter semantics (all OBS, §1.3 of the static findings):** target
  `+0x6074` = `f3/(pivot·ω)` (or 1.0 when confidence ≤ 6e-6); per-candidate
  enable = bit (id1+1) of `[T+0x6038]`; dead-band `+0x604C/+0x6050` =
  2^(±cents/1200) (m01 id 0x10) — inside the band the effective value is
  1.0; rate limiter: `ratio = effective/[+0x6070]` clamped to
  `[+0x6048, +0x6044]` = 2^(∓1/(12·rate)) (m01 id 0x11), then
  `[+0x6070] ← ratio·[+0x6070]`; stability counter `+0x6078` (+1 while
  \|Δω\| < 1.6e-4, sat 0x7FFFFF, else 0) narrows the acceptance band
  (×0.56127/×0.44542) when it exceeds the threshold at `[T+0x6038]+4`;
  `[+0x6070]` hard-resets to 1.0f when \|Δω\| ≥ 4e-4; ctor inits:
  `[+0x6070]=1.0f`, `[+0x5FD4]=2048·50`, cand tables = engine port region
  (ctor-zeroed, host-filled); `0x180161700` = memset(0x5EB0) so the
  m01-configured limiter fields above 0x5EB0 have **no other writer**.
* **Consumers & classification:** the recorder (limiter + ring write), the
  m02 meters 0xc/0xd (±1731.234·ln = ±1200·log2 cents — the round-10
  read-outs, now pinned to `+0x6074`/`+0x6070`), and the prepare reader:
  **`rate_slot = page[0]·gain` through the engine+0x4128 vector — the
  round-9 law `slot_4128 = (ratio/16)·correction` closes end-to-end**. The
  limiter fields control **pitch correction only**; re-arming consumes the
  ring's period field (`v = ring[base]·2⁻¹¹`, `max(v,48)` with a 480
  degenerate fallback — **no upper clamp**), and the ring's interval field
  indexes a per-voice table. The 4th ring field (id1) has **no observed
  reader** (UNK).

### 3.2 Observed kernel origin (P2-B) — embedded precomputed data

Whole-.text reference scan + write-path check + whole-image duplicate scan +
neighbourhood dump + numerical checks (`r14_scan_kernel_refs.txt`):
**no runtime generation, no copy source — the 256×6 table ships as a unique
.rdata block**, consumed through pointer installs by the pitch engine
(`engine+0x40F0`) and one other object (`[obj+0xD0]`, function
0x1800DBEF0–0x1800DC425, role UNK). Mirror symmetry bit-exact; 4 exact-zero
taps; mantissa-nibble histogram consistent with a one-off float32 design
output (DER). **The exact design formula remains UNK** — the
Blackman-Harris-windowed-sinc characterisation (0.3 % RMS) is an
approximation and is never presented as the formula; with no generator code
and no source table in the image, the design is not provable from the binary
alone.

### 3.3 DSP route audit (P2-C) — `r14_path_matrix.md`

32 edges over the full main graph (input/params → tracker → per-voice →
interpolation → delay/feedback → mixing → output): **27 OBS / 2 DER / 3
UNK**. Ten edges are round-14 closures (the tracker-input identity, the
full limiter algebra, the record field order, the ring→rate-map closure,
the kernel origin) and two standing contradictions are resolved (the
round-13 "×2¹¹ pair on field 0" and the round-12 "[48,480]" clamp). The
remaining UNKs are precisely bounded: the host-side fillers of the candidate
tables (outside .text), the id1 ring field's consumer, the kernel design
formula, the `[T+0x5F14]` cfg multiplier semantics, the second kernel
consumer's role — and all runtime behaviour (the target was never executed).

---

## 4. Priority 3 — interpolation-mode comparison (identical deterministic signals)

Harness: `native/bench/interp_compare.cpp` (new CMake target
`octavox_interp_compare`); results:
`evidence/analysis_round14/r14_interp_compare.json` (machine-readable:
config, seeds, metric definitions, meter tables, all per-mode metrics,
memory, recommendation) + `r14_interp_compare.txt` (human tables). Modes:
**Linear · Lagrange6 (default) · Lagrange6Q · ObsKernel6**; 48 kHz, block
128, library defaults otherwise; fixed LCG seeds; the meter's own error
measured first and subtracted where noted (raw values also reported).

### 4.1 Headline results (worst over the battery; full tables in the artifacts)

| metric | Linear | **Lagrange6 (default)** | Lagrange6Q | ObsKernel6 | material? |
|---|---|---|---|---|---|
| pitch error, LS-refined (c) | 0.0256 | 0.0256 | 0.0255 | 0.0255 | no (spread 2.2e-5 c) |
| steady gain error (dB) | 0.0029 | 0.0022 | 0.0022 | 0.0016 | no (0.0013 dB spread) |
| env modulation (dB) | 0.0318 | 0.0317 | 0.0318 | 0.0318 | no |
| **tone THD+N @ ±1 c (dB)** | −98.4 | **−113.7** | −94.2 | −91.6 | **yes: L6 best by 15.3/19.6/22.1 dB** |
| tone THD+N @ ±10 c (dB) | −93.7 | **−103.7** | −92.1 | −89.8 | yes (converges ≥ ±50 c and at 440 Hz: all ≈ −62 dB) |
| chord residual (dB) | 2.77 | 2.77 | 2.77 | 2.77 | no (engine-dominated) |
| transient disc >4× / step ratio | 0 / 0.909 | 0 / 0.909 | 0 / 0.911 | 0 / 0.911 | zero discontinuities, all modes |
| DC settle ripple (dB) | 0 | 0 | 0 | **−78.3** | real (observed kernel's tap-sum ≠ 1) but ≪ −60 dB bar |
| noise RMS gain (dB) | −2.75 | −1.84 | −1.84 | −2.83 | dual-tap class by design |
| noise 12–24k tilt vs 0–4k (dB) | +2.85 | +1.31 | +1.31 | +3.33 | **yes: +1.54 / −0.00 / +2.02 vs default** |
| stereo leakage | exact 0 | exact 0 | exact 0 | exact 0 | structural tie |
| multivoice (8v, spread, 1.5×) | engine-identical across modes (spreads ≤ 4e-4 dB) | | | | no |
| CPU ns/frame (noise 1v/8v, tone 1v/8v) | 888/1037/244/381 | 919/1125/253/450 | 898/1104/251/440 | 911/1127/252/457 | Linear −15.4 %..−3.3 %; Q −2.3 %..−0.6 %; Obs −0.9 %..+1.5 % |
| steady memory (VmRSS Δ per prepare) | 0 | 0 | 0 | 0 | tables shared; OBS kernel is .rodata |

Meter self-check: log-parabolic ≤ **0.082 c**; legacy log-grid ≤
**0.596 c** (mean +0.102 c) — the same quantification that anchors the §2.3
correction. The ±1 c THD+N result is a **phase-resolution** effect: the
2048-bin Lagrange6 table beats the 256-bin tables (Lagrange6Q, ObsKernel6)
by ~20 dB there — kernel order is not the differentiator.

### 4.2 Decision

**The default stays `Interpolation::Lagrange6`** — it is the measured
quality leader exactly where the interpolator is the bottleneck
(|shift| ≤ ~20 c and low-frequency content) and is never materially worse
anywhere except Linear's CPU advantage. No new opt-in mode is implemented:
all four modes already exist in the API and remain available
(`Config::interpolation`), and the round's measurement does not identify a
new algorithm worth adding — the honest conclusion is that the current
implementation should remain unchanged, with the tradeoffs stated above.
**ObsKernel6 is judged only on measured performance** (per the mandate): it
shows **no measured advantage on any metric** in this battery (+22.1 dB
worst-case THD+N, +2.0 dB HF tilt, −78 dB DC ripple, −0.99 dB extra noise
attenuation, costliest per-frame at 8 voices) — its value is target-parity
research, which is precisely why it stays opt-in and clearly labelled.

---

## 5. Priority 4 — harmonizer readiness and VST3 (summary)

Full assessment: `native/VST3_READINESS.md` (grounded in the actual
`dsp.hpp` API and this sandbox's verified toolchain). Core conclusions:

* **Quality-ready** (measured): cold start ≤ 0.018 c, unity THD+N −153 dB,
  exact structural stereo isolation, deterministic reset, allocation-free
  steady state, 18.5× RT at 8-voice worst-case input (48 kHz), [54.4,
  1200] Hz detector across 44.1–192 kHz.
* **Not parameter-surface-ready**: missing per-voice intervals / levels /
  delays, wet/dry, bypass, parameter-level smoothing (per-voice `ratio`
  exists internally — an API gap, not a redesign). The minimum set for a
  useful demo is enumerated with a three-phase plan (API completion with
  gates → vst3sdk wrapper behind `-DBUILD_VST3=ON` → host verification).
* **Toolchain (verified):** CMake 4.4.4 + GCC 14.2 (C++17) present; VST3
  SDK absent locally but `github.com/steinbergmedia/vst3sdk` reachable
  (HTTP 200, verified 2026-10-26; GPLv3-compatible); the SDK's headless
  `validator`/`hostemu` can gate conformance inside this sandbox. **No
  wrapper was built this round** — per the mandate, no placeholder.

---

## 6. Measurement honesty notes

* The corrected log-domain parabolic meter's own error (≤ 0.082 c on this
  battery's tones) is reported alongside every pitch result; the LS-refined
  (meter-free) values are also shipped. The legacy meter's bias (≤ 0.596 c)
  is quantified rather than discarded, because the round-13 cold-start
  values were produced with it.
* The window-placement fix is disclosed with its probe (+1200 c failure
  case) and its full before/after impact list (`r14_native_tests_vs_r13.txt`)
  — including the two informational values that moved materially.
* Noise through the dual-tap blend is ~−3 dB RMS by design (uncorrelated
  blend class); chords beat on their own; the multivoice env-mod 14.8 dB is
  the spread voices' beating, identical across modes (engine property).
* The ObsKernel6 DC ripple (−78.3 dB) is a property of the **observed
  .rdata kernel data** (tap-weight sum ≠ 1) — documented, not hidden.
* Suite durations use getrusage CPU time; hardware disclosure: 2-core x86_64
  sandbox; all timings machine-dependent and carried per-case in the JSONs.
* Static conclusions are never presented as runtime claims about the
  original plugin; the target was read only, never executed, digest
  verified before/after every scan.

---

## 7. Artifacts, re-runs and provenance

* **Round-14 raw evidence** (`evidence/analysis_round14/`): static —
  `r14_disas_tracker_full.py/.txt`, `r14_scan_limiter.py/.txt`,
  `r14_scan_kernel_refs.py/.txt`, `r14_static_findings.md`,
  `r14_path_matrix.md`; corrections — `r14_coldstart_harness.cpp`,
  `r14_coldstart_unified.json` (67 cases: meter self-check 11, cold-start
  matrix 15, reset/restart 13, rate battery 28), `r14_native_tests.json`
  (105/105 release re-run, fixed meter), `r14_native_tests.txt`,
  `r14_native_tests_vs_r13.txt` (the before/after delta list);
  interpolation — `r14_interp_compare.json/.txt` (via the new
  `octavox_interp_compare` target).
* **Native tree updates:** `tests/test_main.cpp` (meter window fix,
  annotated), `bench/interp_compare.cpp` + CMake target (measurement-only),
  `VST3_READINESS.md`; **the DSP core (`src/dsp.cpp`, `include`/
  `octavox/dsp.hpp`) is functionally unchanged this round** — the only edit
  is the version string (13.0.0 → 14.0.0), verified by diff against the
  round-13 archive; CMake version bumped to 14.0.0.
* **ASan+UBSan re-run:** 105/105, zero sanitizer reports (89.7 s) —
  `r14_native_tests_asan.json/.txt`.
* **Round-11/12 suites re-run from the round-14 tree (mandate):**
  `r11_numeric_audit.py` exit 0; `r11_acceptance_tests.py` 45/45;
  `r12_native_metrics.py` 15/15 gates — archived as `r14_rerun_*` with the
  untouched originals restored (byte-identical, manifest-verified).
* **Canonical docs updated** (explicit addenda, historical values corrected
  in place only with visible `[ROUND-14 CORRECTION]` markers):
  `OCTAVOX_ROUND13_FINDINGS.md` (§2.3, §6.1), `DELIVERY_ROUND13.md`,
  `OCTAVOX_DSP_ARCHITECTURE.md` (§11), `OCTAVOX_CONTROL_FLOW_MATRIX.md`
  (§15), `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (R-80…R-88 + round-14
  correction log), `OCTAVOX_OPEN_QUESTIONS.md` (Q-18 status),
  `OCTAVOX_DSP_DEMO_USAGE.md` (§11), `README.md` (round-14 section),
  `native/README.md` / `native/ARCHITECTURE.md` (round-14 notes).
* **Provenance:** round-13 zip `234ad572…` verified intact on arrival;
  target digest `7bb260f6…` re-verified before and after every scan (the
  binary was only ever read); the Python reference implementations are
  byte-identical to the round-12/13 archives (manifest-verified); the
  round-13 archive is preserved unchanged in the delivery set.
* No completeness claim is made; the UNK list in §3.3 is the honest residue.
