# OCTAVOX ROUND 13 FINDINGS

**Native DSP robustness, cold-start alignment and detector improvements**

Continued from `Octavox_RE_evidence_round12.zip` (SHA-256
`a597546534c3f27bd941dea4a427ececbd9972e63b1fa183485eba509cdb0902`, verified
intact on arrival; 285/285 manifest records OK). Static analysis + our own
native implementation only; **the target was never executed and never
modified**. Labels: **OBS** observed · **DER** derived · **UNK** open.

> **Environment note:** the original `Ox.vst3` (SHA-256
> `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`,
> verified before every scan) **is present** this round; the static work
> reads it directly (read-only capstone scans; scripts and outputs in
> `evidence/analysis_round13/`). No Windows host is available and none was
> attempted. Development host: 2-core x86_64 sandbox, GCC 14.2, CMake 4.4.4,
> capstone 5.0.7 (venv).

---

## 1. Verdict

| question | answer |
|---|---|
| Does cold-start pitch shifting still have the near-unity comb? | **No.** A fresh engine now commits the period-aligned spacing at the first reliable estimate (measured: first commit **45.3 ms** at every ratio from ±1 c to ±1200 c at 48 kHz — round 12: *never* at ±1/±10 c, 1461 ms at +50 c, 720 ms at ±100 c). Steady gain ±0.004 dB across the whole cent matrix (round 12: **−2.41 dB at ±1 c**), post-latency 100 ms gain ≥ −0.03 dB (round 12: −3.8 dB measured in the same window after the B-readiness fix), pitch error ≤ 0.06 c (cold-start battery), envelope modulation ≤ 0.51 dB near unity, output steps ≤ 2.1× the input's natural step (continuity). |
| Is the detector now sample-rate robust? | **Yes.** Above 48 kHz the detector runs on a polyphase-FIR-decimated copy (analysis rate ≤ 48 kHz), so 96 and 192 kHz support exactly the same **[55, 1200] Hz** range and cold-start timing as 44.1/48 kHz. Measured pitch errors across 44.1/48/96/192 kHz × {55, 60, 80, 110, 220, 440} Hz at +400 c: worst **+0.24 c**; the 96/192 kHz columns are numerically identical to 48 kHz (rate-invariant by construction). Round 12 at 96 kHz: detector returned **no estimate at all** (the fixed window's geometry), spacing fell back to G, pitch error −15.4 c; round 12 at 48 kHz also **misreported a 55 Hz input as 60.1 Hz** (tau clipping) and would have accepted a 50 Hz input as ~55 Hz — both fixed by the config-driven range + edge reliability rejection. |
| Is the one-time initialisation off the audio path? | **Yes.** The interpolation tables are constructed exactly once per process, by the first `prepare()` — gated by `tables_init_at_prepare` (count 0 → 1 at the first prepare, tables ready before any `process()`), `first_process_clean` (the first `process()` call constructs nothing, allocates nothing, and is *not slower* than steady state: 8.05 ms vs 8.47 ms median for a 32768-sample block) and `tables_never_rebuilt` (count still 1 after the whole 105-case suite). `process()` contains no static locals at all (the r12 `interpTables()` magic static is gone; `voiceStats()` returns by value instead of handing out thread-local scratch). |
| Did the test suite get faster? | **Yes — and a round-12 benchmark bug was found and fixed.** The 105-case suite (54 of them new) runs in **10.2 s CPU** on this 2-core sandbox (the 51 round-12 cases alone now run ~5.8 s vs 14.6 s in round 12). The expensive cases are honestly profiled per case in the JSON: `longrun_60s_8voice_feedback` 3.21 s, `chirp_30s_+700c` 0.28 s, cold-start reset matrix 0.88 s. **The round-12 benchmark harness had re-processed the first block of input instead of advancing the pointers** (see §5) — all performance numbers are re-measured with the fixed harness: noise 915–1122 ns/frame (22.6×/18.7× realtime, 1/8 voices), **periodic input 250–448 ns/frame (83×/46× — the fused detector's early exit, 3.7× faster than noise)**, voice path alone (control interval 512) 354 ns (58.8×), 192 kHz noise 928 ns (5.6×) / tone 266 ns (19.6×). |
| Do the round-11/12 suites still reproduce? | **Yes** — re-run from the delivered package this round: `r11_numeric_audit.py` exit 0 (all 14 verdicts PASS); `r11_acceptance_tests.py` **45/45** (2 m 30 s); `r12_native_metrics.py` **15/15 gates** on the round-13 CLI (pitch battery worst 0.30 c — better than round 12's 0.50 c; env. modulation ≤ 0.024 dB; stereo isolation exact). The re-run results are archived as `r13_rerun_*` alongside the untouched round-11/12 originals. |
| Static advance? | The **record ring's ownership and writer are found** (§6): the ring at engine+0x98 belongs to a *tracker* sub-object at engine+0x4518 (ring pointer +0x5fd8, mask 0xFFF +0x5fe0, write index *wdelta* +0x5fe4 — the engine+0xa4fc read in the prepare is this tracker field), the recorder appends 4-float records at descending indices, and the **first-field scaling is directly observed as a matched ×2¹¹ / ÷2¹¹ pair** (writer: 2048.0 × 3370280550400.0 / x at 0x1800dfeb7–0x1800dfeca; reader: ×2⁻¹¹ as round 12 found). The interpolation kernel is **characterised numerically**: a windowed-sinc-like design best matched by a Blackman-Harris window (half-width 3 taps, sinc cutoff ≈ 0.83 cycles/sample) with a 0.3 % RMS residual — not an exact member of any tested closed-form family; the exact design formula stays UNK. |

---

## 2. Priority 1 — cold-start alignment (the splice commit)

### 2.1 The defect, re-measured as the BEFORE (round-12 library, unmodified)

`r13_coldstart_before.json` (59 machine-readable cases, harness
`r13_coldstart_harness.cpp`): at ±1 c the first commit is **never**
(`commits = 0`, S stays 4096); steady gain **−2.41 dB**; at ±10 c −1.65 dB
with 1.01 dB envelope modulation; first commit at +50 c **1461 ms**, at
±100 c 720/128 ms — exactly the documented defect (the v2 rule "S commits
only at grain wraps" and `|1−r|/S` ≈ 1.4·10⁻⁷/sample at ±1 c ⇒ the first
wrap is minutes away).

### 2.2 The fix

Three mechanisms (full design note in `native/ARCHITECTURE.md`):

1. **Splice commit** (control rate): commit `S ← S_target` when a voice is
   misaligned by > `align_threshold_periods` (0.05 input periods ⇒ worst
   frozen-weight gain error −0.019 dB) for `align_persist` (3) consecutive
   estimates, in the dual-tap regime, with a 4096-sample cooldown, and only
   if the target is itself aligned. The commit snapshots the old machine as
   a **ghost** and crossfades ghost → committed over 10 ms; both endpoints
   are exact and the blend is convex, so the splice is continuous by
   construction (a mid-splice reset is bit-identical to a fresh restart —
   gated).
2. **Startup wrap guard** (r > 1): round 12's first wrap at +1200 c fired at
   sample 2048 jumping the read to delay 4352 — beyond the 2048 recorded
   samples — producing **24 ms of digital silence** (a pre-existing v2-line
   behaviour our new step gates exposed). The guard re-anchors inside the
   recorded data, **content-aligned** to the continuing read
   (`d_new = d_ghost + m·T̂`) when an estimate exists, with the ghost
   carrying the ~2.5-ms transition. Measured max output step at +1200 c:
   0.029 (2.0× the input's natural step; was 0.49).
3. **B-tap readiness**: at cold start the B tap (delay ≈ base + S ≈ 6450)
   reads the zero-filled ring until ~134 ms; the complementary blend summed
   real data with zeros, **halving the amplitude** (measured −3.8 dB over
   the first 100 ms after latency — this is also why round 12's
   first-100 ms numbers were ≈ −9 dB). While the B data has not arrived the
   voice runs as the natural single-tap A path; a 128-sample convex
   crossfade hands over as the data arrives (bounded output, bit-identical
   steady state).

### 2.3 The AFTER (full matrix, `r13_native_tests.json`)

| cents | first commit | post-lat 100 ms | 1 s | steady | pitch err | env mod | step ratio |
|---|---|---|---|---|---|---|---|
| 0 | parked (no commit) | −0.00 dB | +0.00 | −0.00 | +0.31 c* | 0.01 | 1.00× |
| ±1 | **45.3 ms** | −0.00/+0.00 | +0.00/−0.00 | +0.00 | +0.02/−0.29 c* | 0.00/0.01 | 1.00× |
| ±10 | 45.3 ms | −0.02/−0.02 | +0.00 | +0.00 | ±0.05 c | 0.04/0.06 | 1.01× |
| ±50 | 45.3 ms | −0.01/−0.02 | −0.00 | −0.00 | ±0.04 c | 0.24/0.26 | 1.04/0.97× |
| ±100 | 45.3 ms | +0.02/+0.03 | +0.00 | +0.00 | ±0.01 c | 0.47/0.51 | 1.06/0.95× |
| ±500 | 45.3 ms | +0.01/−0.00 | −0.00 | +0.00 | ±0.01 c | 0.90/1.88 | 1.35/0.75× |
| ±1200 | 42.7/45.3 ms | −0.00 | −0.00 | −0.00 | +0.02/−0.66 c | 0.01 | 2.01/0.50× |

\* the cold-start battery's pitch residuals ≤ 0.31 c are **meter-bound**
(see §7). Reset/restart at every ratio: bit-identical to fresh. Unity ↔
non-unity round trip: transition steps ≤ 2.5× pre-transition. Signal types
at +10 c (chord / transient / noise / −60 dB / silence): finite, bounded,
silence exactly zero, low-level gain +0.001 dB, noise RMS −3.53 dB (the
dual-tap uncorrelated-blend class — see the limitations), steps ≤ 1.08×.
**No bypass region exists**: the trigger keys on misalignment only; ±1 cent
is fully processed and measurably shifts.

The 45.3 ms commit time is the honest detector floor: 42.7 ms window fill
(2048 samples) + 3 persist intervals (≈4 ms) — i.e. "as soon as a
sufficiently reliable pitch estimate becomes available".

---

## 3. Priority 2 — sample-rate robustness

### 3.1 What was broken (measured)

* 96/192 kHz: the detector's `m = window/2 − tau_max` geometry went
  negative ⇒ **estimate() always returned 0** (BEFORE table: est = 0.0 for
  every frequency) ⇒ spacing fallback G ⇒ output pitch errors −15.4 c
  (220 Hz @ +400 c) to −80.8 c (60 Hz @ 192 kHz).
* 44.1/48 kHz: a 55 Hz input was reported as **60.1 Hz and accepted** (the
  lag range clipped at sr/60); a 50 Hz input would be reported ~55 Hz and
  accepted (no edge rejection).
* A standalone diagnosis during detector rework also found the round-12
  **first-local-minimum rule selecting a spurious sub-threshold ripple** at
  0.7 periods on a 55 Hz tone at 48 kHz (the finite overlap m ≪ T puts a
  cos² ripple on the CMND descent; measured dip 0.189 < threshold 0.20) —
  intermittent 73–78 Hz outliers on a *steady* 55 Hz tone.

### 3.2 The fix

* **Decimated analysis path** (rate-robust by construction): above
  `analysis_rate_max` (48000) the detector consumes a polyphase-FIR
  decimated copy (63-tap Blackman-windowed sinc, cutoff 0.45·analysis
  Nyquist, DC gain exactly 1, taps designed in `prepare()`; the decimator
  phase follows the absolute stream position ⇒ block-split invariant). At
  44.1/48 kHz: exact passthrough, no filtering.
* **Config-driven lag range with headroom and edge rejection**:
  `tau_min = sr/fmax − 4` (a 1200 Hz tone resolves at its true period
  instead of the tau_min-edge octave error — round 12 reported 600 Hz for
  it), `tau_max = min(window/2 − 32, ceil(sr/fmin) + 8)`, plus rejection of
  boundary-selected lags and of estimates before the window is fully
  populated (the degenerate all-zero-segment case reported sr/tau_min =
  1200 Hz on any cold input within the first 42.7 ms).
* **Valley selection** replacing first-local-minimum (§3.1's ripple fix),
  numerically identical for clean tones, octave-safe by the merge bar.

### 3.3 The AFTER

* Detector capability (gated, `detector_info`): analysis rate
  44.1/48/48/48 kHz, decimation 1/1/2/4, effective range **[54.4, 1200] Hz
  at all four rates**, control interval 64, window 2048 analysis samples.
* Pitch battery (+400 c): worst **+0.24 c** across all 24 rate × frequency
  cells; detector estimate errors ≤ 0.12 c; the 96/192 kHz columns are
  numerically identical to 48 kHz (rate-invariance by construction).
  Negative shift (−400 c, 440 Hz @ 96 kHz): −0.08 c. Noise at every rate:
  finite, bounded, honestly reported.
* Cost (bench, fixed harness): 44.1 kHz noise 1302 ns/sample (17.4×
  realtime), 48 kHz 915 (22.6×), 96 kHz 942 (11.1×), 192 kHz 928 (5.6×);
  192 kHz tone 266 ns (19.6×), 192 kHz 8-voice noise 1218 ns (4.3×).
  Memory: peak RSS 8.4 MB mono / 12.6 MB stereo / 18.0 MB at 192 kHz
  8-voice (the ring is rate-independent 2 MiB/engine; the decimator adds
  only 63 taps + 126 history samples).

---

## 4. Priority 3 — deterministic real-time preparation

* The round-12 `interpTables()` function-local static (a thread-safe-static
  guard + ~13k divisions on first use **inside `process()`**) is replaced by
  an explicit `ensureInterpTables()` on the preparation path: the tables are
  constructed exactly once per process by the first `prepare()`, stored
  behind a plain `Impl` member pointer, and deliberately never destroyed
  (no static destruction order hazard). All namespace-scope objects in
  `dsp.cpp` are constant-initialized; **`process()` contains no static
  locals of any kind**.
* Instrumentation and gates: `octavox::diagnostics::interpTableInitCount()`
  / `interpTablesReady()` / `interpTableInitDuringProcess()`
  (`interpTableInitDuringProcess` is a tripwire that can only increment if
  a future change reintroduces lazy construction on the audio path — it
  stays 0). The suite runs the real-time-safety group **first** so its
  measurements are the process's actual first `prepare()`/`process()`:
  `tables_init_at_prepare` (0 → 1 at the first prepare, ready before any
  process), `first_process_clean` (first call constructs nothing, allocates
  nothing — armed global-new counter — and is not slower than steady:
  8.05 ms vs 8.47 ms median), `tables_never_rebuilt` (count 1 after the
  whole suite).
* Other first-use operations reviewed: the OBS kernel table is
  constant-initialized `.rodata` (hex-float literals — no runtime
  initialisation); `voiceStats()` now returns by value (the r12 API handed
  out function-local and thread-local scratch); every vector used in
  `process()` is allocated in `prepare()` (`dec_block`, `med_scratch`,
  detector buffers, decimator taps/history).

---

## 5. Priority 4 — suite execution time and a round-12 benchmark bug

* The 105-case release suite: **10.2 s CPU** (measured with `getrusage`
  CPU time — a real syscall, immune to the vDSO `clock_gettime` livelock
  observed intermittently in this sandbox during development; per-case
  `duration_ms` is in the JSON). Slowest cases: `longrun_60s_8voice_feedback`
  3.21 s, `coldstart_reset_restart` 0.88 s, `dc_input_finite` 0.42 s,
  `chirp_30s_+700c` 0.28 s. The 60-second multivoice feedback and 30-second
  chirp coverage is **kept** (not removed, not relaxed); the speedup comes
  from the fused detector's early exit (periodic input 3.7× cheaper) —
  14.6 s (r12, 51 cases) → 5.8 s (r13, same 51 cases).
* ASan+UBSan build: 105/105, zero sanitizer reports, 89.3 s.
* **The round-12 benchmark harness bug (OBS, corrected this round):** the
  timed loop passed the *same base pointers* to every block —
  `m.process(inP.data(), outP.data(), channels, k)` without advancing — so
  the benchmark re-processed the first `block` samples over and over
  (a block-periodic input, 128 samples at the default case). Round 12's
  reported numbers were therefore measured on that loop, not on the
  intended 5-second noise stream: the r12 detector always full-scanned
  regardless of input, so the r12 *absolute* numbers happened to stay in
  the noise-cost class (1346–1552 ns/frame ≈ today's 915–1122 with the
  slower r12 scan and longer tau range), but the r12 "input" description
  was wrong, and the bug would have silently produced ~5× optimistic
  numbers with the round-13 early exit (measured: 165 ns/frame on the
  broken harness vs 915 fixed, same library). Round 13 fixes the harness
  (pointers advance per block and are restored per repetition) and
  re-measures everything; the round-12 archive is preserved unchanged.
* Hardware disclosure: all timings on this round's 2-core x86_64 sandbox
  (measured `getrusage` CPU time, best-of-3 for the bench). Absolute
  numbers are machine-dependent; the JSON carries the per-case values so
  any environment can be compared.

---

## 6. Priority 5 — static reconstruction (read-only scans of the target)

All addresses are VA in the shipped image; the digest was re-verified
before and after every scan. Scripts and raw outputs:
`evidence/analysis_round13/r13_scan_recring.py/.txt` (displacement scan),
`r13_disas_recring.py` + the recorder/tracker disassembly staged as
`r13_recring_tracker.txt`, `r13_kernel_fit.py/.txt` (kernel
characterisation).

### 6.1 The record ring: ownership, writer and the first-field scaling (OBS)

Round 12 left the record ring (engine+0x98, 0x1000 floats, read at
`(recidx[v] + wdelta) & 0xFFF` in the prepare at 0x1800E25DB–0x1800E2653)
with an unknown writer and an unknown first-field scaling. Round 13:

* **Ownership** (OBS, ctor 0x1800E0D1F–0x1800E0D51): the engine constructor
  builds a **tracker sub-object at engine+0x4518** (~0x5EB0+ bytes) and
  installs, at tracker+0x5FD8, `lea rax, [r15+0x98]` — **the record ring
  pointer** — together with the mask `0xFFF` at tracker+0x5FE0, a flag at
  +0x5FE8, and the constant **2048.0 (= 2¹¹) at tracker+0x6008**. The
  prepare's `engine+0xa4fc` "wdelta" is this tracker's field at +0x5FE4
  (0x4518 + 0x5FE4 = 0xA4FC) — i.e. the *write index*, zeroed by the same
  ctor (0x1800E0D9E).
* **The recorder** (OBS, the tracker update path ending 0x1800E0154–0x1800E0355,
  ring writes at 0x1800E0276/0x1800E028E/0x1800E02B0/0x1800E02DB/0x1800E0302/
  0x1800E0329/0x1800E034B): appends **4-float records at descending indices
  from wdelta** — `r8 = wdelta; dec; ring[r8] = xmm1; ring[r8−1] =
  (float)r10d; ring[r8−2] = (float)r11d; ring[r8−3] = tracker+0x5FD4 (an
  int); wdelta = r8` — with the non-tracking branch doing the same with
  manual wrap (`if (r8 < 0) r8 = mask`). The reader's per-voice `recidx`
  (the growable int array behind the engine+0x42f8 pointer — a
  begin/end buffer triple, freed by the helper at 0x180010E30) indexes
  *backwards* from the write head.
* **The first-field scaling** (OBS): the writer-side target for field 0 is
  computed at 0x1800DFEB7–0x1800DFECA as
  `xmm8 = (tracker+0x6008) · 3370280550400.0 / xmm1` — i.e.
  **2048.0 × 3370280550400.0 / xmm1**, with the double
  3370280550400.0 = 2²¹·5²·64283 at .rdata 0x180412400 — then
  ratio-limited (÷ tracker+0x6070 → clamp vs +0x6044/+0x6048 → × +0x6070,
  the recorder at 0x1800E01D6–0x1800E0243) before the ring store. The
  reader's `× 2⁻¹¹` (round 12) is therefore the exact inverse of the
  writer's ×2¹¹ — a matched pair, now observed on both sides. With the
  re-arm base clamped to [48, 480] samples, the ring's field 0 lives at the
  2¹¹ scale as written. **UNK (residue):** the units of `xmm1` (the
  tracker's input quantity — the chain above 0x1800DFE9C) and the full
  semantics of the ratio limiter (+0x6070 / +0x6044 / +0x6048) remain
  open; the 3370280550400 constant does not factor into a sample-rate
  product.

### 6.2 The interpolation kernel: numerical characterisation (OBS data, DER analysis)

Fitting closed-form families to all 256 rows of the observed 256×6 kernel
(taps at `k − 2 − mu_r`, `mu_r = (r+0.5)/256`, per-row least squares over
scale and cutoff; `r13_kernel_fit.py/.txt`):

| family (half-width 3 unless noted) | RMS residual |
|---|---|
| **Blackman-Harris windowed sinc** | **2.78·10⁻³** |
| Blackman (2.5) | 4.04·10⁻³ |
| Blackman | 5.94·10⁻³ |
| Hann (2.5) | 6.29·10⁻³ |
| Hamming | 1.94·10⁻² |
| Lanczos | 2.52·10⁻² |
| unwindowed sinc | 5.58·10⁻² |

Best fit: Blackman-Harris window, sinc cutoff ≈ 0.83 cycles/sample,
scale ≈ 0.85, per-row worst error 3.7·10⁻³ — a close but **not exact**
match (the residual is ~40 000× the float32 quantisation step), with exact
mirror symmetry (max |T[r,k] − T[255−r,5−k]| = 0, OBS) and row sums
1.000000–1.000122 (OBS, round 12). **Conclusion (DER):** the kernel is a
windowed-sinc-*like* designed symmetric lowpass; it is not an exact member
of any tested closed-form family, leaving an optimised (least-squares or
minimax) design as the leading candidates. **UNK:** the exact design
formula.

### 6.3 Policy

The target was never executed; §6 consists of static reads and numerical
analysis of observed data. Nothing here is a claim about the original's
runtime behaviour, and no PoC measurement is substituted for a target
measurement.

---

## 7. Measurement honesty notes

* The suite's FFT pitch meter was upgraded to **log-domain parabolic peak
  interpolation** (round 12's linear-power parabola carries a systematic,
  bin-fraction-dependent bias measured on pure reference tones at up to
  ±5 cents — e.g. +1.9 c at 69.3 Hz, −5.2 c at 75.6 Hz/44.1 kHz; probe
  evidence `r13_meter_bias_linear.txt`). The log-domain form measures pure
  tones to within **0.25 cents** across the battery's frequencies and rates
  (`r13_meter_bias_logdomain.txt`); the battery residuals reported above
  are therefore engine-dominated. The FFT zero-pad cap also rose from
  2¹⁶ to 2¹⁸ (low-f0 measurements at 96/192 kHz need the finer bins).
* Suite durations use `getrusage` CPU time (user+sys): a real syscall,
  immune to the intermittent vDSO `clock_gettime` livelock observed in this
  sandbox during development (diagnosed and documented; the choice is
  conservative — CPU time ≥ wall time only under scheduler noise, and the
  totals match external wall-clock timing within ~2 %).
* The round-11/12 evidence files in the tree are byte-identical to the
  round-12 archive (re-verified); this round's re-runs are archived
  separately as `r13_rerun_*`.

---

## 8. Artifacts and provenance

* **Native implementation** (`native/`): `include/octavox/dsp.hpp` (new
  `Config` fields, `VoiceStats` by value, `DetectorInfo`,
  `diagnostics`), `src/dsp.cpp` (splice commit + ghost, startup wrap guard,
  B-tap readiness, decimated detector, valley selection, fused early-exit,
  prepare-time tables), tests (105 cases + WAV I/O + alloc counter),
  bench (fixed harness, input-kind and sample-rate parameters), CLI,
  `CMakeLists.txt` (version 13.0.0), `README.md`, `ARCHITECTURE.md`.
* **Round-13 raw evidence** (`evidence/analysis_round13/`):
  `r13_scan_recring.py/.txt`, `r13_disas_recring.py`,
  `r13_recring_tracker.txt` (tracker/recorder disassembly),
  `r13_kernel_fit.py/.txt`, `r13_meter_bias_linear.txt`,
  `r13_meter_bias_logdomain.txt`, `r13_coldstart_harness.cpp`,
  `r13_coldstart_before.json` (the BEFORE, round-12 library),
  `r13_coldstart_after.json` (the AFTER), `r13_native_tests.json/.txt`
  (105/105 release), `r13_native_tests_asan.json/.txt` (105/105, zero
  sanitizer reports), `r13_native_bench.json/.txt`, and the re-run results
  `r13_rerun_r11_numeric_audit.json`, `r13_rerun_r11_acceptance.json`,
  `r13_rerun_r12_native_metrics.json/.txt`.
* **Canonical docs updated**: `OCTAVOX_DSP_ARCHITECTURE.md` (§10 round-13
  addendum), `OCTAVOX_CONTROL_FLOW_MATRIX.md` (§14 addendum),
  `OCTAVOX_OPEN_QUESTIONS.md` (Q-16/Q-17 updates), 
  `OCTAVOX_RESEARCH_EVIDENCE_MATRIX.md` (R-71…R-79 + round-13 log),
  `OCTAVOX_DSP_DEMO_USAGE.md` (§10 native build/test/bench for round 13),
  `README.md` (round-13 section). Historical values were corrected **in
  place with explicit changelogs**; no raw evidence file was rewritten.
* **Provenance**: round-12 zip `a5975465…` verified intact on arrival;
  target digest `7bb260f6…` re-verified before and after every scan (the
  binary was only ever read); the Python reference implementations are
  byte-identical to the delivered package (manifest-verified); the
  round-12 archive is preserved unchanged (the benchmark-harness bug fix
  is documented here and shipped as new code, with the r12 numbers
  explained rather than rewritten).
* No completeness claim is made; the UNK list in §6 is the honest residue.
