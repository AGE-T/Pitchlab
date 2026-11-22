# PITCHLAB ROUND 24 FINDINGS — SIMD CAPABILITY LAYER (YIN KERNELS)

**Round:** 24 (2026-11-06) · **Core:** octavox-native 16.3.0 · **Original binary: never executed.**
**Session context:** this round was started in the previous session (G0/G1
done, the first two SIMD source files written) and that session ran out of
context mid-task, leaving the layer UNWIRED (the declared `simd_fma.cpp`
unit missing, no CMake integration, no engine wiring, no tests, no
benchmark, no docs). This session completed the round from that state; the
in-round defects found and fixed below are recorded with their evidence.

## 0. What R24 delivers

1. **The SIMD capability layer** (`include/octavox/simd.hpp`,
   `src/simd_dispatch.cpp`, `src/simd_avx2.cpp`, `src/simd_fma.cpp`):
   * one-time CPUID feature detection (AVX2 / FMA independently),
   * a `Config::simd` mode (`Scalar` default = bit-exact vs the R12–R23
     record; `AVX2`; `AVX2FMA`; `Auto` → the bit-exact AVX2 level, the
     measured R24 decision),
   * the YIN difference-function and window-peak kernels in three levels,
     compiled per-TU (baseline library + `-mavx2` unit + `-mavx2 -mfma`
     unit with `-ffp-contract=off`; the library loads and runs on every
     x86-64 processor — the dispatch only hands out ISA entry points
     CPUID has cleared),
   * the RT-safety contract: kernels are pure functions over caller-owned
     buffers; the table is resolved ONCE in `PitchDetector::configure()`
     (prepare() time); `process()` performs only an indirect call.
2. **Engine integration:** `estimate()` / `estimateHybrid()` (all three
   detector modes) call the resolved kernels for the window peak, the
   coarse/fine difference functions and the parabolic neighbourhood;
   `prepare()` validates the mode, REJECTS un-honourable FORCED levels
   loudly and invalid enum values; in-place re-prepare re-resolves the
   table; the VST3 surface is untouched (38 ids, state blob v2 frozen,
   `simd` is engine-internal like `f0_min`/`tracking_gate`).
3. **The R24 battery** (10 new cases, group `r24`; suite 100 → 110): the
   AVX2 lag-axis kernel **bit-exactness hard gate** over a randomized
   21-shape corpus; peak bit-exactness; the FMA **bounded-deviation** gate
   (measured max 4.4e-16 relative; also gated "genuinely fused"); ENGINE
   bit-identity at the AVX2 level across 10 configs (3 detector modes ×
   rates × interpolations + tracking-gate paths); FMA engine closeness
   (measured: bit-identical outputs and estimates on the whole matrix);
   prepare() acceptance/rejection; default neutrality; in-place re-prepare
   level switching (fresh-engine bit-identity); allocation-free
   accelerated processing.
4. **Benchmarks + the measured decision record**
   (`PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md`): kernel-level (both
   vectorisation axes at both ISA levels), stage attribution
   (scalar vs avx2), the 13-case bench at both levels. `octavox_bench`
   gained `--simd <mode>` and `--kernels`.

## 1. Headline measurements (reference machine, best-of-5)

* **Kernel:** AVX2 lag-axis (bit-exact) **4.8–6.3x** on the
  full/decimated domains, 2.8–3.1x on the Hybrid fine stage, 1.15x on the
  3-lag parabolic call; window peak 4.3x.
* **Stage:** det_estimate **3.9–4.0x** (Decimated), **5.9–6.0x**
  (FullRate); FullRate 96 kHz stereo 8v wall **1144 → 368 ms/s** (≈114% →
  ≈37% CPU); Decimated 96 kHz stereo 8v **255 → 208 ms/s**.
* **Engine (13-case bench, default Decimated):** mono_1v **2.8x**,
  stereo_1v **2.8x**, mono_8v/stereo_8v **1.7x** (the voice/interp stages
  now dominate; their SIMD is the natural R25 candidate — see §5).
* **Decisions (measured):** production kernel = the lag-axis AVX2
  (bit-exact AND fastest bit-exact option everywhere that matters); the
  reassociating sample-axis variant is benchmark-only; **Auto → AVX2**
  (FMA measured ≥ AVX2 nowhere on this machine and carries the rounding
  deviation — FMA stays an explicit opt-in); default stays Scalar.

## 2. In-round defects found and fixed (all by our own gates/benches)

1. **The missing `simd_fma.cpp`** (left by the interrupted session): the
   dispatch unit declared `diffLagAxisFMA` / `diffSampleAxisFMA` /
   `peakFMA` with no definition — written this round (lag-axis
   order-preserving fused lanes; sample-axis comparison variant; peak
   delegation).
