# OCTAVOX ROUND 12 FINDINGS

**Static DSP reconstruction advance + the independent native C++17 DSP core
with the unity artefact fixed, validated and benchmarked**

Continued from `Octavox_RE_evidence_round11.zip` (SHA-256
`6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b`, verified
intact on arrival; 255/255 manifest records OK). Static analysis + numerical
work + our own native implementation only; **the target was never executed
and never modified**. Labels: **OBS** observed · **DER** derived · **UNK** open.

> **Environment note:** the original `Ox.vst3` (SHA-256
> `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`, verified
> before every scan) **is present** this round, so the static work below reads
> the binary directly (read-only capstone scans; scripts and raw outputs in
> `evidence/analysis_round12/`). The binary was never executed — no Windows
> host is available and none was attempted.

---

## 1. Verdict

| question | answer |
|---|---|
| What does the engine's `+0x40f0` interpolator really look like? | **A static 256×6-float table in `.rdata` at VA `0x1803B6170`**, installed by `FUN_1800E18B0` (not the ctor), floor-indexed by `floor(frac·256)` — and it is **NOT 6-point Lagrange**: a designed symmetric lowpass kernel with bin-centre phases `(r+0.5)/256`, row sums 1.0000–1.0001, mirror-symmetric rows (OBS; bit-exact extraction shipped). The member-map label "Lagrange" is corrected. |
| Does the target sit in a two-tap blend at unity (Q-14 premise)? | **No — architecturally it cannot.** The prepare's settled path computes exactly **ONE** 6-tap interpolated read per output sample (instruction-level proof); the crossfade path runs only transiently after a re-arm and settles back to single-tap. At exact unity the rate step is exactly 1.0, `pos` is frozen (`+4` per block vs `−rate·4`), the re-arm trigger (`pos − delay < 8.000000000000007`) never fires → **a fixed fractional delay, no comb** (DER, strong static evidence; runtime audibility stays UNK). |
| The crossfade mechanics, fully? | One-shot **linear** fade `out = tapA + (tapA − tapB)·state`, `state: 0 → −1` decremented per output sample by `page[12+idx]·2^-23` (slot `+0x4110`, init `0.06249988`); re-arm jump `floor(2^(page[12+idx] & 0xF)·v + 0.5)` with `v = clamp(ring[recidx]·2^-11, ≥ 48; 480 if ≈0)`, halved if it would exceed `mask − page[11]`; tapA sits one **record interval** earlier (`ring[recidx+1]`) — a period-aligned hand-off (OBS). |
| Was the unity comb in our demo fixed? | **Yes — bit-exact.** The native core engages a single-tap path when a voice's target ratio is **exactly 1.0** (bit-exact compare), parks on the nearest **integer** delay, and morphs in/out over 10 ms. At exact unity the output equals the delayed input **to the last bit** (`out[k] == in[k − d_park + 1]`, gate = 0.000000e+00) for tones, noise, chords, silence, −60 dB input and arrival from +400 cents. Unity comb across an 8-tone battery: **0.0000 dB spread (native) vs 16.43 dB (the Python v2 reference, the BEFORE)**. |
| Is there a bypass region? | **No.** Engagement keys on bit-exact `ratio_t == 1.0` only; ±1 cent gets full processing and measurably shifts (+1 c → +1.41 c measured, −1 c → −1.54 c). Near-unity battery pitch errors ≤ 0.55 cents (C++ harness) / ≤ 0.50 cents (FFT cross-measurement). |
| Does the native core build reproducibly and validate? | **Yes.** CMake + GCC 14.2, C++17: release tests **51/51**, ASan+UBSan build **51/51 with zero sanitizer reports**, block-size invariance to exactly 0 difference, zero steady-state allocations (global-new counter), stereo isolation **bit-identical** whether or not the other channel is processed. |
| How fast is it vs the Python reference? | Identical deterministic LCG input, same machine: native ≈ **15.5× realtime** (1 voice) / **13.9×** (8 voices) mono at the default 64-sample control rate, ≈ **51× realtime** with the control interval at 512 (the YIN detector is ~73 % of the cost); the Python reference runs at **0.42× / 0.25× realtime** — the native core is **≈ 35–55× faster**. Peak RSS ≈ 8.6 MB mono / 12.7 MB stereo (2 MiB ring per engine). |
| Do the round-11 suites still reproduce? | **Yes** — numeric audit exit 0 (all 14 verdicts PASS), acceptance suite **45/45**, both re-run from the delivered package this round. |

