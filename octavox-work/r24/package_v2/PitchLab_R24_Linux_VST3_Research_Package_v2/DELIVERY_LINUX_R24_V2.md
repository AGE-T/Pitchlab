# DELIVERY — PITCHLAB R24 LINUX VST3 RESEARCH PACKAGE v2 (targeted corrections)

**Archive:** `PitchLab_R24_Linux_VST3_Research_Package_v2.zip`
**Round:** R24-v2 (targeted delivery corrections) · **Date:** 2026-11-08
**Source:** octavox-native 16.3.0, repository `main`, corrective commit on
top of `ac6612ca4c2ee6ee8941f80eefde8796686a07d7` · **Original binary:
never executed.**

---

## 0. What this package is (and is not)

This is the corrected Linux VST3 delivery of the R24-SIMD round: the
validated native DSP engine, the AVX2 optimization with safe Scalar
fallback, the reproducible Linux build, and the complete R23/R24 research
evidence — with the four targeted corrections applied (glibc compatibility,
VST3 class-ID collision, Lagrange6Q identity test, SIMD production default).

**Baseline honesty record:** the correction brief referenced a previously
delivered `PitchLab_R24_Linux_VST3_Research_Package.zip` at source commit
`757f242`. **Neither exists on this system** — no such archive anywhere on
the filesystem, no such commit in the repository. The actual baseline was
HEAD `ac6612c` (content-identical working tree) with only one of nine VST3
bundles ever built. This v2 package is therefore the first *complete* Linux
VST3 research package, built directly with all four corrections applied;
the pre-correction GLIBC_2.38-requiring single-bundle build remains in the
repository (`octavox-work/r24/source/build_r24_vst3/`) as the Debian-13
reference. Evidence: `evidence/corrections_v2/logs/b0_baseline.txt`.

## 1. The four corrections (decision records at package root)

| # | correction | decision record |
|---|---|---|
| A | Linux glibc compatibility (`GLIBC_2.38`/`__isoc23_sscanf` eliminated; floor 2.34) | `LINUX_COMPATIBILITY_REPORT.md` |
| B | VST3 class-ID collision (Lagrange6Q minted its own FUID; lineage preserved) | `VST3_IDENTITY_DECISION_RECORD.md` |
| C | Lagrange6Q identity test (authority-based expectation + FUID-uniqueness guard) | `VST3_IDENTITY_DECISION_RECORD.md` §5 |
| D | SIMD production default `Scalar → Auto` (bit-exact AVX2 + proven fallback) | `SIMD_DEFAULT_DECISION_RECORD.md` |

Every source modification traces to exactly one of these four corrections:
`vst3/linux_compat_isoc23.cpp` + its CMake wiring (A);
`vst3/octavox_build_config.h` identity block + authority constant (B/C);
`vst3/octavox_vst3_tests.cpp` expectation fix + new case 22 (C);
`include/octavox/dsp.hpp` + `include/octavox/simd.hpp` default change +
`tests/test_main.cpp` two test updates (D); documentation amendments
(`PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md` Decision 3 amendment, findings,
worklog). Nothing else was touched; the validated R24 SIMD kernels are
byte-identical to the R24 record.

## 2. Contents

```
plugins/
  recommended/              the three recommended configurations
    PitchLab_Lagrange6_Balanced.vst3     (the R16-R20 lineage class)
    PitchLab_Lagrange6_Hybrid.vst3
    PitchLab_ObsKernel6_Balanced.vst3
  additional_variants/      the remaining six (benchmark/auxiliary builds;
                            FullRate = offline/reference use; Lagrange6Q =
                            the auxiliary kernel variant, own identity since v2)
source/
  native/                   the full native core tree (builds with CMake)
  vst3/                     the VST3 wrapper tree (nine-variant matrix)
research/
  R23/  R24/  mathematical_references/
evidence/
  linux_build/  vst3_validation/  regression/  sanitizers/  benchmarks/
  corrections_v2/           this round's baseline + audit evidence
build_configuration/       exact commands, environment record, build scripts
README.md  DELIVERY_LINUX_R24_V2.md  SHA256SUMS
LINUX_COMPATIBILITY_REPORT.md  VST3_IDENTITY_DECISION_RECORD.md
SIMD_DEFAULT_DECISION_RECORD.md  REFERENCES.md
```

## 3. Validation states (the four-state separation — no conflation)

1. **Successfully built:** YES — all nine VST3 plugin targets built from
   clean directories, native Linux x86_64, GCC 14.2.0, CMake 4.4.4,
   Release. Bundle structure/`moduleinfo.json` verified per bundle.
2. **Successfully validated (SDK-level):** YES — per variant: VST3 wrapper
   suite 22/22, compiled-module integration 12/12 (real `dlopen` through
   the SDK hosting stack + full interface contract + bit-exact audio vs the
   core reference), Steinberg VST3 validator 47/47; dual-module null test
   PASS; class-ID audit PASS; binary audits PASS. Native suite 110/110
   Release and 110/110 ASan/UBSan with zero sanitizer reports; 8/8
   historical hash gates bit-exact under the new default.
3. **Successfully loaded in a DAW (real host application):** **NOT TESTED.**
   No Linux DAW (REAPER/Ardour/etc.) is installed in this environment. The
   module-integration harness is an SDK-hosting load test, not a DAW test,
   and is not reported as one.
4. **Linux runtime compatibility:** executed on Debian 13 (glibc 2.41) —
   every bundle dlopen'ed and processed audio; Ubuntu 22.04 LTS / Debian 12
   / RHEL 9 compatibility established by the symbol-version audit (glibc ≤
   2.34, GLIBCXX ≤ 3.4.29 required) — not by execution there (no container
   runtime available; stated as a limitation).

## 4. Reproducibility

Exact commands: `build_configuration/BUILD_COMMANDS.md`. Environment
record: `build_configuration/environment.txt`. Per-TU ISA wiring (AVX2/FMA
flags ONLY on the two SIMD units — never global) is verifiable in the
CMake files shipped under `source/`. The VST3 SDK v3.7.12_build_20 is not
redistributed; acquire it with the pinned-SHA script
`source/native/tools/get_vst3_sdk.sh`.

## 5. Remaining limitations (complete list)

* No DAW-level validation (see §3 state 3).
* Older-distro compatibility is symbol-audit-based, not execution-based
  (Ubuntu 22.04/Debian 12/RHEL 9); no containers available.
* Auto→Scalar fallback executed only via the resolver rule gates on this
  AVX2 machine (design-guaranteed unreachable AVX2 entry points on
  non-AVX2 hosts; no non-AVX2 machine available).
* The FMA level remains opt-in; per-host FMA-vs-AVX2 performance is
  documented as a per-machine measurement (unchanged R24 record).
* Wrapper/integration/validator tool binaries themselves still reference
  glibc 2.38 symbols (build-host tools, not shipped plugin binaries).
* `voice_loop`/`interp_reads` SIMD (the R25 candidate) is out of scope and
  not started, per the scope boundaries.

## 6. The next milestone (unchanged roadmap)

The native C++ Phase Vocoder, followed by spectral phase locking and
transient preservation. The original granular engine remains intact. No
new development direction was started by this corrective round.
