# Pitch Lab — C++20 DSP research system

**Local offline DSP research laboratory for sound-design-oriented pitch
processing.** This sub-tree is the Pitch Lab DSP system defined by the
architecture. It is NOT the web workbench (repo root) and shares nothing
with it at runtime.

**Current state: IMPLEMENTATION PHASE, cycles 1-3 complete (2026-09-26,
implementation specification §17 steps 1-3).** Cycle 1: core audio/frame
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
`corpus_gen --verify`). **Cycle 3 (§17 step 3): the FIRST REAL ENGINE —
`native.varispeed` (§6.1 + frozen behaviour §6.1.1, the rate-following
reference) — plus the harness: engine contract (§4.2/§4.2.1), registry
factory (§4.6, one engine registered — content always equals implemented
engines), ExperimentCompiler + OfflineRenderer + canonical-JSON manifests
with SHA-256 provenance (§4.8/§4.8.1), CLI `compile`/`render`, own SHA-256
+ canonical JSON primitives.** 14 CTest tests are green (T-E1..T-E11
contract suite through the real harness, T-A1 allocation audit, T-D3 reset
reuse, T-LEN-CAL length-calibration evidence — worst |Δ| = 8 frames over
the full battery × 6 rates, §13.4; OD-6 stays open for ratification).
Example render: `./build/pitchlab render
experiments/suites/example-varispeed-basic.toml --root .` — byte-identical
regeneration (T-E6). The remaining four engines are §17 step-4 work; none
is faked. Measured §7.7 acceptance evidence and the open decision it
produced (OD-18: resampler constants vs kernel-level criteria) are recorded
in the implementation specification §7.7.1.

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
implemented) · `src/engines/` own engines (`varispeed_engine` —
**implemented, registered**; the other four are §17 step-4 work) ·
`src/harness/` ExperimentCompiler + OfflineRenderer + manifest/JSON writer
(implemented) · `src/cli/` the `pitchlab` CLI (`compile`, `render`,
`engines`, `--version`) · `external/` vendored permissive third-party
(doctest 2.4.12) · `tests/` C++ tests (14 CTest targets) · `tools/`
stand-alone tools (`corpus_gen` — implemented) · `artifacts/` GENERATED
(gitignored; deterministic regeneration, manifests are sidecar provenance —
never source of truth).
