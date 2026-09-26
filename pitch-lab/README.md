# Pitch Lab — C++20 DSP research system

**Local offline DSP research laboratory for sound-design-oriented pitch
processing.** This sub-tree is the Pitch Lab DSP system defined by the
architecture. It is NOT the web workbench (repo root) and shares nothing
with it at runtime.

**Current state: IMPLEMENTATION PHASE, cycle 1 complete (2026-09-26,
implementation specification §17 step 1).** Foundational components are
implemented and unit-tested: core audio/frame types (§4.1), deterministic
PCG64 RNG + SplitMix64 consumer mixing (§4.7), own WAV I/O — IEEE float
32/64, extensible, deterministic writer (§9), and the shared band-limited
resampling primitive — Kaiser windowed-sinc kernel, presets, per-sample
ratio block resampling, anti-alias FIR (§7, frozen behaviour §7.2.1). 7
CTest tests are green. NO DSP engine is implemented and none is faked: the
engine registry (`src/core/engine_registry.cpp`, the single authoritative
identity source) is deliberately empty and a CI test asserts exactly that.
Measured §7.7 acceptance evidence and the open decision it produced (OD-18:
resampler constants vs kernel-level criteria) are recorded in the
implementation specification §7.7.1.

## Authoritative documents (live in the repository root `research/`)

| Document | Role |
|---|---|
| `../research/pitch-lab-v0.1-implementation-specification.md` | v0.1 implementation specification (contracts, engines, tests) |
| `../research/pitch-lab-build-and-ci-environment.md` | canonical build/CI environment |
| `../research/pitch-lab-architecture-design.md` | architecture v1.1 (governing DSP structure) |
| `../research/doppler-whip-pitch-research-report.md` | research findings |
| `../research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` | engineering process framework |

(The architecture's proposed `pitch-lab/docs/` location is satisfied by the
repository root `research/` tree — one canonical location, no duplicates.)

## Build (canonical environment: see the build & CI spec)

```bash
# Canonical (CI): ubuntu-24.04, GCC 13.3.0, CMake 3.31.6 (sha256-pinned), Ninja
cmake -S pitch-lab -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pitchlab --version && ./build/pitchlab engines
```

Tests use the vendored doctest 2.4.12 (`external/doctest/`, MIT — see its
`ORIGIN.toml`; dev/test only, never linked into DSP runtime code).

Generated outputs land in `pitch-lab/artifacts/` (gitignored, reconstructable
bit-for-bit for deterministic engines — never source of truth).

## Layout (architecture §B.3)

`config/` authoritative defaults · `experiments/` authoritative experiment
+ curve definitions · `assets/corpus/` authoritative inputs · `src/core`
harness + shared primitives (types, errors, RNG, WAV I/O, resampler —
implemented) · `src/engines/native/` own engines (implementation phase) ·
`src/analysis/` C++ metric modules (implementation phase) · `external/`
vendored permissive third-party (doctest 2.4.12) · `tests/` C++ tests ·
`tools/` stand-alone tools (corpus generator, implementation phase) ·
`artifacts/` GENERATED (gitignored).
