# OCTAVOX Native DSP Core (15.0.0)

Our own C++17 port of the round-11 `delayline_aligned` design plus the
round-12 unity engagement fix, the recovered round-13/14 scope (cold-start
alignment law, sample-rate-relative detector window, zero-allocation
re-prepare), and the round-15 P1 **per-voice harmonizer parameter surface**:
per-voice pitch ratio / gain / mute / base-delay overrides, a wet/dry mix
with the latency-aligned dry path, and a click-free state-transparent
bypass — all audio-thread-safe with block-boundary application semantics.
**Not a VST3 plugin and not a reproduction of the original plugin's runtime
behaviour** — see `ARCHITECTURE.md` for the full note (state, memory
ownership, threading, channel isolation, limitations) and
`../docs/OCTAVOX_ROUND15_FINDINGS.md` for the research context.

[R16-P1 correction, 2026-10-28: the title and intro previously described
the round-12 state ("Round 12", 51 cases); superseded to the actual 15.0.0
state — see evidence/analysis_round16/R16_P1_RECONCILIATION_LOG.md.]

## Layout

```
include/octavox/dsp.hpp   public API (AlignedDelayLine, MultichannelShifter;
                         the ROUND-15 PARAMETER SURFACE contract is normative)
src/dsp.cpp               implementation (15.0.0, R15-REB1 rebuild)
src/obs_kernel_table.h    OBSERVED target .rdata kernel (opt-in mode; data, not code)
tests/test_main.cpp       correctness + unity + acceptance suite (85 cases)
tests/r15_acceptance_cases.cpp.inc  the 7 r15acc_* acceptance case definitions
tests/wav_io.*            WAV I/O (harness only; mirrors the Python CLI)
tests/alloc_counter.cpp   global-new counter for the no-alloc gate
bench/bench_main.cpp      release benchmark, 12 cases (deterministic LCG input;
                         incl. 96 kHz variants + the active-surface case)
cli/octavox_cli.cpp       WAV->WAV demo CLI (cross-language metrics driver)
cli/r15ref_capture.cpp    neutrality reference capture (octavox_r15refcap)
r15_reference_hashes.txt  the four default-neutrality regression hashes
VST3_READINESS.md         wrapper readiness assessment
ARCHITECTURE.md           architecture note (round-15 surface mechanisms)
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

* `octavox_tests` (release, 15.0.0): **85/85 pass** — the 51 frozen round-12
  cases (unity bit-exactness, the cent battery, 1/8 voices, mono/stereo,
  channel isolation, block-size invariance, repeated prepare, reset
  determinism, long runs, NaN/clip gates, steady-state allocation-freedom)
  + 19 R13-recovery cases (cold-start alignment, 96 kHz/44.1 kHz detector
  batteries, estimate tracking, RT re-prepare/reset/setters) + 7 P1 surface
  cases + 7 r15acc acceptance cases; machine-readable JSON
  (`../evidence/analysis_round15/r15_acceptance_results.json`).
* ASan+UBSan build: **85/85 pass, zero sanitizer reports**.
* Default-path neutrality: the four reference hashes in
  `r15_reference_hashes.txt` (12.0.0 == 15.0.0 bit-identical; re-verified
  by `octavox_r15refcap` before any round-16 edit).
* Benchmark (12 cases, mono/stereo 48 kHz + 96 kHz + active surface, best
  of 3): ≈ 15.5–15.9× realtime (1 voice) / ≈ 13.4–13.8× (8 voices) at the
  default 64-sample control interval; ≈ 50× realtime with the interval at
  512 (the YIN detector is ~73 % of the cost); 96 kHz ≈ 2.0–2.1× realtime
  (the R13-b window law price); stereo ≈ 6.4–7.9×. Zero steady-state
  allocations in every case.
* Cross-language (identical input, round-12 record): the Python reference
  runs at 0.42× (1 voice) / 0.25× (8 voices) realtime — the native core is
  ≈ 35–55× faster on the same machine.

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

## Round 16 additions (test/bench harness + the VST3 wrapper)

- **Harness (R16-P2):** per-case wall durations with live progress output,
  `--list` / `--filter <substr>` / `--group <id>` / `--category unit|long`
  selection, `--durations` table column, machine-readable JSON with an
  environment record and per-group subtotals; the benchmark gained
  `--case/--cases` named selection and a 13th row
  (`mono_8v_b128_96k_ctrl512`, the 96 kHz detector-cost isolation
  measurement). All 85 tests remain in the default full run; the gates are
  unchanged (220 `check(...)` calls textually identical; pre-edit sources
  preserved in `../evidence/analysis_round16/`). Measured here: release
  85/85 in ~41 s; ASan+UBSan 85/85 in ~6.2 min.
- **VST3 wrapper (R16-P4b):** `native/vst3/` — a REAL minimal VST3 plug-in
  around this core (module `OctavoxNative.vst3`, validator 47/47, wrapper
  tests 8/8 incl. the bit-exact sample-accurate-vs-block-boundary
  equivalence proof and the state roundtrips). Requires the VST3 SDK
  v3.7.12_build_20 (NOT redistributed; pinned-SHA acquisition record and
  build instructions: `vst3/VST3_WRAPPER.md`). The core itself is
  unchanged; the wrapper compiles it from `../src` independently.
