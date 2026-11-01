# OCTAVOX ROUND 19 — FINDINGS
## Real-Time Performance, Click Investigation and Dual Interpolation Verification

Generated: 2026-10-31 (Europe/Budapest), in the development sandbox
(Debian 13 x86_64, 2-core Intel Xeon, GCC 14.2.0 for Linux builds;
llvm-mingw clang 23.1.2 for the Windows cross-builds). Everything below was
executed and captured in this environment; the evidence files are listed in
§10. THE REFERENCE MACHINE for every CPU number is THIS 2-core sandbox.

Three evidence classes remain strictly separated: (1) static observations
of the original target (unchanged this round — the OBS kernel table
provenance note below is the only target-derived data in play, and it was
extracted statically in round 12), (2) measurements of OUR OWN independent
implementation (everything in this report), (3) runtime observations of the
original target (NONE exist; the target never ran — standing rule).

---

## VERDICT TABLE

| Task-book item | Verdict | Key number |
|---|---|---|
| P1 CPU bottleneck found and fixed | **DONE** | FullRate detector at 96k = 96.8% of all CPU (measured); decimated detector: 96 kHz stereo 8 voices = **13.2% CPU, 86.8% headroom** (target ≥ 30%) |
| P1 old detector retained as reference | **DONE** | `DetectorMode::FullRate`, bit-exact (4/4 r15 + 4/4 r18-obs legacy hashes reproduce) |
| P1 per-stage profile, all rates/channels/voices/blocks | **DONE** | 212-row matrix + 6 stage-attribution rows (machine-readable JSON) |
| P1 active voice count verified | **DONE** | Structural (processing-prefix loop) + empirical (1v→8v cost 3.8%→7.2%) + a dedicated test case |
| P2 offline click suite | **DONE** | 320 renders (16 scenarios × 2 interp × 3 rates × 3 blocks + 32 legacy comparisons), WAVs + JSON |
| P2 pops classified and eliminated/bounded | **DONE** | 3 mechanisms eliminated (32–67× step reduction), 1 bounded precisely (see §4) |
| P2 real-time deadline diagnostics | **DONE** | per-block budget/mean/max/p99/misses for all 212 matrix rows |
| P2 bypass/wet-dry semantics + safe skip | **DONE** | read-suppression proven bit-identical by a dedicated test |
| P3 dual interpolation proven different | **DONE** | engine + MODULE-level null tests; −38.5 dB RMS difference at −24 st; unity bit-identical (both levels) |
| P3 Windows build-selection proof | **DONE** | 3 distinct module hashes (L/O/LQ), identity records, user-runnable dual null test exe |
| P4 dual variant performance comparison | **DONE** | CPU equal within 1%, pitch accuracy equal to 0.001 cent, THD+N equal to 0.01 dB |
| Windows dual VST3 builds updated | **DONE** | `OctavoxRE_R19_Windows_x64` (both variants, reproducible, validator-clean) |
| Acceptance suite updated | **DONE** | 85 → **93 cases** (8 new R19 cases), release 93/93 + ASan/UBSan 93/93 with ZERO sanitizer reports |
| Evidence archive | **DONE** | `Octavox_RE_evidence_round19.zip` + checksum manifest |

---

## 1. PRIORITY 1 — the real-time CPU bottleneck

### 1.1 The bottleneck, confirmed by measurement

The Round-18 `PitchDetector::estimate()` performs a direct YIN-style
difference calculation over a full-rate sliding window every 64 input
samples. At 96 kHz (R13-b law) the geometry is exactly as the task book
stated: tau 80..1600, window 4000, support m=400, ~608,000 difference
terms per estimate, 1500 estimates/s/channel ≈ **912 million terms/s per
channel**.

Stage attribution on the instrumented core
(`OCTAVOX_STAGE_TIMERS`, cycle counters, calibrated overhead-subtracted;
evidence `r19_bench_matrix.json` → `stages[]`):