---

## 2. Static reconstruction (scans A–D; raw evidence in `evidence/analysis_round12/`)

All addresses are VA in the shipped image; every claim below was regenerated
after fixing a PE-parser field-order bug that desynced the first scan pass
(cross-checked against the round-6/9 REA decompiles of the same function).

### 2.1 The interpolator is static `.rdata`, 256×6, non-Lagrange (OBS)

`FUN_1800E18B0` — the same function that stores the five quartic coefficients
— installs the table pointer:

```
0x1800E1F82  mov dword [rsi+0x40d8], 0x3e800000   ; c0 (rounds 8-10)
0x1800E1F8C  mov dword [rsi+0x40dc], 0x3eb12e86   ; c1
0x1800E1F96  mov dword [rsi+0x40e0], 0x3e79c4bb   ; c2
0x1800E1FA0  mov dword [rsi+0x40e4], 0x3df569c6   ; c3
0x1800E1FAA  mov dword [rsi+0x40e8], 0x3d0ed15e   ; c4
0x1800E1FB4  lea  rax, [rip+0x2d41b5]             ; -> 0x1803B6170
0x1800E1FBB  mov qword [rsi+0x40f0], rax          ; engine+0x40f0 = table
```

Phase law (prepare, `0x1800E2490–0x1800E24AD`): `phase =
(((int)(pos·2048.0)) >> 3) & 0xFF` = `floor(frac(pos)·256)` with the mask
`0x7F8` at `engine+0x4098`. Kernel properties (bit-exact extraction,
`r12_interp_table.txt` + `r12_obs_kernel_table.h`):

* 256 rows × 6 taps, 0x18-byte stride — matching the prepare's tap reads at
  `+0/+4/+8/+0xc/+0x10/+0x14`;
* row sums 1.000000–1.000122 (unity DC gain); effective fractional delay
  sweeps 0.0020 → 0.9980 samples — **bin-centre phases `(r+0.5)/256`** (DER);
* rows mirror-symmetric (row 255 = reverse of row 0);
* **not** 6-point Lagrange (best-aligned L2 mismatch 0.11–0.19 rms/row) — a
  designed symmetric lowpass kernel, windowed-sinc-like (DER).

The native core offers this exact table as the opt-in
`Interpolation::ObsKernel6` mode (floor-indexed, 256 phases — the target's
own phase law) alongside our own Lagrange tables; the default stays our
6-point Lagrange 2048-phase table (Python v2 parity). This is OBSERVED DATA
reuse for research comparison, clearly labelled — the same practice as the
quartic coefficients in rounds 9–11.

### 2.2 The settled path is a single interpolated tap (OBS, instruction level)

The branch at `0x1800E2470/79` (`(int)state[v] == −1` → settled): the loop
`0x1800E2490–0x1800E25D5` computes per output sample **one** 6-tap weighted
sum (one phase row applied to six consecutive work-ring samples at
`(int)pos + base`), advances `pos` by `−rate` (clamped ≥ 0), writes one
output float. There is no second tap and no blend in the settled path. The
crossfade path (`0x1800E2812+`) computes two reads — tapA at
`ringbase − interval`, tapB at `ringbase` — and mixes
`out = tapA + (tapA − tapB)·state`, `state ∈ (−1, 0]`.

### 2.3 Q-14's premise answered statically (DER)

