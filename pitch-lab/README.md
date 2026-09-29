# Pitch Lab — C++20 DSP research system

**Local offline DSP research laboratory for sound-design-oriented pitch
processing.** This sub-tree is the Pitch Lab DSP system defined by the
architecture. It is NOT the web workbench (repo root) and shares nothing
with it at runtime.

**Current state: IMPLEMENTATION PHASE, cycles 1-5 complete (2026-09-26/27,
implementation specification §17 steps 1-5 — the v0.1 engine registry AND
the analysis layer are COMPLETE).** Cycle 1: core audio/frame types (§4.1), deterministic PCG64 RNG
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
byte-identical regeneration (T-E6). **Cycle 5 (2026-09-27, §17 step 5):
the ANALYSIS LAYER — complete.** `src/analysis/` implements the frozen
§10.4: the metric registry (single in-code identity authority — 15 ids,
unknown id ⇒ CONFIG ERROR), pure-function metric modules (peak-rms-crest,
realised-duration, latency, onset-timing, transient-preservation, hf-energy,
aliasing-indicator, spectral-error, amplitude-modulation, phase-coherence,
stereo-coherence; the tracker-dependent trio MEASURED through the OD-12
clean-room pYIN since cycle 6, cpu-cost gated `not-applicable`), the shared
spectral primitives (per-rate
STFT, exact-frequency projection, Hilbert envelope, the frozen onset
detector, emission-map warp), the analyzer (manifest + SHA-256-verified
master + hash-cross-checked curve recompilation, varispeed reference
resolution, the full failure model) and the `pitchlab analyze` CLI writing
deterministic `pitchlab.analysis.v1` artifacts under `artifacts/analysis/`
(byte-identical on delete + re-run). **27 CTest tests are green** (the seven
T-M suites carry the analytic goldens: self-reference spectral error ≡ 0
exactly, the 8-onset percussive-recipe golden, bin-centred leakage-floor
classes, artifact determinism, the failure-model matrix); the 60-job
cross-engine metric matrix evidence is §13.6. **Cycle 6 (2026-09-28, OD-12):
the clean-room C++ pYIN ReferencePitchTracker — complete** (`src/analysis/pitch_tracker.{h,cpp}`
per the frozen §10.5: YIN CMND stage + 100-threshold Beta(2,18) prior +
parabolic refinement + the 2M-state HMM + Viterbi + forward-backward
posteriors; no external pYIN source imported or linked; the corpus [f0]
recipe metadata supplies the analytic expected side; pitch-error/
pitch-lag/warble-instability now MEASURE — evidence §13.7: sub-cent identity
medians, the delayed-curve lag golden == 4 frames EXACTLY, the control-rate
lag discriminants (varispeed/vardelay/pv.classic 0.000 ms vs pv.phaselocked
21.3 ms / granular 32.0 ms); CTest 27, byte-deterministic artifacts).** NO
global quality score
exists anywhere — metrics are independent reported dimensions. **Cycle 7
(2026-09-28): §17.6 — THE FINAL GATE — PASS, and the RESULT PRODUCT.** The
repository-level verification & reproducibility gate (owner final-gate
directive: existing evidence reconciled, genuinely missing checks executed —
clean-checkout CI re-verified, the full artifacts tree regenerated 180/180
byte-identical, corpus `--verify` 17/17, hidden-dependency scan clean,
provenance sha256 4/4, source/build separation + doc/SoT consistency
verified; the record is spec §17 step 6). Then the first tangible v0.1
result: **`results/v0.1/` — the committed comparative five-engine battery**
(30 jobs, 3 materials × 2 curves × 5 engines through the REAL pipeline;
float32 listening renders + manifests + analysis JSONs + the human-readable
report at `results/v0.1/README.md`; evidence §13.8; `pitchlab verify` /
`report` / `listen-index` stay unimplemented by owner decision — the T-G2
semantics were executed as the recorded gate checks). v0.1 is COMPLETE:
§17.6 is the LAST gate; the roadmap continues at the owner's direction.

