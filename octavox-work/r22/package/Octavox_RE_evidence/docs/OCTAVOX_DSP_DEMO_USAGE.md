# OCTAVOX_DSP_DEMO_USAGE

**How to run the independent demo (WAV → WAV) and the design comparison**

Everything described here is **our own implementation**. It contains no target code, and no number
produced by it is a measurement of the target. The target was never executed.

> **Round-11 corrections and additions:** (1) the stereo claim in §4.3 below was **wrong for the
> delay-line design** — the reference CLI processed both channels through one stateful engine, so
> the right channel received the left channel's delayed ring content (measured contamination
> −0.44 dB relative peak; the vocoders were and are stateless/clean). The **v2 CLI**
> (`poc/octavox_v2.py`) builds a fresh engine per channel and fixes this. (2) A fourth design,
> **`delayline_aligned`** (period-aligned grain splicing), was added in `octavox_v2.py` — it
> removes the delay line's measured few-cents pitch error and crossfade modulation
> (before/after: `evidence/analysis_round11/r11_dsp_improvement.md`). (3) An automated gated
> acceptance suite now exists: `python3 evidence/analysis_round11/r11_acceptance_tests.py`
> (45 cases, exit 0 = pass, machine-readable JSON next to it). (4) Known edge behaviours,
> measured: the phase vocoders drop inputs shorter than `n_fft/2` (21 ms at 48 kHz) to digital
> silence (latency truncation); the delay line at unison is a two-tap comb (−2.4 dB at 220 Hz);
> percussive broadband bursts overshoot unity in the float domain (≤ 1.14) and are clip-guarded
> at the 16-bit write path.

---

## 1. What is in the demo

| design | module | family | status |
|---|---|---|---|
| `delayline` | `poc/octavox_poc.py` (`OctavoxPoC`) | variable-rate two-tap delay line with crossfaded re-arm — the **reconstructed target architecture** | reference design / regression baseline |
| `pv` | `poc/octavox_offline.py` (`PhaseVocoderShifter`) | STFT phase vocoder: time-stretch by `r`, then resample by `1/r` | independent second approach |
| `pv_transient` | `poc/octavox_offline.py` | same vocoder + transient-aware short analysis window and phase re-lock at detected onsets | independent third approach (hybrid) |
| `delayline_aligned` | `poc/octavox_v2.py` (`AlignedDelayLineShifter`) | the delay-line design with **period-aligned grain splicing** (tap spacing `k·T̂` committed at wraps) | round-11 improvement, kept separate from the reference |

The delay-line design is the architecture recovered from the binary (see
`OCTAVOX_ROUND6_DSP_FINDINGS.md` §4). The two vocoder designs were written from scratch for this
round; they share no code with the delay line beyond the metric functions.

---

## 2. Requirements

* Windows / Linux / macOS, CPython **3.11** (tested: 3.11.9, MSC v.1938, 64-bit)
* **NumPy** (tested: 2.4.4). No SciPy, no other dependency.
* Both files must stay in the same folder (the CLI imports `octavox_poc`).

```powershell
python -c "import numpy; print(numpy.__version__)"
```

---

## 3. Quick start

```powershell
cd f:\VST\OV_DIS\Evdence\poc

# 1. make the deterministic 2 s example input (48 kHz mono, 5 plucked notes)
python octavox_offline.py --make-example example_input.wav

# 2. shift it up a major third with the reconstructed delay line, 3 voices
python octavox_offline.py --in example_input.wav --out out_dl.wav `
    --algo delayline --semitones 4 --voices 3 --interp lagrange6

# 3. same shift with the phase vocoder
python octavox_offline.py --in example_input.wav --out out_pv.wav `
    --algo pv --semitones 4 --voices 3

# 4. same shift with the transient-aware hybrid
python octavox_offline.py --in example_input.wav --out out_pvt.wav `
    --algo pv_transient --semitones 4 --voices 3