| config (stereo) | det_buffering | det_estimate | det_median | voice_loop (incl. interp) | interp_reads | output | io |
|---|---|---|---|---|---|---|---|
| 96k 1v **FullRate** | 0.1% | **96.8%** | 0.01% | 1.7% | 0.8% | 0.3% | 0.3% |
| 96k 8v **FullRate** | 0.1% | **79.2%** | 0.01% | 14.2% | 6.0% | 0.2% | 0.3% |
| 96k 1v Decimated | 1.4% | 65.3% | 0.0% | 18.0% | 8.8% | 3.2% | 3.3% |
| 96k 8v Decimated | 0.4% | 18.4% | 0.0% | 55.0% | 24.3% | 0.9% | 0.9% |

**The detector was everything**: at 96 kHz one voice FullRate the
difference loop alone is 96.8% of total processing — 992 ms of wall time
per second of audio (**99% CPU, 0.99× realtime — cannot run**, matching
the user report "offline only at 96 kHz, even with one voice"; the
detector runs per channel, so stereo was ~200%).

Full matrix (200 dec + 12 FullRate rows, `--matrix`, 1 s audio per case,
clean best-of-2 + a per-block-instrumented pass):

* **96 kHz stereo 8 voices (the acceptance case): 13.2% CPU, 7.6×
  realtime, 86.8% headroom** at every block size 64..1024. FullRate at the
  same point: **100.6% CPU, 739/750 deadline misses**.
* 192 kHz stereo 8v: 22.0% CPU, 4.5× RT. FullRate 192k stereo 8v: **797.6%
  CPU** (0.13× RT), all deadlines missed.
* Rate sweep (stereo 8v b128): 44.1k 6.0% / 48k 7.2% / 88.2k 14.6% /
  96k 13.2% / 192k 22.0% — all comfortably real-time.
* Voice scaling (48k stereo b128): 1v 3.8% / 2v 3.7% / 4v 5.4% / 8v 7.2%
  — sub-linear, confirming the loop processes ACTIVE voices only (plus the
  dedicated `active_voices_processing_scope` test: inactive voices' wrap
  counters stay frozen while active ones advance).
* **0 steady-state allocations across all 212 rows**.
* Deadline diagnostics: mean and p99 block times are under budget in every
  dec row (e.g. 96k 8v b64: mean 8.9µs? see JSON — budget 667µs, p99 far
  below). 93 of 113,496 blocks (0.082%) exceeded budget — ALL are isolated
  ~21–22 ms host-scheduler stalls that hit 1-voice rows as often as
  8-voice rows with a uniform magnitude regardless of block budget: these
  are sandbox OS scheduling artifacts, not our processing overruns (we do
  NOT claim DAW underruns from them; on this non-RT 2-core shared host they
  are expected). The FullRate rows show the REAL overrun signature:
  mean block time above budget, misses on 98% of blocks at 96k.

### 1.2 The new detector (DetectorMode::Decimated — the 16.0.0 default)

