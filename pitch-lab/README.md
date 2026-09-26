# Pitch Lab — C++20 DSP research system

**Local offline DSP research laboratory for sound-design-oriented pitch
processing.** This sub-tree is the Pitch Lab DSP system defined by the
architecture. It is NOT the web workbench (repo root) and shares nothing
with it at runtime.

**Current state: IMPLEMENTATION PHASE, cycles 1-4 complete (2026-09-26,
implementation specification §17 steps 1-4 — the v0.1 engine registry is
COMPLETE).** Cycle 1: core audio/frame types (§4.1), deterministic PCG64 RNG
+ SplitMix64 consumer mixing (§4.7), own WAV I/O — IEEE float 32/64,
extensible, deterministic writer (§9), and the shared band-limited
resampling primitive (§7, frozen behaviour §7.2.1). Cycle 2: the curve
library (§4.4 + frozen behaviour §4.4.3.1 — own strict TOML-subset parser,
the 21-file curve battery) and the deterministic synthetic corpus (§11 —
`tools/corpus_gen` + committed 17-item corpus under `assets/corpus/syn-*/`).
Cycle 3 (§17 step 3): the harness — engine contract (§4.2/§4.2.1), registry
factory (§4.6), ExperimentCompiler + OfflineRenderer + canonical-JSON
manifests with SHA-256 provenance (§4.8/§4.8.1), CLI `compile`/`render`, own
SHA-256 + canonical JSON — and the FIRST real engine, `native.varispeed`
(§6.1, the rate-following reference). **Cycle 4 (§17 step 4): ALL FOUR
remaining engines, each frozen (§6.x.1) BEFORE coding and entering the SAME
contract suite T-E1..T-E13 + T-A1 + T-D3 through the real harness —
`native.vardelay` (§6.2, variable-delay Doppler-style shifter),
`native.granular` (§6.5, the seeded textural engine), `native.pv.classic`
(§6.3, the classic phase-vocoder baseline, powered by the vendored
pocketfft) and `native.pv.phaselocked` (§6.4, the L-D'99 peak-shift
phase-locked vocoder: per-region frequency translation, identity locking as
the rigid rotation Z, OLA on the analysis grid — no stretch, no resampler).**
The registry equals the §14 v0.1 end state: exactly the five implemented
engines, asserted by T-E19. **18 CTest tests are green** (locally; measured
evidence §13.5 — e.g. pv.phaselocked: identity residue 2.7e-11, 0.00-cent
pitch at 1.5x/0.75x/2.0x, bit-identical across block schedules, zero
allocations in the processing path). Example render: `./build/pitchlab
render experiments/suites/example-varispeed-basic.toml --root .` —
byte-identical regeneration (T-E6). Remaining v0.1 work: §17 step 5
(metrics) and step 6 (the full-suite reproducibility gate) — owner-triggered.
Measured §7.7 acceptance evidence and OD-18 (resampler constants) are
recorded in the implementation specification §7.7.1; OD-6 (length-tolerance
ratification) remains open with its §13.4 evidence pack.

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
# First real render (cycle 3): byte-identical regeneration (T-E6)
./build/pitchlab render experiments/suites/example-varispeed-basic.toml --root .
```

Tests use the vendored doctest 2.4.12 (`external/doctest/`, MIT — see its
`ORIGIN.toml`; dev/test only, never linked into DSP runtime code).

Generated outputs land in `pitch-lab/artifacts/` (gitignored, reconstructable
bit-for-bit for deterministic engines — never source of truth).

## Layout (architecture §B.3)

`config/` authoritative defaults (incl. `corpus.toml`, the committed generator
config) · `experiments/curves/` the 21-file curve battery (implemented) ·
`experiments/suites/` experiment files (`example-varispeed-basic.toml` —
the cycle-3 example render configuration) · `assets/corpus/` authoritative
inputs — the committed synthetic corpus (generated once by
`tools/corpus_gen`, byte-identity gated) · `src/core` harness contracts +
shared primitives (types, errors, engine contract, registry, RNG, WAV I/O,
resampler, TOML subset parser, curve compiler, corpus generator, SHA-256 —
implemented) · `src/engines/` own engines — **ALL FIVE v0.1 engines
implemented + registered** (`varispeed`, `vardelay`, `pv_classic`,
`pv_phaselocked`, `granular`; the registry == the §14 v0.1 end state) ·
`src/harness/` ExperimentCompiler + OfflineRenderer + manifest/JSON writer
(implemented) · `src/cli/` the `pitchlab` CLI (`compile`, `render`,
`engines`, `--version`) · `external/` vendored permissive third-party
(doctest 2.4.12; pocketfft — BSD-3, FFT for the PV engines, ORIGIN.toml-
pinned) · `tests/` C++ tests (18 CTest targets) · `tools/`
stand-alone tools (`corpus_gen` — implemented) · `artifacts/` GENERATED
(gitignored; deterministic regeneration, manifests are sidecar provenance —
never source of truth).
