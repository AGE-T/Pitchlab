# PITCHLAB R24 — LINUX VST3 RESEARCH PACKAGE (v2, corrected)

**PitchLab Harmonizer** — an independent native re-implementation
(static reverse-engineering research; the original target binary was never
executed) — R24-SIMD round, targeted-corrections delivery.

Read first: **`DELIVERY_LINUX_R24_V2.md`** (what this package is, the four
corrections, the four-state validation record, limitations).

## Quick start (using the plugins)

Copy any bundle from `plugins/` into your VST3 plug-in directory
(`~/.vst3/` per-user, or `/usr/lib/vst3/` system-wide):

```
cp -R plugins/recommended/PitchLab_Lagrange6_Balanced.vst3 ~/.vst3/
```

* **Recommended configurations:** `PitchLab_Lagrange6_Balanced`,
  `PitchLab_Lagrange6_Hybrid`, `PitchLab_ObsKernel6_Balanced`.
* All nine variants are independently installable (pairwise-distinct
  processor class IDs since the v2 correction — see
  `VST3_IDENTITY_DECISION_RECORD.md`; sessions saved by R16–R20 modules
  keep restoring into the Lagrange6 Balanced class unchanged).
* **Linux requirement:** glibc ≥ 2.34 and libstdc++ ≥ 3.4.29 (Ubuntu 22.04
  LTS / Debian 12 or newer). Executed and validated on Debian 13. Full
  statement: `LINUX_COMPATIBILITY_REPORT.md`.
* The engine auto-selects the bit-exact AVX2 kernels on AVX2-capable CPUs
  (Scalar fallback elsewhere); FMA stays opt-in via `Config::simd`
  (engine-internal — never a host parameter).
* FullRate bundles are offline/benchmark reference builds (high CPU).

## Building from source

```
# native core + tests + hash gates + bench
cd source/native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/octavox_tests && ./build/octavox_r15refcap --legacy

# any VST3 variant (SDK v3.7.12_build_20 at ../../vst3sdk — see
# source/native/tools/get_vst3_sdk.sh, pinned SHAs)
cd ../vst3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DOCTAVOX_VST3_INTERPOLATION=LAGRANGE6 -DOCTAVOX_VST3_DETECTOR=DECIMATED
cmake --build build -j
```

Complete nine-variant loop and every validation command:
`build_configuration/BUILD_COMMANDS.md`.

## Layout

| path | content |
|---|---|
| `plugins/` | the nine built Linux x86_64 VST3 bundles (recommended + additional) |
| `source/native/` | the native DSP core, tests, benchmarks, CLI instruments |
| `source/vst3/` | the VST3 wrapper (nine-variant identity matrix, compat shim) |
| `research/R23/` | the R23 YIN research corpus (16 documents) |
| `research/R24/` | R24 SIMD findings + measured benchmark decision record + R21 identity lineage |
| `research/mathematical_references/` | YIN mathematical derivation + citation-only references |
| `evidence/` | build, validation, regression, sanitizer, benchmark, correction-audit evidence |
| `build_configuration/` | exact commands, environment record, build/audit scripts |
| package root | delivery report, the three correction decision records, references, `SHA256SUMS` |

## Integrity

`sha256sum -c SHA256SUMS` from this directory verifies every file (the
manifest covers the full tree; the archive-level checksum is published
next to the download). The historical R23/R24 evidence records are
preserved unmodified; the v2 correction evidence is kept separately under
`evidence/corrections_v2/`.

## Provenance

All code, tests, benchmarks, and documents in this package are the
project's own work. No original-vendor code, media, or copyrighted
third-party material is included; external sources are cited by reference
only (`REFERENCES.md`). The Steinberg VST3 SDK is not redistributed
(pinned-SHA acquisition script included). The internal `octavox-*`
namespaces continue the established evidence-tree convention; the
user-facing product identity is PitchLab Harmonizer / PitchLab Project.