At exact unity: `rate = 4·P(0)·1·1 = 1.0` **bit-exact** (`P(0) = 0.25`);
`pos += 4.0` per 4-sample block (const at `0x1804123E8`, added at
`0x1800E224D`) and `pos −= rate` per output sample — net motion **zero**; the
re-arm trigger `pos − delay < 8.000000000000007` (const `0x180412428`) never
fires; the engine stays in the settled single-tap path forever. **The target's
rest state at unity is one fixed 6-tap read — a fixed fractional delay with
no two-tap comb.** Our reference demo's −2.42 dB unity comb is a property of
*our* continuous complementary blend, which the target does not use at rest.
The runtime audibility of the whole path remains UNK (never executed) — this
is the static answer, and it is what the round-12 native fix implements.

### 2.4 Crossfade mechanics decoded (OBS)

After each settled 4-sample block (`0x1800E25DB+`): record read at
`(recidx[v] (+0x42f8) + wdelta (+0xa4fc)) & 0xFFF`;
`v = clamp(ring[base]·2^-11, ≥ 48.0; 480.0 if ≈0)`; `interval =
(int)ring[base+1]`; `correction = ring[base+3]` (**confirms round 9's slot
law at instruction level**); `idx = clamp(int((v−48)/16), 0, 31)`;
`slot+0x4110[v] = page[12+idx]·2^-23` (the fade decrement per output sample);
re-arm jump `floor(2^((int)page[12+idx] & 0xF)·v + 0.5)`, halved when
`jump ≥ mask − page[11]`; on re-arm `delay[v] = pos ∓ jump`, `state[v] = 0`.
The fade hands off between taps **one record interval apart** — the same
period-alignment idea our v2 implements independently by committing
`S = k·T̂`. The record ring's first-field scaling (what makes `v ≥ 48` for
musical periods) stays **UNK**.

### 2.5 Complete rip-constant map of the prepare (OBS)

`2^-23 @0x180411E54`, `2^-11 @0x180411E94`, `1/16 @0x180411F68`,
`0.5 @0x180412058`, `1.0 @0x18041213C`, `4.0 @0x1804123E8`,
`8.000000000000007 @0x180412428`, `64.0 @0x180412490`, `2048.0 @0x180412558`,
`31.0 @0x180412660`, `48.0 @0x180412688`, `480.0 @0x180412730`,
`eps 7.105427357601002e-15 @0x180411E44`, `-1.0 @0x18041280C` — plus the
ctor facts: work-ring stride by sample rate (`<50k → 0x1000`, `50–100k →
0x2000`, `≥100k → 0x4000` floats/voice) and the `+0x4110` init
`0x3D7FFFE0 = 0.06249988`.

---

## 3. The native core and the unity fix

**Deliverable**: `native/` — C++17 static library + test harness + benchmark +
WAV→WAV CLI (CMake ≥ 3.16, GCC 14.2; a VST3 wrapper was deliberately out of
scope). The Python implementations are preserved untouched as reference
baselines (`poc/octavox_poc.py`, `octavox_offline.py`, `octavox_v2.py`).
API: `AlignedDelayLine` (mono engine, 1..8 voices) and `MultichannelShifter`
(one engine **per channel** — channel isolation is structural); explicit
`prepare()` (all allocation), `reset()`, parameter setters and planar
`process()`; no allocation, locks or blocking in steady state (verified).
Full design note: `native/ARCHITECTURE.md`.

**The unity engagement** (the round-12 fix, mirroring the target's settled
architecture from §2.3): per voice, when `ratio_t == 1.0` **bit-exactly**, the
two-tap blend morphs to tap A alone over 10 ms (`wA_eff = e·w + 1−e`,
`wB_eff = e·(1−w)` — constant unity weight sum), then parks on the nearest
**integer** delay inside `[base, base+S)`, where the read takes the direct
sample path — so exact unity is a **bit-exact delayed copy** of the input.
Departure (any ratio ≠ 1.0) reverses the morphs and additionally commits the
period-aligned spacing `S ← S_target` when the read point is preserved
exactly (`frac·S_old/S_new ∈ [0,1)`) — tap B's jump is masked at its ~zero
weight. No bypass region exists: engagement keys on the single bit-exact
value 1.0; ±1 cent is fully processed.