**PRODUCT PHASE (2026-09-28/29): the REAL VST3 PLUGIN + UI — complete.**
`src/vst/` adds the product layer on the UNMODIFIED v0.1 engines: the VST3
single-component processor + module entry (official Steinberg VST3 SDK +
VSTGUI, vendored under `external/vst3sdk` with ORIGIN.toml provenance), the
realtime adapter (preserving engines = envelope-bounded virtual jobs with
crossfaded seams; varispeed + granular = windowed-splice adaptation, the
recorded §3 correction — the frozen product-phase spec
`../research/pitch-lab-vst3-product-phase-specification.md` carries the
boundary analysis + the implementation-time corrections), the ONE
authoritative parameter model (`src/vst/parameters.h`), and the real
VSTGUI editor (`src/vst/ui/` — engine selector, pitch/LFO, per-engine
panels, meters, honest status; the Linux open path performs the XEMBED
run-loop handshake). Product evidence: `results/vst3/v0.1/` — 11 listening
examples rendered through the REAL VST3 process path (the factory +
lifecycle driver `tools/vst_host_sim.cpp`), 4 UI screenshots
(`tools/vst_ui_screenshot.cpp`, a DAW-style dlopen + XEMBED host under
Xvfb), the official SDK validator record **47/47**; the doctest suites
(`tests/vst_*_test.cpp`) cover parameters/state/adapter/processor
incl. every ordered engine-pair mid-stream switch. Docs:
`docs/vst3-product.md`. The v0.1 research surface is untouched
(`PITCHLAB_BUILD_VST3=OFF` restores it exactly).

Measured §7.7 acceptance evidence and OD-18 (resampler
constants) are recorded in the implementation specification §7.7.1; OD-6
(length-tolerance ratification) remains open with its §13.4 evidence pack;
OD-9 (metric tolerance values) remains open — nothing in any cycle ratified
or weakened any tolerance.

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
the cycle-3 example render configuration; `cross-engine-metrics-mono.toml` +
`-stereo.toml` — the cycle-5 60-job cross-engine metric matrix evidence
suites; `result-product-v0.1.toml` — the RESULT PRODUCT battery, §13.8) ·
`assets/corpus/` authoritative
inputs — the committed synthetic corpus (generated once by
`tools/corpus_gen`, byte-identity gated) · `src/core` harness contracts +
shared primitives (types, errors, engine contract, registry, RNG, WAV I/O,
resampler, TOML subset parser, curve compiler, corpus generator, SHA-256 —
implemented) · `src/engines/` own engines — **ALL FIVE v0.1 engines
implemented + registered** (`varispeed`, `vardelay`, `pv_classic`,
`pv_phaselocked`, `granular`; the registry == the §14 v0.1 end state) ·
`src/harness/` ExperimentCompiler + OfflineRenderer + manifest/JSON
writer + canonical-JSON reader (implemented) · `src/analysis/` the analysis
layer (metric registry, metric modules, spectral primitives, analyzer,
the clean-room pYIN tracker — implemented, §10.4/§10.5) · `src/cli/` the
`pitchlab` CLI (`compile`, `render`, `analyze`, `engines`, `--version`) ·
`external/` vendored permissive
third-party (doctest 2.4.12; pocketfft — BSD-3, FFT for the PV engines AND
the analysis STFT/Hilbert primitives, ORIGIN.toml-pinned) · `tests/` C++
tests (27 CTest targets — 18 engine/component suites + 7 T-M metric
suites + the 2 tracker suites `pitch_tracker_test`/`metric_tracker_test`) · `tools/` stand-alone tools
(`corpus_gen` — implemented) ·
`artifacts/` GENERATED (gitignored; deterministic regeneration — renders
via `pitchlab render`, analysis via `pitchlab analyze` (byte-identical on
re-run); manifests and analysis artifacts are sidecar provenance — never
source of truth) · `results/v0.1/` **RETAINED OUTPUT (committed): the v0.1
RESULT PRODUCT** — the 30-job comparative five-engine battery with f32
listening renders, manifests, analysis JSONs and the human-readable report
(`results/v0.1/README.md`); regenerable byte-identically (§13.8, §3).