# 5. the three-design comparison on identical signals
python octavox_offline.py --compare
```

`--compare` writes `offline_measurements.json` and `OFFLINE_COMPARISON.md` and prints the Markdown
table. It takes about a minute on the recorded machine (most of it is the Python delay line).

---

## 4. CLI reference

| option | meaning | default |
|---|---|---|
| `--in PATH` | input WAV (PCM 16/24/32-bit; mono or stereo) | — |
| `--out PATH` | output WAV (always 16-bit PCM) | — |
| `--algo {delayline,pv,pv_transient}` | pitch-shift design | `delayline` |
| `--ratio R` | pitch ratio (`1.0` = no shift, `2.0` = +1 oct) | `1.0` |
| `--semitones S` | pitch shift in semitones; **overrides** `--ratio` | — |
| `--voices N` | number of voices (0…8) | `1` |
| `--interp {lagrange6,lagrange6_q,linear}` | delay-line interpolator | `lagrange6` |
| `--grain G` | delay-line grain length in samples | `4096` |
| `--n-fft N` | vocoder FFT size (power of two) | `2048` |
| `--sr HZ` | **internal processing rate** | `48000` |
| `--keep-rate` | write the output at the **input file's** rate | off |
| `--no-spread` | put every voice at the same ratio (no musical spread) | off |
| `--json PATH` | also write the run info (levels, cost, sample counts) | — |
| `--make-example PATH` | write the deterministic example input and exit | — |
| `--compare` | run the three-design measurement suite and exit | — |

### 4.1 Sample-rate handling — exact semantics

* The file is read at its native rate `sr_in`.
* If `sr_in != --sr`, the input is **linear-resampled** to `--sr` and all processing happens at
  `--sr`.
* By default the output is written **at `--sr`**. The run info records `sr_in` and `sr_out`.
* With `--keep-rate`, the result is resampled back to `sr_in` before writing, so the file keeps its
  original rate (use this when the host expects the original rate).
* No sample-rate conversion is hidden elsewhere: the algorithms themselves are rate-parametric.

Example (44.1 kHz output, down a fourth):

```powershell
python octavox_offline.py --in example_input.wav --out example_output_pvt_44k.wav `
    --algo pv_transient --semitones -5 --sr 44100
# -> sr_in 48000, sr_out 44100, 88200 samples, 2.00 s
```

### 4.2 Voice spread

With `--voices N > 1` and spread on (default), voice 0 keeps the requested ratio exactly and the
remaining voices are placed at `ratio · 2^(d/12)` with
`d ∈ {0, +7, +4, +12, −5, −12, +3, −3}` semitones. `--no-spread` puts every voice at the same ratio
(useful for A/B tests and for the comparison suite, which uses 1 voice).

### 4.3 Supported WAV input

| format | supported |
|---|---|
| PCM 16-bit, 24-bit, 32-bit | yes |
| IEEE float 32-bit (`WAVE_FORMAT_IEEE_FLOAT`) | **no** — convert to PCM first |
| mono / stereo | mono: yes. stereo: **vocoders** yes (stateless, independent); **delay line via the reference CLI: NO — defect found in round 11** (shared stateful engine bleeds L into R at −0.44 dB). Use `octavox_v2.py` (all four algorithms, fresh engine per channel) for stereo. |
| compressed (ADPCM, µ-law, …) | no |

Output is always 16-bit PCM, deterministic (no dither), clip-guarded at ±1.0.

---

## 5. How the three designs work (one paragraph each)

**`delayline` — reconstructed architecture.** Each voice reads a ring buffer at a position that
advances at `1 − r` samples per output sample (`r` = pitch ratio), so a grain of length `G` is
consumed at rate `r`. When the read position would leave its window, the voice re-arms: it starts a
second tap `G` samples away and crossfades over the grain. The two taps are mixed with a
complementary linear crossfade; the interval is applied as a cents value in the ratio exponent.
Interpolation is a 6-point Lagrange kernel (or its 256-phase quantised form). The nominal delay is
`base_delay + G/2` = 2304 samples (**48 ms at unison**); the measured main-response delay is not
constant, because the read position sweeps: 65 ms at ratio 0.80 and 102 ms at 1.25. This design
reproduces the target's *structure* (see `OCTAVOX_ROUND6_DSP_FINDINGS.md` §4); it is not a bit-exact
model.