Also carried over from the v2 with one improvement: the control-rate detector
fires every 64 **stream** samples, making the output invariant to the
caller's block splits (the Python restarts its control loop per call). The
one documented behavioural delta vs the Python is a ≤ one-interval control
timing lag (a streaming API cannot reproduce the reference's intra-block
look-ahead); class-level behaviour is unaffected (§4).

---

## 4. Validation (all machine-readable; JSON in `evidence/analysis_round12/`)

**Correctness + unity regression — `octavox_tests` (51 cases, exit-code
gated)**: release build **51/51**, ASan+UBSan build **51/51, zero sanitizer
reports**. Coverage: mono/stereo, 1/8 voices, the full cent battery
(0, ±1, ±10, ±50, ±100, ±500, ±1200), silence, −60 dB level, tones, chords,
transient bursts, noise, chirps, DC, 44.1/48/96 kHz, block sizes
1/7/16/64/128/512/1024/4096/mixed, repeated prepare, reset determinism,
60 s 8-voice feedback stability, impulse latency, NaN/Inf gates, clip
behaviour, 16/24-bit WAV I/O round-trips, steady-state allocation-freedom,
block-size invariance (max difference **exactly 0**), stereo isolation
(R-channel output **bit-identical** with/without L processing; exactly zero
for silent input). Unity gates: bit-exact delayed copy (max diff
**0.000000e+00**) for 220 Hz/1100 Hz tones, LCG noise, a 4-tone chord,
silence, −60 dB input, and arrival from +400 cents; the 8-voice unison mix
equals the exact float-rounded mean of the 8 delayed copies; ±1 cent really
shifts (+1.41/−1.54 c measured); entry/exit transitions click-free
(transition max step 0.0180 vs 0.0181 pre-transition on a 0.5-amplitude tone).

**Cross-language metrics — `r12_native_metrics.py` (15/15 gates)**, the
native CLI vs the preserved Python v2 on identical inputs, measured with the
same metric code:

| metric | native core | Python v2 reference |
|---|---|---|
| unity comb, gain spread over 8 tones (110–1320 Hz) | **0.0000 dB** | **16.43 dB** (−16.71 … −0.28 dB; the documented two-tap comb — BEFORE) |
| pitch error, cent battery @220 Hz | ≤ 0.50 cents (worst −1200 c) | (r11 values: ≤ 0.08 c — same class) |
| env. modulation (period-sync) | 0.00–0.88 dB across the battery | (r11: 0.00 dB at the aligned settings) |
| stereo isolation, L tone / R silence, +400 c | R peak = **exact 0** | exact 0 (the r11 per-channel fix) |

**Benchmark — `octavox_bench` + Python timing, identical LCG noise input**:
native 1346–1352 ns/frame (1 v) and 1503–1552 ns/frame (8 v) at the 64-sample
control rate → **15.5× / 13.9× realtime**; stereo 2592/3089 ns/frame
(8.0× / 6.7×); **410 ns/frame → 50.9× realtime** with the control interval at
512 — the YIN detector is ~73 % of the runtime at the default rate. The
Python v2 reference: 50 084 / 84 240 ns/frame → **0.42× / 0.25× realtime**.
Same machine, same input: **≈ 35–55× speedup**. Zero steady-state allocations
in every configuration; peak RSS 8.6 MB (mono) / 12.7 MB (stereo).

**Honest limits measured and kept visible**: at 96 kHz the 2048-sample
detector window cannot bracket 60 Hz, so the spacing falls back to `G`
(reference-parity behaviour) — pitch error −13.8 c at +400 c/96 kHz (gated
loosely, reported); a *fresh* near-unity engine (never at exact unity) keeps
the static two-tap comb for periodic input until its first wrap (−2.41 dB at
220 Hz measured — the exit-commit removes it after any departure from exact
unity: gain −0.001 dB measured after unity→±50 c).

