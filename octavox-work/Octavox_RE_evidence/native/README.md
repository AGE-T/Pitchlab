# OCTAVOX Native DSP Core (Round 12)

Our own C++17 port of the round-11 `delayline_aligned` design plus the
round-12 unity engagement fix. **Not a VST3 plugin and not a reproduction of
the original plugin's runtime behaviour** — see `ARCHITECTURE.md` for the
full note (state, memory ownership, threading, channel isolation,
limitations) and `../docs/OCTAVOX_ROUND12_FINDINGS.md` for the research
context.

## Layout

```
include/octavox/dsp.hpp   public API (AlignedDelayLine, MultichannelShifter)
src/dsp.cpp               implementation
src/obs_kernel_table.h    OBSERVED target .rdata kernel (opt-in mode; data, not code)
tests/test_main.cpp       correctness + unity regression suite (51 cases)
tests/wav_io.*            WAV I/O (harness only; mirrors the Python CLI)
tests/alloc_counter.cpp   global-new counter for the no-alloc gate
bench/bench_main.cpp      release benchmark (deterministic LCG input)
cli/octavox_cli.cpp       WAV->WAV demo CLI (cross-language metrics driver)
```

## Build

Requires CMake ≥ 3.16 and a C++17 compiler (developed with GCC 14.2).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Sanitizer build (address + undefined):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan -j
```

## Test / benchmark

```sh
./build/octavox_tests --json results.json      # exit 0 = all pass
./build-asan/octavox_tests --json results_asan.json
./build/octavox_bench --json bench.json
./build/octavox_cli --in ../poc/example_input.wav --out out.wav --cents 400 --voices 3
```

The authoritative cross-language measurements (native vs the preserved Python
reference) are produced by
`../evidence/analysis_round12/r12_native_metrics.py`.

## Recorded results (this round's environment)

* `octavox_tests` (release): **51/51 pass** — unity bit-exactness (tones,
  noise, chord, silence, low level, arrival from a shift), the cent battery
  (0, ±1, ±10, ±50, ±100, ±500, ±1200), 1/8 voices, mono/stereo, channel
  isolation, block-size invariance, repeated prepare, reset determinism,
  long runs, NaN/clip gates, steady-state allocation-freedom.
* ASan+UBSan build: **51/51 pass, zero sanitizer reports**.
* Benchmark (mono 48 kHz, +4 st, LCG noise, best of 3): ≈ 15× realtime
  (1 voice) / ≈ 13.8× (8 voices) at the default 64-sample control interval;
  ≈ 51× realtime with the interval at 512 (the YIN detector is ~73 % of the
  cost). Zero steady-state allocations. Stereo ≈ 6.7× realtime.
* Cross-language (identical input): the Python reference runs at 0.42×
  (1 voice) / 0.25× (8 voices) realtime — the native core is ≈ 35–55× faster
  on the same machine.

## Version history (recovery record)

- **14.0.0** — recovered round-13/14 scope rebuilt on the round-12 baseline
  after an environment reset (see worklog R15-R0/E1/E1a/E1b/EV1): cold-start
  alignment law, the R13-b detector window law (`win = max(2048,
  ceil(2.5·τmax))` — the round-12 core was blind at 96 kHz), zero-allocation
  re-prepare, RT contract, tracker/rate-limiter parity evidence, four-mode
  interpolation comparison (Lagrange6 stays default), 14.0.0 baseline bench.
  The original rounds-13/14 artifacts were lost in the reset.
- **15.0.0** — round-15 P1 per-voice harmonizer parameter surface: per-voice
  pitch ratio / gain / mute / base delay, wet/dry mix with the dry path
  aligned to the intrinsic latency, click-free bypass; block-boundary
  parameter application. Re-implemented after a SECOND environment reset
  (worklog R15-R1/REB-1) and verified bit-neutral vs 12.0.0 by the four
  reference hashes in `r15_reference_hashes.txt` (tool `octavox_r15refcap`:
  re-run after any core change and check the hashes). Suite: 85 cases
  (51 frozen round-12 + 19 R13 + 7 P1 + 7 r15acc acceptance); see
  `../evidence/analysis_round15/r15_acceptance_results.json` and
  `r15_bench_comparison.md` for the P5 regression verdict.