**`pv` — phase vocoder.** Frames of `N` samples with a periodic Hann window and 75 % overlap are
transformed. For each bin the true instantaneous frequency is estimated from the phase difference
between consecutive frames (`ω = expected + princarg(Δφ − expected)`), and the synthesis phase is
advanced by `ω · Hs/H`, where `Hs = round(H·r)`. The stretched signal is then resampled by `r`
(`out[j] = stretch[j·r]`), which shifts the pitch by exactly `r` and restores the original duration.
A causal `N/2` delay is added so the reported latency is honest (34 ms at `N = 2048`, 48 kHz).

**`pv_transient` — transient-aware hybrid.** Same vocoder, but a level-normalised spectral-flux
detector marks local onset maxima (`flux > median + 8·MAD`, `flux > 2 %`). On those frames the
analysis/synthesis window is shortened to `N/4` (zero-padded, so the FFT size and the bin spacing are
unchanged) and the synthesis phase is re-locked to the analysis phase. This trades a little spectral
resolution exactly where it does not matter (the onset) for a much sharper attack and far less
pre-echo. The overlap-add normaliser is accumulated per sample (`Σ w²`), so the changing window length
does not alter the level.

---

## 6. Reproducibility

* All test signals are generated in code with fixed parameters; the only noise source is
  `np.random.default_rng(7)` (cost test) and `default_rng(3)` (example material), both seeded.
* No wall-clock or random value influences the audio path.
* The comparison writes both the raw numbers (`offline_measurements.json`) and the formatted table
  (`OFFLINE_COMPARISON.md`).
* Two runs on the same machine produce byte-identical WAVs; the cost numbers vary with machine load,
  so the cost columns are indicative, not exact.

To re-create every artefact in this folder:

```powershell
python octavox_offline.py --make-example example_input.wav
python octavox_offline.py --in example_input.wav --out example_output_delayline.wav --algo delayline --semitones 4 --voices 3 --json example_delayline.json
python octavox_offline.py --in example_input.wav --out example_output_pv.wav        --algo pv        --semitones 4 --voices 3 --json example_pv.json
python octavox_offline.py --in example_input.wav --out example_output_pvt.wav       --algo pv_transient --semitones 4 --voices 3 --json example_pvt.json
python octavox_offline.py --in example_input.wav --out example_output_pvt_44k.wav   --algo pv_transient --semitones -5 --sr 44100 --json example_pvt_44k.json
python octavox_offline.py --compare
```

---

## 7. What the demo does **not** do

* It is **not** real time. The Python delay line runs at ≈ 0.25× realtime for 3 voices and ≈ 0.15×
  for 8 voices on the recorded machine; the vocoders run ≈ 13–42× realtime but are still offline
  batch code, not a plugin.
* It is **not** a bit-exact model of the target: no target code or data was reused, and the
  coefficients, window lengths and interpolation weights are our own choices where the binary did not
  determine them.
* It makes **no claim** about how the target sounds. In particular, the measured two-tap comb at
  unity ratio (`POC_RESULTS.md`) is a property of *our* reconstruction of the architecture; whether
  the target exhibits it is Q-14 and remains open.
* It performs no protection analysis and does not touch the target file.

---

## 8. File inventory (rounds 6–11)