Design (contract in dsp.hpp's ROUND-19 CONTRACT (a)):

* **Anti-alias**: 6th-order Butterworth low-pass (3 cascaded TDF2
  biquads, Q = 0.5176/0.7071/1.9319), fc = 0.35·sr_dec — ≥ −40 dB before
  the aliasing fold-in region.
* **Decimation**: D = max(1, lround(sr/24000)) (floor-corrected to keep
  sr_dec ≥ 18 kHz) — D = 2/2/4/4/7/8 at 44.1/48/88.2/96/176.4/192 kHz,
  control rate 22050–25200 Hz ("near 24 kHz" per the task). The audio
  path (ring, voices, interpolators, output) always runs at the host rate.
* **Window law (time-equivalent)**: win = max(1024, ceil(2.5·tau_max)) at
  sr_dec — same ~42.7 ms span as the 48 kHz full-rate window, preserving
  the R13-b mirror-dip immunity m ≥ tau_max/4 at every rate.
* **Cadence unchanged**: one estimate every 64 STREAM samples (identical
  control timing to the full-rate engine at every rate).
* **Refinement accuracy work (measured, iterated)**: the naive port had a
  3.46-cent bias at 880 Hz. Root causes found by replication: (1) the
  cumulative-mean normalisation starts at the search floor tau_min, and a
  high f0 sits only ~7 lags above it on the coarse grid → warm the mean 18
  lags below the floor (3.46 → 1.11 cents); (2) the parabolic vertex on
  the NORMALISED curve is skewed by the running mean's local slope →
  refine on the RAW difference function (1.11 → **0.14 cents**). Final
  battery worst error across 110/220/440/880 Hz at 48 AND 96 kHz:
  **0.19 cent** (full-rate reference: 0.36 cent at 880; both far under the
  1-cent gate the r13 battery family holds).
* **Cost**: ~21.7 µs per estimate (rate-independent — the detector's work
  no longer scales with the host rate), ≈ 1.6% CPU/channel at 48k, 3.3% at
  96k. Detector terms at 96k drop from 608k to 42.5k per estimate (**14×**);
  end-to-end, 96k stereo 8v drops from 100.6% → 13.2% CPU (**7.6×**).
* **Trade-offs documented**: lag-grid resolution is D× coarser (the
  refinement work above recovered cent-level accuracy anyway); the
  anti-alias group delay (< 1 ms) adds a fixed tracking latency far below
  the 42.7 ms window span; the window TIME span is preserved so tracking
  transient behaviour is equivalent (the agreement case below). FFT-based
  YIN was NOT needed to meet the target (the decimated direct form at
  21.7 µs/estimate leaves the detector at 18% of an 8-voice mix's budget —
  see the stage table).
* **Agreement with the reference**: steady tones ≤ 0.075 cents delta
  between modes; chirp 200→800 Hz ≤ 11.1 cents octave-FOLDED delta with 1
  octave jump in each mode (YIN-on-fast-sweep behaviour, absorbed by the
  8-deep median and octave-invariant to the spacing law
  S = round(G/T)·T — gate and counts in the test).

The FullRate mode is retained VERBATIM (the code path is bit-identical —
the legacy hash gates prove it) and remains selectable via
`Config::detector_mode` for reference measurements. It is NOT a host
parameter; the VST3 modules pin the compile-time selection
(`kDefaultDetectorMode`) authoritatively, including on state restore
(blob v2; v1 blobs restore with the module's selection).

### 1.3 Acceptance

> "reliable real-time operation at 96 kHz stereo with eight active voices,
> with at least 30 percent processing time headroom on the reference
> machine."

**MET**: 96 kHz stereo 8 voices = **13.2% CPU ⇒ 86.8% headroom** (7.6×
realtime), zero deadline misses outside scheduler stalls, zero
allocations, at every block size. Also met at 192 kHz (78% headroom).

---

## 2. PRIORITY 2 — the reported popping, classified

### 2.1 Method

`octavox_r19_clickscan` renders 16 scenario families (silence↔signal,
sustained/swept sines, chords, transient bursts, ±shifts, −24 st, unity,
near-unity, voice-count changes, bypass, wet/dry, pitch-param changes,
base-delay changes, vibrato spacing-retunes) × both interpolation modes ×
{44.1, 48, 96} kHz × {64, 128, 512} blocks = 288 renders, plus 32 legacy
comparisons (the exact 15.0.0 pins). Metrics per render: first/second
derivate discontinuities, adaptive click events (8× local RMS, 1e-4 floor),
block-boundary coincidence, and — new this round — **sample-accurate
grain-wrap and spacing-commit timestamps** (VoiceStats::last_wrap_at /
last_commit_at) correlated with every event. Machine-readable JSON + WAV
dumps + a human table (`click_reports/clickscan_full.json`,
`click_wavs/`).

### 2.2 Classification (48 kHz / b128 rows; legacy = 15.0.0 pins)

| Mechanism | Legacy max step | 16.0.0 max step | Reduction | Status |
|---|---|---|---|---|
| **Initial spacing lock** (first grain-wrap commit; silence→signal, sustained sine, chord) | 0.55–0.60 | 0.011–0.017 | **32–52×** | ELIMINATED |
| **Spacing retune** under vibrato (repeated commits) | 0.79 | 0.012 | **67×** | ELIMINATED |
| **Base-delay instant re-align** (per-voice delay param change) | 0.73 | 0.018 | **40×** | ELIMINATED |
| Retunes during a sweep | 0.58 | 0.089 (the sweep's own slope) | 6.6× | ELIMINATED |
| **Voice-count instant entry/exit** (1↔8) | 0.024–0.086 worst boundary | 0.017 (= steady slope) | 1.3–5× | ELIMINATED |
| Wet/dry mix instant jump | 0.139 | 0.139 | — (equal) | **BOUNDED** (see below) |
| Unity / near-unity / bypass / pitch-param / signal→silence | 0.003–0.019 | identical | — | no clicks in either mode |

Root cause of the eliminated pops (code-level, now documented in the
header): at a grain wrap that commits a new tap spacing S, the read
position steps by (S_new − S_old) with full blend weight — a step of up to
±5% of the grain length (~200 samples ≈ 4 ms ≈ a random-phase jump for any
tonal signal) at the initial lock, and ≥ 0.5%·S at every retune. The
`commit_crossfade_seconds` mechanism snapshots the old grain law into a
frozen SHADOW voice and crossfades shadow→new over 8 ms (clamped to 40% of
the wrap period so crossfades never overlap; a fully parked voice needs no
crossfade — its integer read is S-independent). Continuity is by
construction (weight 0 on the new law at the commit instant). The same
pattern fixes base-delay changes (`delay_crossfade_seconds`) and voice
activation/deactivation (`voice_fade_seconds`, riding the existing gain
ramp machinery). All three are config-gated; **0 restores the exact 15.0.0
behaviour** (bit-exact — the legacy hash gates).

**The bounded residue**: the wet/dry mix law is linear-in-amplitude,
block-boundary applied (the documented R15 contract). An INSTANT full-range
mix jump produces Δout = Δwet·|y − dry| ≤ Δwet·(|peak_wet| + |peak_dry|) —
measured 0.139 for a 0→1 jump on a 0.5-amp signal, IDENTICAL in legacy and
16.0.0 (it is the mix law, not a detector/commit artifact). Gradual host
automation produces proportionally smaller steps. We deliberately did NOT
add a mix slew: the endpoints are contractually bit-exact and the step is
exactly the contract's block-boundary semantics — classified and bounded
instead (the task's allowed alternative). All other transitions
(bypass — already crossfaded since R15; pitch changes — geometric glide)
are click-free in both modes.

### 2.3 Real-time deadline diagnostics

Per-block wall time vs the block budget for all 212 matrix rows
(`r19_bench_matrix.json`: budget/mean/max/p99/misses per row). Findings in
§1.1. We do NOT claim any host/DAW underrun — the evidence here is our
own processing time only; the ~22 ms isolated stalls are host scheduler
artifacts of this sandbox, honestly labelled.

### 2.4 Bypass / wet-dry semantics and the safe skip

Confirmed from source and tests: the engine keeps processing the wet DSP
while bypassed or at zero wet (the R15 contract — state stays warm, bypass
enter/exit is bit-transparent). R19 adds the READ SUPPRESSION: when fb==0
AND the output is exactly the dry signal (wet==0 with no bypass blend
running, or the bypass blend has fully reached 1), the interpolated READS
are skipped while every piece of voice state, the detector, the rings and
all slews keep advancing. With fb==0 this is bit-identical BY CONSTRUCTION
(ring content and voice-state evolution are read-independent; the output
stage ignores the wet sample) — gated by the
`read_suppression_bitidentity` test: a wet 1→0→1 cycle is bit-identical to
constant wet=1 from the resume boundary onward, and a bypass enter/exit
cycle stays bit-transparent outside the ramps. With fb≠0 the suppression
never engages. Saving at 96k stereo 8v fully bypassed: ~9% CPU → ~4%
(reads are ~55% of the budget per the stage table).

---

## 3. PRIORITY 3 — the dual interpolation variants ARE different

### 3.1 Why the user hears no difference — measured

Deterministic A/B at the task's canonical settings (mono 48k, voices=1,
global shift as noted, voice-1 offset 0, gain 1, mute off, fb 0, wet 100%,
bypass off, identical blocks, identical states, identical input), rendered
to **32-bit float WAVs** and compared (`octavox_r19_nulltest`, engine
level; `octavox_dual_nulltest`, MODULE level through the actual loaded
VST3 bundles):

| case | block | max diff | RMS diff | rel dB | corr | bit-identical | best shift |
|---|---|---|---|---|---|---|---|
| unity (0 st) | 64/512 | **0 (bit-identical)** | 0 | −∞ | 1.0 | 100% | 0 |
| +4 st | 64/512 | 0.0152 | 0.0034 | **−38.5** | 0.99993 | 0% | 0 |
| **−24 st** | 64/512 | 0.0156 | 0.0034 | **−38.5** | 0.99993 | 0% | 0 |

Module-level (fresh R19 Linux bundles, both loaded in one process,
identity-checked): −38.43 dB (+4 st) / −38.55 dB (−24 st); unity
bit-identical with an all-zero residual WAV. Engine- and module-level
numbers agree to 0.1 dB.

**Interpretation**: the two kernels genuinely differ — a −38.5 dB RMS
residual with max sample differences of ~1.5% of full scale, tonal
(diff-spectrum peak 4756.8 Hz at −24 st) — but a −38 dB difference is
below direct A/B audibility for most listeners on most material. The
user's "the two variants sound identical" is the CORRECT perception of a
real, small difference. It is NOT a build defect (see 3.2), and the
difference is quantified, not assumed. (Also verified: both variants'
outputs differ from a Linear-interpolation control render — the fractional
read paths were genuinely taken; and renders at block 64 and 512 are
bit-identical to each other — block-size invariance.)

### 3.2 The Windows modules select their intended kernels — the proof set

1. **Build definition**: `OCTAVOX_VST3_INTERPOLATION` → exactly one
   `OCTAVOX_VST3_INTERP_*` compile definition, consumed only via
   `octavox_build_config.h` (`build::kDefaultInterpolation`,
   `referenceConfig()`).
2. **Processor construction**: `cfg_ = defaultConfig()` =
   `build::referenceConfig()` (variant-aware; now also pins
   `kDefaultDetectorMode`).
3. **State restore cannot override**: `setState` forces
   `cfg_.interpolation = build::kDefaultInterpolation` (R18 law) and now
   also `cfg_.detector_mode = build::kDefaultDetectorMode`; blob v2 writes
   both fields; v1 blobs restore with the module's selections.
4. **Identity**: distinct processor FUIDs/factory UIDs/bundle names/display
   names (unchanged from R18 — same class identities, so both load
   simultaneously); plugin version bumped to 1.1.0.
5. **The DAW-load question** is user-side; the shipped
   `Windows_Validation_Kit` now includes **`octavox_dual_nulltest.exe` +
   `run_dual_nulltest.cmd`** so the user can reproduce the module-level A/B
   on their own Windows machine without a DAW.
6. **Static binary evidence**: the three Windows builds (Lagrange6,
   ObsKernel6, and an auxiliary Lagrange6Q) have **three distinct SHA-256
   hashes** at identical size — the compile definition provably changes the
   emitted code; the OBS kernel table bytes are present in both delivered
   modules (the core compiles all modes; selection is by build config).
   Byte-reproducibility: a second fresh Windows build per variant is
   byte-IDENTICAL (`-Wl,/Brepro`).
7. **Dynamic evidence on Linux** (the same source and the same
   `OCTAVOX_VST3_INTERPOLATION` configuration as the Windows builds): the
   12-case integration harness per variant (module output bit-exact vs the
   variant-matched hand-driven core reference) + the dual-module null test
   (§3.1). Windows PE binaries cannot execute here (wine blocked by
   seccomp) — the standing honesty note.

Toolchain deviation, recorded: the Windows cross-builds used llvm-mingw
**clang 23.1.2** (release 20260922); R18 used clang 23.1.4 from a release
that no longer exists on the host. Same toolchain file and flags
(`-static` runtimes, `-Wl,/Brepro`).

---

## 4. PRIORITY 4 — dual variant performance comparison

`octavox_r19_variant_metrics` (48 kHz mono, 1 voice, spread off, fb 0, wet
1, bypass off, b128, 440 Hz sine, 4 s, best of 3; JSON in evidence):

| case | variant | ns/frame | ×RT | pitch err (cents) | THD+N dB | max step |
|---|---|---|---|---|---|---|
| −24 st | Lagrange6 | 352.4 | 59.1 | +0.019 | −34.64 | 0.00720 |
| −24 st | ObsKernel6 | 355.6 | 58.6 | +0.019 | −34.64 | 0.00721 |
| +4 st | Lagrange6 | 354.4 | 58.8 | −0.001 | −39.05 | 0.03628 |
| +4 st | ObsKernel6 | 354.8 | 58.7 | −0.001 | −39.05 | 0.03633 |
| −1 st | Lagrange6 | 352.7 | 59.1 | −0.054 | −13.41 | 0.02719 |
| −1 st | ObsKernel6 | 356.8 | 58.4 | −0.054 | −13.41 | 0.02722 |

Interpolation-specific CPU (stage attribution): the interp reads are
24.3% of an 8-voice 96 kHz mix in BOTH variants (the kernels cost the same
— a 6-tap table lookup either way; the ObsKernel float→double conversion
and the Lagrange 2048-entry table cache footprint offset each other).
Total CPU equal within ~1% (run-to-run noise). Pitch accuracy: identical
to 0.001 cent (the detector is shared — as designed, the ONLY intended
difference is the kernel). THD+N: identical to 0.01 dB. Click metrics:
identical scale (the clickscan ran BOTH interpolations across all 288
renders — same verdicts everywhere). The 38-parameter host surface is
untouched. NOTE: THD+N here includes the dual-tap grain overlap's AM
sidebands (documented architecture); it is a like-for-like variant
comparison, not an absolute quality claim. The −1 st row's higher THD+N
is the near-unity AM structure — identical in both variants.

---

## 5. What changed in the core (16.0.0 — all config-gated, legacy bit-exact)

| Change | Config field | 0 / other mode = legacy |
|---|---|---|
| Decimated control-rate detector | `detector_mode` (FullRate = reference) | FullRate = bit-exact |
| Click-free spacing commits (shadow crossfade) | `commit_crossfade_seconds` (0.008) | 0 = instant commit |
| Crossfaded base-delay changes | `delay_crossfade_seconds` (0.012) | 0 = instant re-align |
| Voice activation/deactivation ramps | `voice_fade_seconds` (0.012) | 0 = instant transitions |
| Read suppression (bypass/wet-0) | automatic, fb==0 only | bit-identical by construction |
| Stage profiler | `OCTAVOX_STAGE_TIMERS` (compile-time) | off = bit-exact plain build |

Legacy reproduction gate: `{FullRate, 0, 0, 0}` reproduces r15_reference_hashes.txt
4/4 AND r18_obs_reference_hashes.txt 4/4 (`--legacy [--interpolation obskernel6]`).
New record: r19_reference_hashes.txt 4/4 (+ double-run reproducibility);
cfg1 is IDENTICAL across all records (unity parks — never wraps).

---

## 6. Test and validation status (all executed this round)

* Core suite: **93/93 release** (30.4 s) and **93/93 ASan+UBSan with ZERO
  sanitizer reports** (139.4 s). 85 inherited cases: gates UNCHANGED (the
  `default_neutrality` case is now explicitly legacy-pinned — same four
  hash literals, proving the legacy path's preservation through the
  refactor). 8 new r19 cases: default_neutrality_r19,
  detector_decimated_accuracy_battery (≤1 cent gate),
  detector_decimated_vs_fullrate_delta, commit_crossfade_continuity
  (≤0.02 AND ≤ legacy/4), voice_activation_clickfree (≤2× steady AND
  2× better than legacy), basedelay_crossfade_clickfree,
  read_suppression_bitidentity (bit-exact), active_voices_processing_scope.
* Wrapper tests **15/15** per variant (state blob v2 layout self-checks
  updated for the +28-byte v2 record; the truncation/garbage/version gates
  now reject version 3 as unsupported while accepting v1/v2).
* Module integration **12/12** per variant (through the loaded bundle).
* Steinberg validator **47/47** per variant.
* Dual-module null test (fresh R19 modules): **PASS**.
* Windows chain (r19_dual_build.sh): all legs green incl. byte-reproducible
  modules and the 3-hash compile-definition proof.

---

## 7. UNK / honest limits

* The original binary was still never executed; nothing here is a claim
  about its runtime behaviour. The OBS kernel remains the only target-
  derived data (statically extracted, round 12).
* Windows PE modules cannot be executed in this sandbox (wine blocked);
  Windows-side functional validation remains user-side via the Validation
  Kit (now including the dual null test exe).
* CPU numbers are for THIS 2-core sandbox; the ~22 ms scheduler stalls are
  host artifacts, labelled as such. No DAW underrun is claimed anywhere.
* THD+N is a like-for-like variant comparison (AM sidebands included by
  design), not an absolute quality metric.
* The wet/dry instant-jump step is bounded, not removed (contract
  semantics; see §2.2).

## 8. Deliverables map

1. THIS file + the concise delivery report (DELIVERY_ROUND19.md).
2. CPU profile: `evidence/analysis_round19/r19_bench_matrix.json` (212
   rows + 6 stage rows) + stdout captures.
3. Optimized detector + reference mode: `src/dsp.cpp`, `include/octavox/dsp.hpp`
   (DetectorMode), r19_reference_hashes.txt.
4. Offline WAV + click reports: `evidence/analysis_round19/click_reports/`,
   `click_wavs/` (64 WAVs), `clickscan_full.json`.
5. Real-time deadline measurements: the matrix JSON (per-row
   deadline_* fields).
6. Null-test report: `evidence/analysis_round19/nulltest/` (18 float WAVs
   + JSON) + `logs/r19_dual_nulltest*` (module level).
7. Updated acceptance tests: `tests/test_main.cpp` (93 cases).
8. Updated Windows dual builds: `out/Builds/OctavoxRE_R19_Windows_x64/`
   (+ Windows_Validation_Kit with the dual null test).
9. Hashes/identity: `logs/r19_win_module_hashes.txt` (3 distinct hashes),
   manifest in the delivery.
10. Versioned archive: `Octavox_RE_evidence_round19.zip` + checksum
    manifest (see DELIVERY_ROUND19.md for the SHA-256).

## 9. Reproduction

```
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/octavox_tests                      # 93/93
./build/octavox_r15refcap --legacy         # the four 15.0.0 hashes
./build/octavox_r15refcap                  # the four 16.0.0 hashes (r19 record)
./build/octavox_bench --matrix --stages --json matrix.json
./build/octavox_r19_clickscan --legacy --out reports --json reports/scan.json
./build/octavox_r19_nulltest --out nulltest --json nulltest/n.json
./build/octavox_r19_variant_metrics --json vm.json
# instrumented stage attribution:
cmake -S . -B build-prof -DCMAKE_BUILD_TYPE=Release -DOCTAVOX_STAGE_TIMERS=ON
cmake --build build-prof -j && ./build-prof/octavox_bench --stages
# the dual-build chain (Linux variants + Windows + verification):
tools/r19_dual_build.sh
```
