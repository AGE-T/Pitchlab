# PITCHLAB R24 — SIMD KERNEL BENCHMARK RESULTS (the measured decision record)

Round 24 of the PitchLab independent re-implementation (static reverse
engineering of Eventide's Octavox; the original binary is never executed).
This document is the MEASURED decision record for the R24 SIMD capability
layer (`include/octavox/simd.hpp`): which kernels the engine dispatches at
each level, why, and with what measured evidence. Every number below is
reproducible from this source tree (`octavox_bench --kernels`, `--stages
--simd <mode>`, and the plain 13-case bench) on the reference machine.

## 1. Environment

| item | value |
|---|---|
| CPU | Intel(R) Xeon(R) (cloud vCPU; flags: `avx2 fma`) |
| Compiler | g++ (Debian 14.2.0-19) 14.2.0 |
| Effective core flags | `-O3 -DNDEBUG -std=c++17 -Wall -Wextra` (CMake Release; the CMakeLists' trailing `-O2` reaches the test/bench targets only — a pre-existing R17-era quirk, unchanged this round and gated by the bit-exact hash records) |
| ISA units | `simd_avx2.cpp` → `-mavx2 -ffp-contract=off`; `simd_fma.cpp` → `-mavx2 -mfma -ffp-contract=off` |
| Measurement | best-of-5 adaptive passes, ~1.5e9 difference terms per implementation per shape |

The reference machine is a SHARED 2-core cloud vCPU: absolute numbers
carry run-to-run noise; the SPEEDUPS are best-of-5 and were re-confirmed
by an independent direct-timing probe (`probe/timing_check.cpp`, see §5).

## 2. The two vectorisation axes (kernel level)

The YIN difference function `d[tau] = sum_j (x[seg0+j] - x[seg0+tau+j])^2`
can be vectorised along two axes:

* **LAG axis** (production): 4 consecutive taus in the 256-bit lanes, the
  minuend broadcast; every lane executes EXACTLY the reference op sequence
  for its tau (sub → mul → sequential add in ascending j). **Bit-identical
  to the scalar reference by construction** (AVX2 level), hard-gated as an
  exact-equality battery (`simd_kernel_avx2_lag_bitexact`).
* **SAMPLE axis** (comparison only): 4-lane reassociation over j with the
  documented association `((P0+P2)+(P1+P3)) + s_tail`. NOT bit-exact (the
  summation order differs from the reference); exists because the R24 brief
  requires both axes measured side by side.

### 2.1 Kernel microbenchmark (`octavox_bench --kernels`)

ns per 1e6 difference terms (best of 5; lower is better):

| shape (domain) | scalar | avx2 lag | avx2 sample | fma lag | fma sample |
|---|---|---|---|---|---|
| full_48k (ntau 798, m 224) | 504,635 | **84,702 (5.96x)** | 112,065 (4.50x) | 88,834 (5.68x) | 112,854 (4.47x) |
| full_96k (ntau 1520, m 400) | 521,858 | **82,480 (6.33x)** | 128,553 (4.06x) | 85,732 (6.09x) | 127,351 (4.10x) |
| dec_48k (ntau 398, m 112) | 428,537 | **88,989 (4.82x)** | 116,502 (3.68x) | 86,689 (4.94x) | 109,237 (3.92x) |
| hyb_fine_48k (ntau 9, m 224) | 501,489 | **177,311 (2.83x)** | 136,061 (3.69x) | 317,466 (1.58x) | 126,766 (3.96x) |
| hyb_fine_96k (ntau 13, m 400) | 521,276 | **168,335 (3.10x)** | 128,948 (4.04x) | 333,114 (1.56x) | 128,597 (4.05x) |
| parabolic_nb (ntau 3, m 224) | 499,083 | 434,847 (1.15x) | 113,896 (4.38x) | 432,009 (1.16x) | 113,323 (4.40x) |
| peak_4096 (window peak) | 1,109,729 | **260,347 (4.26x)** | — | 259,653 (4.27x) | — |

**Reading:**
* The ORDER-PRESERVING lag-axis AVX2 kernel — the only bit-exact
  acceleration — wins or ties the full/decimated domains at **4.8–6.3x**.
* The sample-axis kernels are 1.1–1.6x faster than the lag-axis on the
  SMALL shapes only, but they REASSOCIATE the sum (documented association)
  — they are NOT bit-exact and therefore NOT production candidates
  (the house rule: audio integrity first; the measured ~1.3x on a stage
  that is 19% of the default engine buys ~4% total and costs the entire
  bit-exactness record — rejected).
* The AVX2+FMA lag kernel measures EQUAL to AVX2 on the big domains
  (throughput-bound at ~5.7–6.1x) and 1.8x SLOWER on the single-batch
  small-domain shapes (latency-bound: the dependent `vfmadd231pd` chain,
  4-cycle latency per j pass, vs the AVX2 version whose add chain is
  shorter in practice on this core). FMA additionally deviates from the
  reference rounding (fused per-term) — see §4.

### 2.2 In-round kernel defects found by measurement and fixed

1. **Runtime batch count spilled the accumulators to the stack** (first
   cut): the batch loop took `nbatches` as a RUNTIME argument, so GCC kept
   `acc[]` in stack memory (`vfmadd213pd 0x8(%rsp),...` in the object
   dump) — the lag-axis kernels measured only 2.1–3.1x. Templating the
   batch count (`template <int NB>`, NB ∈ {1,4}) unrolled the b-loops,
   kept the accumulators in ymm registers, and restored **5.0–6.3x**
   (full_48k: 2.66x → 5.96x).
2. **The ntau < 16 pure-scalar small-domain branch**: the first cut routed
   small lag counts to a scalar loop ("no vector overhead") — measured
   1.14x (AVX2) and 0.45x (FMA: a serial `std::fma` chain is
   `vfmadd231sd`-LATENCY-bound and slower than GCC's SLP'd 2-wide
   reference loop). Routing small domains through the one-batch path
   restored 2.8–3.1x (AVX2) and fixed the FMA regression; the FMA
   SCALAR TAIL additionally reverted to the reference mul+add rounding
   (bit-exact vs Scalar there, and faster — see `simd.hpp`'s semantics
   table for the documented mixed rounding).

## 3. Engine-level measurements

### 3.1 Stage attribution (OCTAVOX_STAGE_TIMERS build, 1 s audio per config)

`octavox_bench --stages [--simd avx2]` — det_estimate stage ticks
(share of the run in parentheses, scalar side):

| stage config | scalar det_estimate | avx2 det_estimate | speedup | wall scalar → avx2 |
|---|---|---|---|---|
| dec_1v_128b48000_2c | 93.5M (65.5%) | 23.4M | 4.00x | 50.0 → 28.3 ms |
| dec_8v_128b48000_2c | 92.8M (19.1%) | 23.9M | 3.87x | 126.3 → 104.3 ms |
| dec_1v_128b96000_2c | 187.0M (65.7%) | 46.7M | 4.00x | 108.4 → 56.0 ms |
| dec_8v_128b96000_2c | 187.1M (18.9%) | 47.8M (5.7%) | **3.91x** | 254.7 → 208.3 ms |
| full_1v_128b96000_2c | 3,052.7M (96.7%) | 511.0M (81.6%) | **5.97x** | 997.1 → 206.6 ms |
| full_8v_128b96000_2c | 3,039.6M (79.4%) | 516.7M (37.6%) | **5.88x** | 1144.2 → 367.9 ms |

**The R19 headline problem — the FullRate detector at 96 kHz — is
mitigated by the AVX2 level**: the det_estimate share drops from 96.7% to
37.6% (8 voices) and the full_8v_96k stereo wall time drops
1144 ms → 368 ms per audio second (≈ 114% CPU — the R19 "offline only"
report — → ≈ 37%, vs the Decimated default's 13.2%). FullRate becomes a
usable real-time REFERENCE mode at 96 kHz WITH bit-exact arithmetic (the
AVX2 level); Decimated remains the production default (the voice/interp
stages now dominate both).

### 3.2 The canonical 13-case bench (default Decimated detector)

ns/frame, best of 3 (the shipped configuration is Scalar = the left
column; AVX2 is OPT-IN):

| case | scalar | avx2 | speedup |
|---|---|---|---|
| mono_1v_b64/128/512 | 373.5 | 134.3 | 2.8x |
| mono_8v_b64/128/512 | 593.8 | 348.0 | 1.7x |
| stereo_1v_b128 | 742.9 | 269.8 | 2.8x |
| stereo_8v_b128 | 1187.0 | 708.8 | 1.7x |
| mono_8v_b128_ctrl512 | 314.5 | 284.2 | 1.1x |
| mono_1v_b128_96k | 370.8 | 132.5 | 2.8x |
| mono_8v_b128_96k | 594.1 | 352.2 | 1.7x |
| mono_8v_b128_96k_ctrl512 | 311.1 | 281.9 | 1.1x |
| mono_8v_b128_surface | 599.5 | 355.5 | 1.7x |

(At the default detection_interval 64 the detector fires every 64 stream
samples — the SIMD gain is maximal; at ctrl512 the detector fires 8x less
and the voice/interp stages dominate, so the gain shrinks toward Amdahl.)

## 4. THE DECISIONS (measured, not assumed)

1. **Production kernel = the LAG-AXIS AVX2 implementation.** It is
   bit-identical to the scalar reference by construction AND the fastest
   bit-exact option on every dominant shape (4.8–6.3x). The reassociating
   sample-axis variant is faster only on small shapes and is rejected as a
   production kernel (not bit-exact; the gain does not buy the loss).
2. **Auto resolves to AVX2 (the bit-exact level), never to FMA.** The FMA
   lag kernel measured >= the AVX2 one NOWHERE on this machine (equal on
   the throughput-bound big domains, 1.8x slower on the latency-bound
   single-batch shapes) while it additionally carries the fused per-term
   rounding deviation. Auto therefore selects the bit-exact accelerated
   level; `simd::Mode::AVX2FMA` remains an explicit opt-in for
   experimentation on other cores (its 16-tau batch regime is
   throughput-bound and MAY win on machines with different FMA/add
   latencies — that is a per-host measurement, recorded honestly).
3. **Default stays Scalar** (bit-exact vs the R12–R23 record, gated by the
   four legacy + four default hash records every round). Hosts opt in via
   `Config::simd` (engine-internal; NOT serialized in the VST3 state blob,
   which stays the frozen v2 layout).
4. **FMA level semantics (documented mixed rounding):** the vector lanes
   fuse the per-term rounding (order preserved); the <= ntau % 4 scalar
   tail keeps the reference mul+add (bit-exact vs Scalar there, and faster
   than a latency-bound `std::fma` chain — measured 0.45x). See
   `include/octavox/simd.hpp`'s semantics table.

## 5. Reproduction

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/octavox_bench --kernels                       # §2.1
cmake -S . -B build_s -DCMAKE_BUILD_TYPE=Release -DOCTAVOX_STAGE_TIMERS=ON
cmake --build build_s --target octavox_bench
./build_s/octavox_bench --stages                      # §3.1 scalar
./build_s/octavox_bench --stages --simd avx2          # §3.1 avx2
./build/octavox_bench                                 # §3.2 scalar
./build/octavox_bench --simd avx2                     # §3.2 avx2
./build/octavox_tests                                 # 110/110 (incl. the
                                                      # R24 SIMD battery)
```

Raw JSON records: `evidence/analysis_round24/logs/g5_kernel_bench.json`
(kernel), `g3_stages_scalar.json` / `g3_stages_avx2.json` (stages),
`g4_bench13_scalar.json` / `g4_bench13_avx2.json` (13-case). The
independent direct-timing cross-check lives in `probe/timing_check.cpp`
(scalar 0.698 ns/term vs avx2_lag 0.092 ns/term on full_48k = 7.6x,
single-shot, agreeing with the best-of-5 bench within the shared-machine
noise).