| file | role |
|---|---|
| `octavox_poc.py` | delay-line DSP + synthetic regression harness (round 5, unchanged) |
| `octavox_offline.py` | WAV I/O, CLI, phase vocoder, transient-aware hybrid, metric runner |
| `octavox_v2.py` | **round 11**: `delayline_aligned` design + stereo-safe per-channel engines + v2 CLI |
| `example_input.wav` | deterministic 2 s example (5 plucked notes, 48 kHz mono) |
| `example_output_delayline.wav` | +4 st, 3 voices, delay line (reference) |
| `example_output_pv.wav` | +4 st, 3 voices, phase vocoder |
| `example_output_pvt.wav` | +4 st, 3 voices, transient-aware hybrid |
| `example_output_pvt_44k.wav` | −5 st, 1 voice, processed at 44.1 kHz |
| `example_output_aligned.wav` / `example_aligned.json` | **round 11**: +4 st, 3 voices, `delayline_aligned` via the v2 CLI |
| `example_*.json` | per-run info: sample counts, rates, cost, peak |
| `offline_measurements.json` / `OFFLINE_COMPARISON.md` | comparison suite outputs |
| `poc_measurements.json` / `POC_RESULTS.md` | round-5 synthetic-harness outputs |
| `../evidence/analysis_round11/` | the round-11 gated acceptance suite + results + improvement note |


---

## 9. Round 12: the native C++17 core (build, test, benchmark)

Round 12 ports the round-11 `delayline_aligned` design to a native library
(`native/`) and fixes the unity artefact (single-tap engagement + integer
parking — bit-exact delayed copy at exact unity; see
`../native/ARCHITECTURE.md` and `OCTAVOX_ROUND12_FINDINGS.md` §3). The Python
reference implementations (`octavox_poc.py`, `octavox_offline.py`,
`octavox_v2.py`) are **preserved untouched** and remain the baselines; every
previously documented reproduction command in this file still applies.

Build and run (CMake >= 3.16, C++17; developed with GCC 14.2 on Debian):

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build -j
./native/build/octavox_tests --json r12_native_tests.json    # 51 cases, exit 0 = pass
cmake -S native -B native/build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build native/build-asan -j
./native/build-asan/octavox_tests                            # ASan+UBSan, 51/51
./native/build/octavox_bench --json r12_native_bench.json    # release benchmark
./native/build/octavox_cli --in poc/example_input.wav --out out.wav --cents 400 --voices 3
python evidence/analysis_round12/r12_native_metrics.py \
    --poc poc --native-cli native/build/octavox_cli --bench-json r12_native_bench.json
```

Differences from the Python v2 engine (documented, measured):

* **Block-size invariance** (improvement): the control-rate detector fires
  every 64 *stream* samples, so any block split produces identical output
  (gated to exactly 0 difference). The Python restarts its control loop per
  `process()` call.
* **Control timing** (delta): a streaming API cannot reproduce the Python's
  intra-block look-ahead, so with aligned calls the native control timing
  lags by at most one interval (64 samples). Class-level behaviour is
  unaffected (worst pitch error 0.50 cents across the cent battery vs the
  v2's 0.08 cents at the same settings — both far inside the 2-cent gate).
* **Unity** (the round-12 fix): exact unity is a bit-exact delayed copy
  (0.0000 dB comb spread over a tone battery vs the v2's 16.43 dB); ±1 cent
  is fully processed (+1.41/−1.54 c measured). No bypass region.
* **Interpolators**: our Lagrange 2048/256-phase tables (v2 parity), linear,
  and the opt-in `obs6` mode using the OBSERVED target `.rdata` kernel
  (labelled data, not code — `../native/src/obs_kernel_table.h`).

Measured cost (identical LCG noise input, same machine): native
1346–1552 ns/frame (1–8 voices) at the 64-sample control rate
(15.5×/13.9× realtime), 410 ns/frame at control interval 512 (50.9× — the
YIN detector is ~73 % of the runtime); the Python v2 reference measures
50 084–84 240 ns/frame (0.42×/0.25× realtime). The native core is
≈ 35–55× faster. Zero steady-state allocations; peak RSS ≈ 8.6 MB mono /
12.7 MB stereo.

Known native limits (measured, gated and reported): at 96 kHz the detector
window cannot bracket 60 Hz (reference-parity fallback; pitch error −13.8 c
at +400 c/96 kHz); a fresh near-unity engine keeps the static two-tap comb
for periodic input until its first wrap (removed at any departure from exact
unity by the exit-spacing commit).
