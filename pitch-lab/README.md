# Pitch Lab — C++20 DSP research system

**Local offline DSP research laboratory for sound-design-oriented pitch
processing.** This sub-tree is the Pitch Lab DSP system defined by the
architecture. It is NOT the web workbench (repo root) and shares nothing
with it at runtime.

**Current state: IMPLEMENTATION PHASE, cycles 1-2 complete (2026-09-26,
implementation specification §17 steps 1-2).** Cycle 1: core audio/frame
types (§4.1), deterministic PCG64 RNG + SplitMix64 consumer mixing (§4.7),
own WAV I/O — IEEE float 32/64, extensible, deterministic writer (§9), and
the shared band-limited resampling primitive — Kaiser windowed-sinc kernel,
presets, per-sample ratio block resampling, anti-alias FIR (§7, frozen
behaviour §7.2.1). Cycle 2: the curve library (§4.4 + frozen behaviour
§4.4.3.1 — own strict TOML-subset parser, spec validation with
ConfigError{file, field}, deterministic compilation to the dense per-frame
ratio signal, the 21-file curve battery in `experiments/curves/`), and the
deterministic synthetic corpus (§11 — `tools/corpus_gen` + committed
`config/corpus.toml`, 17 items under `assets/corpus/syn-*/` with float64
WAVs + §15.5 metadata; regeneration byte-identity gated by T-C1 and
`corpus_gen --verify`). 9 CTest tests are green. NO DSP engine is
implemented and none is faked: the engine registry
(`src/core/engine_registry.cpp`, the single authoritative identity source)
is deliberately empty and a CI test asserts exactly that. Measured §7.7
acceptance evidence and the open decision it produced (OD-18: resampler
constants vs kernel-level criteria) are recorded in the implementation
specification §7.7.1.

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

`config/` authoritative defaults (incl. `corpus.toml`, the committed generator
config) · `experiments/curves/` the 21-file curve battery (implemented) ·
`assets/corpus/` authoritative inputs — the committed synthetic corpus
(generated once by `tools/corpus_gen`, byte-identity gated) · `src/core`
harness + shared primitives (types, errors, RNG, WAV I/O, resampler, TOML
subset parser, curve compiler, corpus generator — implemented) ·
`src/engines/native/` own engines (implementation phase) · `src/analysis/`
C++ metric modules (implementation phase) · `external/` vendored
permissive third-party (doctest 2.4.12) · `tests/` C++ tests (9 CTest
targets) · `tools/` stand-alone tools (`corpus_gen` — implemented) ·
`artifacts/` GENERATED (gitignored).
