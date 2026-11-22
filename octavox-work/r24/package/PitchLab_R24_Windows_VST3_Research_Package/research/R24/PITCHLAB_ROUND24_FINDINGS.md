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
* ~~**Windows dual build** with the ISA units~~ — **CLOSED by the
  R24-Windows delivery** (§7 below): the zig 0.13.0 cross toolchain was
  re-acquired, the Windows dual build executed, and the delivered binaries
  verified (see §7).
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

## 7. R24-Windows delivery (2026-11-09) — the two Windows x64 VST3 builds

A delivery round, not a DSP round (no kernel changes; two narrowly scoped
Windows portability fixes below). Baseline: this round's HEAD
`ac6612ca…` (the documented R24 baseline) + the corrective delta
`a76b26c` (the `edcddd5`-equivalent for this repository: **Config::simd
default Scalar → Auto** — Auto = the bit-exact AVX2 level with silent
Scalar fallback; FMA stays opt-in; test expectations follow the actual
default; the historical hash gates are UNCHANGED and now exercise the
Auto-resolved level end-to-end). The corrective commit `edcddd5` itself
does NOT exist in this repository (created in a different environment) —
recorded honestly; the delta was re-applied here, narrowly.

**Delivered (exactly two variants, per the brief):**
`PitchLab_Harmonizer_Balanced.vst3` (Lagrange6 + Decimated YIN, the
realtime production candidate) and `PitchLab_Harmonizer_FullRate.vst3`
(Lagrange6 + FullRate YIN, the high-cost detector reference — ~37% of a
core at 96 kHz stereo 8-voice with AVX2 vs ~114% scalar; not
automatically recommended for realtime). Distinct class IDs:
Balanced `6F4E62589A3142E18F3C5A971D2B4E60` (preserved lineage, state
blobs and sessions keep working), FullRate `C13E1BB55BFB0598F25DDD4A52B0E817`
(distinct mint; never previously shipped). Nine-bundle builds were NOT
repeated; Hybrid/Lagrange6Q/ObsKernel6 sources + identities preserved in
the tree, unbuilt this round.

**Windows portability defects found, fixed, tested (both reproduced
first, both narrowly scoped; kernels untouched):**
1. **zig 0.13.0 windows-gnu has no compiler-rt cpu_model** —
   `__builtin_cpu_supports` compiles but FAILS TO LINK (undefined
   `__cpu_indicator_init`/`__cpu_model`; reproduced with a minimal
   probe). Fix: Windows builds use the explicit CPUID+XGETBV algorithm
   (`src/simd_cpuid_explicit.h`, single source of truth; genuine MSVC
   keeps its `<intrin.h>` intrinsics). Linux keeps the R24-validated
   `__builtin` path UNCHANGED. **AVX2 is now ALSO gated on
   OSXSAVE/XCR0 YMM state** (the original R24 MSVC branch gated only
   FMA — both use YMM). The exact Windows algorithm is validated on real
   hardware: `-DOCTAVOX_FORCE_EXPLICIT_CPUID` +
   `octavox_r24w_cpuid_check` → explicit vs `__builtin` **AGREE**
   (avx2=1 fma=1 on this vCPU).
2. **missing explicit includes under libc++** — `simd_avx2.cpp`
   (`std::abs` → `<cmath>`) and `pitch_control.cpp` (`std::max` →
   `<algorithm>`); libstdc++ provided both transitively, zig's libc++
   does not.

**Validation (identical sources + variant defines, Linux side):** core
suite **110/110 Release AND 110/110 ASan/UBSan** (the Auto default
re-proves bit-exactness end-to-end: all eight historical hash gates now
run the resolved AVX2 level and still reproduce the Scalar-era hashes);
per variant: wrapper **21/21**, module integration **12/12**, Steinberg
validator **47/47**; the extended `octavox_dual_nulltest --fullrate`
loads Balanced + FullRate **into one process** (both dlopens coexist,
live factory re-probe OK): unity outputs **bit-identical** (the parked
path is detector-independent — the R19 unity-park finding), non-unity
outputs **differ** (genuine FullRate detector selection; a bit-identical
result would exit 1 as a build-config defect). 3/3 PASS.

**Windows binaries (zig 0.13.0 cross-build, x86_64-windows-gnu, PE32+,
MS x64 ABI, static libc++, system-DLL imports only — the recorded
R21/R22 toolchain deviation; Visual Studio/MSVC do not exist in this
Linux sandbox, native MSVC = BLOCKED here, rebuild instructions
shipped):** machine-code ISA verification (objdump, pe-x86-64):
per-TU flags exactly on the two ISA units (`-mavx2` /
`-mavx2 -mfma` + `-ffp-contract=off`, no global ISA switch); object
scans: baseline TUs (dsp/pitch_control/simd_dispatch) contain **zero**
ymm/vfmadd/VEX-FP instructions; final DLL arithmetic: **ymm 154 = 86
(AVX2 unit) + 68 (FMA unit)** and **vfmadd 26 = 26 (FMA unit only)** —
every 256-bit/FMA instruction in the shipped DLLs originates from the
ISA-gated kernel units; `cpuid` ×3 (leaves 0/1/7) + `xgetbv` ×1 present
(the Windows detection path). Structural verification
(`r24w_windows_verification.json`): PE32+ x86-64, GetPluginFactory
export, import allow-list, both FUIDs in COM layout (and each variant's
DLL contains ONLY its own FUID), moduleinfo identity, canonical bundle
layout, distinct CIDs — all OK. A **Windows Validation Kit**
(`octavox_dual_nulltest.exe` + `.cmd`) ships for on-target verification.

**Honest four-state classification (the delivery report
`DELIVERY_WINDOWS_R24.md` has the full table):** build PASS (cross,
documented deviation), Linux-side validation PASS, structural Windows
verification PASS, **real-DAV validation NOT TESTED** (no Windows host in
this sandbox — the kit + steps are provided), native-MSVC toolchain
**BLOCKED** in this environment. The delivery package:
`PitchLab_R24_Windows_VST3_Research_Package.zip` (SHA-256 in the
delivery notes and SHA256SUMS).