---

## 5. Round-11 suite re-runs (task requirement)

Re-run from the delivered package on this environment:

* `r11_numeric_audit.py`: **exit 0** — all 14 verdicts PASS (49.939/199.923 ¢
  examples, 0.10131 ¢ interior maximum, residual direction, F-range proofs,
  excess absorption bounds).
* `r11_acceptance_tests.py`: **45/45 pass, exit 0** (~2.5 min) — including the
  reference CLI's −0.44 dB stereo defect reproduction and the v2 fix at
  −234 dB, latency 102.5 ms, and the 8-voice costs (4.30 s/s reference,
  3.79 s/s aligned).

---

## 6. Static findings versus runtime unknowns (policy unchanged)

* The target was **never executed** in any round; nothing here changes that.
  All §2 results are static reads of the shipped image; all §3–§4 numbers
  measure **our own** implementations. No PoC result is substituted for a
  target measurement, and the native core is **not** claimed to be a VST3
  plugin or to reproduce the original's runtime behaviour.
* **UNK after this round**: the runtime audibility of the target's
  settled/crossfade behaviour (Q-14's audible question — the static premise
  is now answered); the record-ring first-field scaling that makes the
  re-arm base `v ≥ 48` (§2.4); the exact design formula of the observed
  kernel (structure and values are OBS; "windowed-sinc-like" is DER); walk
  statistics and correction convergence (round-9/10 residuals); the m02
  double-conversion runtime behaviour (Q-15, DER prediction stands); the
  host-reported latency value (Q-8, negative result stands).
* The controlled Windows-host tests that would close the runtime questions
  remain those listed in `OCTAVOX_ROUND11_FINDINGS.md` §6; none were
  attempted this round.

---

## 7. Artifacts and provenance

* **Native implementation** (`native/`): `include/octavox/dsp.hpp`,
  `src/dsp.cpp`, `src/obs_kernel_table.h` (OBS data, bit-exact extraction),
  `tests/` (51-case suite + WAV I/O + alloc counter), `bench/`, `cli/`,
  `CMakeLists.txt`, `README.md`, `ARCHITECTURE.md`. Build directory
  (`build*`) intentionally **not** shipped.
* **Round-12 raw evidence** (`evidence/analysis_round12/`):
  `r12_scan_table.py/.txt` (displacement scans), `r12_prepare_disasm.py/.txt`
  (annotated prepare disassembly), `r12_constants_and_table.py/.txt`
  (rip-constant map + ctor search), `r12_interp_table.py/.txt` (kernel
  analysis), `r12_obs_kernel_table.h` (generated extraction),
  `r12_static_findings.txt` (the §2 evidence summary),
  `r12_native_tests.json/.txt` + `r12_native_tests_asan.json` (51/51 both),
  `r12_native_bench.json`, `r12_native_metrics.py` +
  `r12_native_metrics_results.json/.txt` (15/15 gates).
* **Canonical docs updated**: `OCTAVOX_DSP_ARCHITECTURE.md` (S5/S6 rewrite
  from the instruction-level evidence, member-map corrections, §9 round-12
  addendum), `OCTAVOX_CONTROL_FLOW_MATRIX.md` (§13 addendum),
  `OCTAVOX_OPEN_QUESTIONS.md` (Q-14 static advance, new residuals),
  `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (R-62…R-70 + round-12 log),
  `OCTAVOX_DSP_DEMO_USAGE.md` (native build/test/bench instructions),
  `README.md` (round-12 section). Historical values were corrected **in
  place with explicit changelogs**; no raw evidence file was rewritten.
* **Provenance**: round-11 zip `6ff8173a…` verified intact on arrival;
  target digest `7bb260f6…` re-verified before and after every scan (the
  binary was only ever read); the Python reference implementations are
  byte-identical to the delivered package (manifest-verified).
* No completeness claim is made; the UNK list above is the honest residue.