2. **Hybrid fine-stage off-by-one (found by the suite, 3 failing cases):**
   the first integration passed the parabolic neighbourhood as
   `diff(..., t0, 2, ...)` — which computes d(t0) and d(t0+1)=d(best),
   so `y2` silently became `bestD` and the vertex correction degenerated
   to exactly +0.5 every fire (a constant +1-lag bias: 880 Hz tracked as
   864.86 Hz; isolation probe in `probe/probe_hybrid.cpp`). Fixed by
   spanning THREE taus (`t0, best, t2`); the 110/110 suite and the
   digit-identical probe vs the pristine R23 core prove the repair.
3. **Runtime batch count spilled the accumulators to the stack** (found by
   the kernel bench: only 2.1–3.1x; confirmed by the object dump
   `vfmadd213pd 0x8(%rsp)`): templated the batch count → 5.0–6.3x.
4. **The `ntau < 16` pure-scalar small-domain branch** (1.14x AVX2, 0.45x
   FMA): routed through the one-batch path (2.8–3.1x); the FMA scalar
   tail reverted to the reference mul+add rounding (a serial `std::fma`
   chain is `vfmadd231sd`-latency-bound; the mixed rounding is documented
   in `simd.hpp`'s semantics table).
5. **The VST3 core library would not have linked** (`octavox_core_vst3`
   compiled only `dsp.cpp`): `pitch_control.cpp` (required since R23's
   tracking_gate — see §4) and the three SIMD sources are now listed with
   the same per-TU ISA layout as the native build.

## 3. Validation (final code, all gates green)

* Suite **110/110** Release AND **110/110** ASan/UBSan (zero reports);
  4/4 legacy (15.0.0) hash pins **bit-exact**
  (0x74318b06…, 0x46a091e9…, 0x9502cf88…, 0x6f7a2ce3…) and 4/4
  16.0.0-default record **bit-exact** (0x74318b06…, 0x9b721781…,
  0xb66549c1…, 0x38f625fe…).
* VST3 (REBUILT from the current core): wrapper **21/21**, module
  integration **12/12** on the fresh bundle, SDK validator **47/47**.
* R23 instruments on the final engine: control battery **290/290**,
  integration check **5/5**.
* Evidence: `octavox-work/r24/evidence/analysis_round24/logs/`
  (g0 baseline, g1 stage bench, g2 test JSONs, g3/g4/g5 bench JSONs,
  g6 final validation).

## 4. R23 delivery gap found and closed (honest record)

The R23 evidence tree's VST3 bundle
(`r23/source/build_r23/VST3/Release/PitchLab_Lagrange6_Decimated.vst3`,
built Nov 5 00:22) **predates the R23 engine deltas**: its symbols contain
neither `lastCmndfMin` (R23 G8 telemetry) nor `PitchControlLayer`
(the tracking_gate integration) — and neither does the R23
module-integration binary. The R23 "12/12 real-VST3-module" gate therefore
verified the PRE-delta wrapper. R24 closes the gap: the module is rebuilt
from the current core (R23 + R24 deltas; the vst3 CMakeLists now compiles
`pitch_control.cpp` + the SIMD units into `octavox_core_vst3`) and the
wrapper/integration/validator gates re-run green on the FRESH bundle.
*Classification: OBS (symbol dumps + the R23 delivery's own
`module_path` record); impact: the R23 module-level claim was stale, not
wrong about what it ran.*

## 5. Open items / next-round candidates

* **voice_loop + interp_reads SIMD** (55% + 24% of the 8-voice stage
  profile at 96 kHz — now the dominant cost): the natural R25 target;
  the stage-attribution harness already measures it.
* **Windows dual build** with the ISA units (the llvm-mingw toolchain was
  lost to the 6th sandbox reset — `/home/z/tools/llvm-mingw` is empty;
  the CMakeLists' MinGuard path is authored but unexecuted this round).
* **Positive-shift pre-read anti-alias filter** (the R23 top candidate,
  measured +9.4 dB HF @ +12 st) — still open, unchanged.
* **Auto-detection on other cores:** the FMA level may win on hosts whose
  FMA/add latencies differ from this vCPU; Auto's choice is a per-host
  measurement away (explicitly documented in the decision record).

## 6. Provenance

All R24 code is our own implementation. No original-plugin execution; no
"Octavox" branding in the product surface (the internal codename
`octavox-native` continues the established evidence-tree convention).
Synthetic corpora only, labelled as such. Evidence classes used: OBS
(measurements, disassembly excerpts), DER (bit-exactness by construction,
proven by hard gates), HYP (none new), UNK (none new).
