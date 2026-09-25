# PITCH LAB — V0.1 Implementation Specification

**Document status:** SPECIFICATION (implementation freeze). This document turns the architecture (v1.1, `research/pitch-lab-architecture-design.md`) into an implementation-level specification for the v0.1 scope. Per Operating Principles §81 the status chain of everything specified here is **DESIGNED** — nothing is implemented, tested, verified or integrated by this document. The C++ infrastructure skeleton committed in the same cycle (registry/version/CI smoke targets) is explicitly labelled infrastructure, not DSP implementation.
**Version:** v1.0 (initial issue)
**Date:** 2026-09-25
**Author role:** senior audio DSP architect / software engineer (AI-assisted)

**Governing documents, in authority order:**

1. `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` — engineering process (130 sections).
2. Owner constraint mandate, task "PITCH LAB V0.1 — IMPLEMENTATION FREEZE & CI READINESS" (2026-09-25) — owner-level decisions, locked (§1.3 below).
3. `research/pitch-lab-architecture-design.md` v1.1 — Pitch Lab architecture (governs DSP structure; amended only by the recorded amendments in §2.4 of this document).
4. `research/doppler-whip-pitch-research-report.md` — research findings inherited as [RESEARCH FINDING].
5. `research/pitch-lab-build-and-ci-environment.md` — canonical build/CI environment (companion document, same cycle).

**Reading order for a new implementation agent:** this document §0–§2 → architecture v1.1 (full) → this document §3–§15 → build & CI environment spec → worklog current-state block.

**Classification tags** (task mandate; do not collapse):

| Tag | Meaning |
|---|---|
| `DEFINED` | Fully specified here + architecture; implementer may proceed |
| `PARTIALLY DEFINED` | Semantics fixed, some parameter values provisional |
| `CONTRADICTORY` | Conflict between sources found — resolution recorded in §2 |
| `MISSING` | Not specified anywhere — flagged, never silently invented |
| `IMPLEMENTATION DETAIL` | Deliberately left to the implementer within stated constraints |
| `OPEN DECISION` | Owner must decide (§16); no silent default |
| `EXTERNAL DEPENDENCY` | Third-party code/tool required (§17 of the build/CI spec) |
| `ENVIRONMENT CONSTRAINT` | Canonical environment fact (build/CI spec) |
| `REQUIRES EMPIRICAL VALIDATION` | Tolerance/threshold value must be calibrated before it is authoritative |

Auxiliary qualifiers: `KNOWN` (verified fact), `INFERRED` (engineering inference), `RECOMMENDED` (advice, not decision).

**Conventions:** British English in prose; exact repository identifiers (directory names such as `artifacts/`, engine ids such as `native.pv.classic`) keep their authoritative spelling. "Frame" always means one sample across all channels at one time index.

---

## 0. Purpose and scope of this document

The purpose of the implementation freeze is that **v0.1 implementation can begin without inventing hidden design decisions**. This document therefore:

1. reconciles the actual repository with the v1.1 architecture (§2) — every disagreement is recorded with the resolution and its authority;
2. freezes the v0.1 C++ contracts (§4) — signatures that architecture v1.1 deliberately left open ("semantics binding, signatures not frozen") are *proposed and frozen here* for v0.1, each tagged with ownership and lifecycle;
3. specifies the five v0.1 engines at implementation level (§6);
4. specifies shared DSP primitives (resampling §7), I/O (§9), analysis (§10), corpus (§11) and the verification matrix (§13).

It does **not** implement DSP. Non-goals (architecture §0.2) and the do-not-implement list (§0.5) remain binding.

## 1. Scope and locked constraints

### 1.1 v0.1 scope (inherited unchanged from architecture §0.5) — `DEFINED`

Shared band-limited resampling primitives; `native.varispeed`, `native.vardelay`, `native.pv.classic`, `native.pv.phaselocked`, `native.granular`; the harness (ExperimentCompiler, OfflineRenderer, Analyzer, Reporter, CLI `pitchlab`, in-code engine registry, curve library, C++ metric modules required for §L contract tests and §G suite execution); the deterministic synthetic corpus generator (task §11; specified §11 here). Nothing else.

### 1.2 Boundary: Agent/Web Workbench vs Pitch Lab DSP system — `DEFINED`

The repository hosts two systems with a hard boundary:

| | Agent/Web Workbench | Pitch Lab DSP research system |
|---|---|---|
| Location | repo root (`src/`, `prisma/`, `db/`, `scripts/push-to-github.sh`, `Caddyfile`, `package.json`) | `pitch-lab/` subtree |
| Nature | Next.js observability workbench for the owner (docs browser, artifacts browser, GitHub sync button) | C++20 offline DSP laboratory (the architecture's four components) |
| Runtime relationship | **None.** The workbench never links, calls or configures DSP code; it only *observes* files (reads `research/` and `pitch-lab/artifacts/`) | The DSP system never depends on the workbench; deleting the workbench must not change DSP behaviour |
| State relationship | Workbench state (`.sync-state.json`, `db/`) is never input to DSP | `pitch-lab/artifacts/` is a generated tree, never source of truth |

The workbench stays as-is (owner mandate; architecture has no workbench concept — recorded as reconciliation R-3 in §2.3).

### 1.3 Owner-locked constraints (2026-09-25 mandate) — `DEFINED` (locked)

| # | Constraint | Effect in this specification |
|---|---|---|
| L-1 | **C++20 only; no Python runtime/build dependency; one-off research scripts are not part of the v0.1 runtime/build contract** | `src/analysis-py/` is NOT created in v0.1. Resolves architecture open decision §N.1 (pure C++; the designed-in C++ metric path is the v0.1 path). Architecture assumption A5 is void for v0.1. Python-based trackers (librosa/pYIN) cannot be the v0.1 reference tracker → §16 OD-12 |
| L-2 | **TOML for human-authored configuration; JSON only for generated machine-readable manifests; engine identity never in configuration** | §15; architecture §N.2 resolved; §D.6 unchanged |
| L-3 | **Harness block size default 4096** (configurable harness parameter) | `config/harness.toml` `block_frames = 4096`; architecture §N.3 resolved |
| L-4 | **Research/master output float64; listening/export copies float32; do not unnecessarily reduce internal precision** | Amendment A-1 (§2.4): the v0.1 engine I/O bus is planar `double`; WAV masters are float64, listening exports float32. Architecture §N.4 resolved (as its own provisional recommendation anticipated); §D.1 float32-bus decision superseded for v0.1 — full conflict record in §2.2/§2.4 |
| L-5 | **Block-boundary validation criterion: −80 dBFS audio-equivalent (provisional); must not silently become a bit-exact requirement** | §13 contract test T-B6; tolerance value `REQUIRES EMPIRICAL VALIDATION`; architecture §N.5 resolved-in-kind (audio-equivalent chosen, value provisional) |
| L-6 | **Rate-following output-length tolerance must NOT be invented; semantics first; a dedicated calibration test establishes the tolerance** | §4.3.4 defines the semantics + the calibration test `T-LEN-CAL`; architecture §N.6 remains open in *value*, closed in *procedure* |
| L-7 | **Reference pitch tracker intended = pYIN; investigate the C++-only route; if not justifiable, record an OPEN DECISION** | Investigated (worklog Task 10-c). No permissive, maintained, dependency-light C++ pYIN exists; four options recorded in §16 OD-12 — NOT silently resolved |
| L-8 | **Synthetic deterministic corpus first; real-world corpus is a separate later layer; no bulk downloads** | §11 (specification) and §12 (schema, design only) |
| L-9 | **Web scaffold stays; not the DSP runtime; document the boundary; no GUI work in Pitch Lab v0.1** | §1.2; architecture §0.2 unchanged |

### 1.4 Canonical environment summary — `ENVIRONMENT CONSTRAINT`

Canonical build/test target = GitHub Actions, pinned `ubuntu-24.04` x64, GCC 13.2.0, C++20, CMake 3.31.6 (checksum-pinned tarball), Ninja, CTest. Full specification, justification and exact commands: `research/pitch-lab-build-and-ci-environment.md`. Local builds are permitted but never authoritative; a clean checkout must configure/build/test with zero ambient dependencies (verified by CI, which at freeze state vendors **no** third-party code).

---

## 2. Repository reconciliation (architecture v1.1 ↔ actual repository)

### 2.1 Method

The actual repository was audited on 2026-09-25 (git log/status, filesystem walk, workflow/scripts inspection — worklog Task 10). The architecture tree (§B.3) assumes a standalone project root `pitch-lab/`; the actual repository is a monorepo containing the agent workbench. The mapping below records every disagreement and the action taken **this cycle**.

### 2.2 Reconciliation map

| Area | Architecture requires (v1.1) | Actual repository (2026-09-25) | Action required / taken this cycle |
|---|---|---|---|
| C++ project root | §B.3 "proposed root: `pitch-lab/`" | Did not exist | **Created `pitch-lab/`** with the §B.3 subtree (R-1) |
| Authoritative docs | `pitch-lab/docs/` holds design doc + research links | Docs live in repo-root `research/` (referenced by workbench, README, worklog) | **Kept `research/` as the single authoritative doc tree**; `pitch-lab/docs/` NOT created (duplicate source of truth would violate Operating Principles §2/§80); `pitch-lab/README.md` points to `research/` (R-2) |
| Engine source tree | `pitch-lab/src/core`, `src/engines/native`, `src/analysis` | Did not exist | Created `src/core/` + `src/cli/` with freeze-state infrastructure only (version + registry types; no engines). `src/engines/`, `src/analysis/` are implementation-phase directories (created when first used) |
| Python analysis source | `src/analysis-py/` OPTIONAL (dev-only) | Absent | **Not created in v0.1** (owner lock L-1; §N.1 resolved) |
| Generated tree | `pitch-lab/artifacts/{renders,analysis,reports}` GENERATED, **gitignored**, reconstructable | Repo-root `artifacts/` was tracked (Task 9, `.gitkeep` only) — CONTRADICTORY with §B.3's gitignore policy | **Resolution R-4:** root `artifacts/` removed from version control; generated tree = `pitch-lab/artifacts/` (gitignored); workbench artifacts API re-pointed; publication of generated artifacts to GitHub is now OD-15 (§16) instead of implicit behaviour |
| DSP scripts | `pitch-lab/scripts/` (bootstrap, build, run-suite, verify-reproducibility) | Root `scripts/` holds workbench sync script only | `pitch-lab/scripts/` created (empty at freeze; bootstrap is implementation-phase — §J bootstrap contents appear when there is something to vendor). Root `scripts/` = workbench tooling, boundary documented in §1.2 (R-3) |
| DSP tests | `pitch-lab/tests/` | Root `tests/` contains scaffold shell scripts (unrelated to DSP) | `pitch-lab/tests/` created with the freeze-state smoke tests; root `tests/` is scaffold environment, documented, left untouched (R-3) |
| Experiments/curves/assets trees | `experiments/{curves,suites,creative}`, `assets/{corpus,reference-notes}` | Did not exist | Created (`.gitkeep`), `config/` seeded with `harness.toml` + `metrics.toml` + `tolerances.toml` carrying the locked/provisional values (§15) |
| External vendoring | `external/` per-library + `LICENSE.txt` + `ORIGIN.toml` | Absent | `external/` created with policy README only; **nothing vendored at freeze** (pocketfft + doctest are implementation-phase vendoring, dependency-audited in the build/CI spec §6) |
| Engine registry | §D.6 one in-code registration point | Did not exist | `pitch-lab/src/core/engine_registry.{h,cpp}` created: registration mechanism + **empty production list at freeze** (R-5; §14) |
| CI / build | Architecture §J: CMake+Ninja dev-only; no canonical CI defined | No `.github/`, no CMake project | Created `.github/workflows/ci.yml` + `pitch-lab/CMakeLists.txt` (task §16; environment spec) |
| Worklog | §0/§118: current-state block at top | Chronological entries only, no current-state header | **Added current-state block at the top of `worklog.md`** this cycle (R-6) |

### 2.3 Conflicts found (surfaced, never silently merged)

| # | Conflict | Authority analysis | Resolution |
|---|---|---|---|
| C-1 | **Bus precision.** Architecture §D.1 [RESOLVED]: float32 planar I/O bus (rejected alternative: double bus, "diverges from future VST reality"). Owner mandate L-4: float64 research/master output, "do not unnecessarily reduce internal precision". A float32 bus would quantise every master render before the float64 WAV file — making L-4's master precision decorative | Owner mandate is the newer, explicit decision and overrides a project-level [RESOLVED DESIGN] (Operating Principles §62: protected constraints change by explicit owner decision; this is explicit). The architecture's VST-reality concern is preserved by the donation path (§M: VST re-vendors code; f64→f32 conversion is the adapter's job in the future VST project, not the Lab's) | **Amendment A-1** (§2.4): v0.1 bus = planar `double`; listening copies convert to float32 at export. Architecture v1.1 text left unchanged as historical evidence; a v1.2 amendment is queued (§16 OD-16). NOT silent: recorded here, in the worklog, and in the final report |
| C-2 | **Generated artifacts on GitHub.** Task 9 (previous cycle) tracked an `artifacts/` tree for GitHub publication. Architecture §B.3: generated trees are gitignored + reconstructable via `pitchlab verify` | Architecture is the governing source for repository structure (task §1 authority order) | Architecture wins: `pitch-lab/artifacts/` gitignored. The owner's Task-9 intent ("every artifact goes up") is preserved as **OD-15** (owner decides whether selected generated artifacts, e.g. reports, are published as release evidence) — not silently dropped, not silently continued |
| C-3 | **Docs location.** Architecture §B.3 `pitch-lab/docs/` vs actual repo `research/` | Operating Principles §2/§80 (one canonical location) | `research/` is the single authoritative location; `pitch-lab/docs/` not created (R-2) |

Design preferences NOT treated as defects (Operating Principles §90): the monorepo layout itself (workbench + DSP in one repository) — an organisational choice of the owner, not an architecture violation; the architecture never mandated repository exclusivity.

### 2.4 Recorded amendments to architecture v1.1 (v0.1 binding, architecture text unchanged)

| Amendment | Changes | Rationale | Reversible? |
|---|---|---|---|
| **A-1** | Engine I/O bus: `float` (32-bit) → planar **`double`** (§4.1). WAV masters float64; listening exports float32 (conversion at export boundary only) | Owner mandate L-4; removes the only precision bottleneck of the master path; VST-donation unaffected (adapter-side conversion, §M) | Yes — owner may revert; queued as OD-16/architecture v1.2 |

All other architecture semantics are inherited unchanged. Where this specification adds signatures the architecture left open, they are tagged `[v0.1 CONTRACT — proposed]` and are binding for v0.1 implementation (the architecture's "signatures not frozen" stance is satisfied: the *architecture* did not freeze them; the *implementation specification* now does, which is its purpose).

---

## 3. Project structure (authoritative layout) — `DEFINED`

```text
<pitch-lab/ >                      # C++20 DSP research system root (architecture §B.3)
  CMakeLists.txt                  # project(); C++20; warnings; CTest enable
  README.md                        # boundary + pointer to research/ docs (R-2)
  config/                          # AUTHORITATIVE: harness defaults, metric tolerances,
    harness.toml                   #   block size (L-3), render/export policy
    metrics.toml                   #   metric alignment parameters (provisional)
    tolerances.toml                #   ALL provisional numeric tolerances (L-5, §N.9)
  experiments/                     # AUTHORITATIVE
    curves/                        #   pitch curve specs (TOML) [+ optional CSV data]
    suites/                        #   benchmark suites (§G.3 battery as files)
    creative/                      #   creative experiments (namespaced)
  assets/
    corpus/                        # AUTHORITATIVE: inputs + metadata.toml (synthetic
                                   #   corpus generated once, then committed — §11)
    reference-notes/reference-model.md   # what counts as reference and why (§10.3)
  src/
    core/                          # contract, registry, curve lib, renderer, compiler,
                                   #   harness, CLI, shared DSP primitives (resampler)
    engines/native/                # one directory per engine (implementation phase)
    analysis/                      # C++ metric modules (implementation phase)
  external/                        # vendored third-party (LICENSE.txt + ORIGIN.toml
                                   #   per library) — EMPTY at freeze, §17 build spec
  scripts/                         # bootstrap/build/run-suite/verify (implementation phase)
  tests/                           # C++ test sources (freeze: smoke/infrastructure only)
  tools/                           # stand-alone tools (corpus_gen) — NOT components
  artifacts/                       # GENERATED (gitignored): renders/ analysis/ reports/
```

Repository-root trees and their roles: `research/` (authoritative documents), `src/ prisma/ db/ …` (workbench, §1.2), `scripts/` (workbench sync), `tests/` (scaffold environment scripts — unrelated to DSP), `mini-services/`, `examples/`, `download/`, `upload/` (sandbox scaffolding). `artifacts/` at repo root is **removed** (R-4).

Generated-tree rule (architecture §B.2): deleting `pitch-lab/artifacts/` and re-running `pitchlab render/analyze/report` must reconstruct it bit-for-bit for deterministic engines; generated files are never build inputs and never source of truth.

---

## 4. Core C++ contracts (v0.1) — `DEFINED` unless tagged otherwise

Conventions: namespace `pitchlab`; headers `snake_case.h`; all interfaces are internal-only (no installed headers in v0.1); every contract below states **ownership** and **lifecycle**. All durations/latencies are in frames. All sample-rate values in Hz as `double` where computed, `int32`/`uint32` in serialised forms.

### 4.1 Primitives

```cpp
// src/core/types.h  [v0.1 CONTRACT — proposed]
namespace pitchlab {

using FrameCount  = int64_t;   // one frame = one sample index across all channels
using ChannelCount = int32_t;
using SampleRate  = double;

// Planar audio block view (Amendment A-1: double bus). NON-OWNING.
struct AudioBlockView {
    const double* const* channels;  // channel-major, non-interleaved; channels[c][f]
    ChannelCount channelCount;
    FrameCount    frameCount;       // frames in THIS block
};

// Mutable planar output region. NON-OWNING.
struct AudioBlockOut {
    double* const* channels;
    ChannelCount channelCount;
    FrameCount    frameCapacity;    // capacity; produced frames may be fewer
};

enum class ChannelLayout { Mono, Stereo };     // v0.1 corpus/harness surface
// (ChannelMode in capabilities remains the architecture's richer declaration set)
}
```

- **Ownership** `DEFINED`: the *renderer* owns all buffers (§5). Views never outlive the `process()` call that received them.
- **Sample format** `DEFINED`: in-memory planar `double` (A-1). File formats: §9. Internal engine compute: `double` by default (owner L-4); engines may use `float` internally only as a documented implementation decision recorded in the engine sheet (none of the v0.1 engines plan to).
- **Sample rates** `DEFINED`: supported set {44100, 48000, 88200, 96000, 176400, 192000}; the value is carried per job; no component assumes 44.1/48 (§8).

### 4.2 PitchEngine contract

```cpp
// src/core/pitch_engine.h  [v0.1 CONTRACT — proposed; semantics = architecture §D/§D.4]
namespace pitchlab {

enum class UsageClass { Prototype, BenchmarkOnly, ExternalIntegration,
                        ExternalBenchmark, ResearchReference, Rejected, Future };
enum class DurationBehaviour { Preserving, RateFollowing };
enum class ChannelMode { Mono, Stereo, MonoAndStereo, MultiChannel };
enum class Determinism  { Deterministic, SeededDeterministic };

struct ControlRateSpec {
    enum class Kind { PerSample, FixedBlock, EngineEvent } kind;
    int blockFrames = 0;      // FixedBlock: engine-declared, harness-verified
};

struct BandwidthSpec {
    double nyquistFraction;   // declared honest bandwidth (0 < f <= 1.0)
    const char* notes;        // free-form honesty notes (e.g. aliasing regime)
};

struct LatencyInfo {
    FrameCount inputLatencyFrames;   // lookahead the engine needs (renderer pads, §4.3.6)
    FrameCount outputLatencyFrames;  // bounded flush length (D.4.1 item 4)
};

struct EngineInfo {
    const char* id;            // stable registry id, e.g. "native.varispeed"
    const char* displayName;
    const char* version;       // engine's own version string
    UsageClass   usageClass;
    const char* origin;        // "own implementation"
    const char* license;       // "own code, no dependency"
};

struct Capabilities {
    double minRatio, maxRatio;          // no global harness ceiling
    bool   supportsDynamicRatio;
    ControlRateSpec controlRate;
    ChannelMode channelMode;
    int     maxChannels;
    BandwidthSpec bandwidth;
    DurationBehaviour duration;
    Determinism determinism;
};

struct EngineConfiguration {
    // typed parameter bag; TOML table → engine-defined keys.
    // Harness validates ONLY: seed present, all values finite.
    // Serialised verbatim (canonical JSON, sorted keys) into the manifest.
    std::vector<std::pair<std::string, ParameterValue>> parameters; // string|double|bool|int64
    uint64_t seed;   // REQUIRED
};

struct ProcessContext {
    SampleRate   sampleRate;
    ChannelCount channels;
    int          maxBlockFrames;   // harness block size (config/harness.toml, L-3: 4096)
    FrameCount   totalInputFrames; // known offline; INPUT frames
};

struct PitchCurveView {
    const double* ratio;      // dense, INPUT-timeline indexed (architecture §D.4.2)
    FrameCount    frames;     // == total input frames
    SampleRate    sampleRate; // audio sample rate (curve indexed per input frame)
    // EngineEvent engines sample arbitrary indices via sampleRatioAt(int64_t inputFrame):
    // linear interpolation inside [0, frames-1]; out-of-range → clamped + flagged
    // via CurveAccessReport (§4.4.5). No engine extrapolates.
};

struct ProcessReport {        // per-call block-exchange accounting (§D.4.1 items 1-3)
    FrameCount inputFramesConsumed;
    FrameCount outputFramesProduced;
    bool       inputExhausted;    // engine signals: input stream fully consumed
};

class PitchEngine {
public:
    virtual ~PitchEngine() = default;
    virtual EngineInfo     info() const = 0;
    virtual Capabilities   capabilities() const = 0;
    virtual LatencyInfo    latency() const = 0;          // valid after prepare(); constant per job
    virtual void configure(const EngineConfiguration& cfg) = 0;  // params + seed; before prepare
    virtual void prepare(const ProcessContext& ctx) = 0;        // allocate + reset state
    // Block exchange (§D.4). Renderer supplies up to inFrames input frames at
    // inputFrameIndex (absolute input-timeline position of in[0]) and output capacity
    // outCapacity. The engine consumes/produces what it can; returns the report.
    virtual ProcessReport process(const AudioBlockView& in, int inFrames,
                                  AudioBlockOut& out, int outCapacity,
                                  const PitchCurveView& curve,
                                  FrameCount inputFrameIndex) = 0;
    // End-of-input + bounded flush (§D.4.1 items 3-4). Called exactly once, after the
    // renderer has delivered all real input. Emits up to outCapacity frames; returns
    // the report. Total flush output MUST be <= latency().outputLatencyFrames.
    virtual ProcessReport finish(AudioBlockOut& out, int outCapacity) = 0;
    virtual void reset() = 0;   // clears DSP state, keeps configuration
};
}
```

**`process()` preconditions** `DEFINED`: called with arbitrary `inFrames ≤ maxBlockFrames`, strictly increasing `inputFrameIndex`, exactly once per input frame range, in order. `outCapacity ≥ 0`. The engine must tolerate any block boundary (block-streaming mandate). Engines must not allocate in `process()`/`finish()` (§5 allocation rules). Exceptions: only `EngineException` (derived `std::runtime_error`) — any other exception escaping an engine is a JOB FAILURE (`engine-initialisation-or-processing-failure`, §13 failure fixtures).

**Symmetric form**: a `Preserving` engine at steady state returns `{inFrames, outFrames == inFrames, false}` with `outFrames ≤ inFrames` (it may buffer, e.g. windowed STFT engines produce nothing until a hop completes). `RateFollowing` engines return independent consumed/produced counts (varispeed steady state: consumed ≈ produced·ratio).

### 4.3 Frame accounting (architecture §D.4 made executable) — `DEFINED`

#### 4.3.1 Timelines

- **Input timeline**: input frame indices `0 … N_in−1`. The curve is indexed here.
- **Output timeline**: output frame indices `0 … N_out−1`; the cumulative `outputFramesProduced` counter *is* the output-timeline progress (used for NaN-scan positions, taint positions, reporting — architecture §D.4.1 item 2).
- A `Preserving` engine maps output frame `k` to input frame `k` (after declared-latency alignment in analysis).
- A `RateFollowing` engine drives a read position `r(t)`; the emission map is deterministically derivable from the input-indexed curve + declared latency (architecture §D.4.1 item 5). The harness reconstructs the *expected* map from the curve hash + lengths + latency recorded in the manifest; it never asks the engine for a per-frame map.

#### 4.3.2 Renderer loop (normative pseudocode)

```text
consumedTotal = 0; producedTotal = 0
engine.prepare(ctx)                       // fs, channels, maxBlock, N_in
lat = engine.latency()                    // recorded in manifest
pad input stream with lat.inputLatencyFrames zero frames   // §4.3.6: padding is
                                                          // renderer machinery; NOT
                                                          // counted as consumed input
while consumedTotal < N_in:
    in = next block (up to maxBlockFrames) at inputFrameIndex = consumedTotal
    rep = engine.process(in, inFrames, out, outCapacity, curve, consumedTotal)
    consumedTotal += rep.inputFramesConsumed        // MUST NOT exceed real N_in
    producedTotal += rep.outputFramesProduced
    emit out[0 .. rep.outputFramesProduced)
    if rep.outputFramesProduced == 0 and rep.inputFramesConsumed == 0 and
       consumedTotal < N_in:  → RENDER STALL failure (§K-class)   // deadlock guard
rep = engine.finish(out, outCapacity)
producedTotal += rep.outputFramesProduced
emit; check flush bound: rep.outputFramesProduced <= lat.outputLatencyFrames
                                            else → end-of-render violation (JOB FAILURE)
// length policy check (§4.3.3/4.3.4); NaN/Inf scan; manifest emission
```

The renderer never truncates or zero-pads output to force a length (architecture §D.4.3).

#### 4.3.3 `Preserving` length policy — `DEFINED`

Canonical total output length = `N_in + lat.outputLatencyFrames`. Actual ≠ canonical ⇒ taint `length-policy` (render completes; analysis aligns what it can). End-of-render = input exhausted + flush complete.

#### 4.3.4 `RateFollowing` length policy — `PARTIALLY DEFINED` (value open by owner mandate L-6)

Expected output length: `m` solving `∫₀^m ratio(r(τ)) dτ = N_in` (input-indexed curve; read advances `ratio[r]` input frames per output frame), **plus** `lat.outputLatencyFrames` flush tail. Constant-ratio case: `m = N_in / ratio` (worked example, architecture §D.4.4: 1000 frames @ 2.0× ⇒ 500 + L).

- The renderer computes the expected length by numerically integrating the curve with the same deterministic quadrature the reference engine uses (left-edge rectangular rule over output frames — the exact rule is part of the varispeed engine spec §6.1, so expectation and reference cannot diverge by construction; a *different* engine with a different internal quadrature is a measured property, not a policy violation).
- Actual vs expected is checked against a tolerance: **provisional ±1 harness block (4096 frames) — `REQUIRES EMPIRICAL VALIDATION`**; the dedicated calibration test **T-LEN-CAL** (§13) measures the actual |error| distribution of the varispeed reference across the full curve battery × sample-rate set and reports the observed maximum; the owner ratifies the authoritative tolerance from that evidence (OD-6 remains open in value). Mismatch ⇒ taint `length-policy`.

#### 4.3.5 End-of-input and flush — `DEFINED`

The engine is informed exactly once that input is complete (the `finish()` call). Flush is bounded by `lat.outputLatencyFrames` (reported after `prepare()`, constant per job — engines with ratio-dependent flush bounds compute the bound from the curve within `prepare()`). Violations: producing past the bound ⇒ JOB FAILURE `end-of-render-violation`; never signalling input exhaustion while the renderer has real input left ⇒ RENDER STALL/deadlock guard failure; a `Preserving` engine that cannot fill its canonical length ⇒ taint `length-policy` (architecture §K row "engine fails to reach end-of-render" refined here into the three cases).

#### 4.3.6 Input lookahead padding — `DEFINED`

If `lat.inputLatencyFrames > 0`, the renderer appends that many zero frames to the input stream as *renderer machinery*. These frames are **not** counted in `inputFramesConsumed` (consumed accounting must reflect real input only) and they are recorded in the manifest as `inputPaddingFrames`. The engine must consume them (its reports may count them as consumed only if it reports `inputExhausted` at the correct boundary — engines report consumption of padding frames; the *renderer* corrects the accounting by subtracting padding when comparing against `N_in`. Rule: the engine's cumulative consumed count may legitimately reach `N_in + inputPaddingFrames`; the renderer's guard is `consumedTotal ≤ N_in + padding`).

### 4.4 Pitch curve model — `DEFINED` (semantics from architecture §E)

#### 4.4.1 Canonical representation

Linear pitch ratio, `double`, input-timeline indexed, one value per input audio frame. `ratio = 1.0` identity; 0.5 = −1 octave; 2.0 = +1 octave; any positive finite ratio representable (no global range). Semitones exist only at the authoring surface: `ratio = 2^(semitones/12)` computed in `double` at compile (curve-compilation) time.

#### 4.4.2 Two layers

- **`PitchCurveSpec`** (authoritative TOML, `experiments/curves/*.toml`) — schema §15.3.
- **`PitchCurveSignal`** (derived, dense `std::vector<double>`, one entry per input frame at the job's fs) — compiled deterministically by the CurveLibrary from the spec + job (sample rate, total input frames). The dense signal is the single source of pitch truth for a job; it is never persisted as truth (hash recorded in the manifest).

#### 4.4.3 Compilation rules — `DEFINED`

- Static kind: constant ratio over `[0, N_in)`.
- Ramp kinds (`ramp_lin`, `ramp_exp`): time-parameterised; exponential ramps use ratio-domain geometric interpolation (per-second semitone rate is converted to a per-frame ratio growth factor once, then applied multiplicatively — `IMPLEMENTATION DETAIL` with the exact formula fixed in the curve-compiler unit tests to prevent divergence).
- LFO kinds: sine/triangle/saw/random-walk at declared rate/depth/centre/bounds; random-walk and any stochastic element REQUIRE `seed`; the RNG is the harness PCG64 (§4.7), reseeded per compilation.
- Reversal kind: piecewise-linear between the two targets within the declared window; a *derivative* discontinuity is always allowed.
- Breakpoints: strictly monotone times; per-segment interpolation law (`lin`|`exp`); value discontinuities require `allow_discontinuity = true` in the spec (§E.4).
- External kind: dense ratio at a declared rate, or `(time, ratio)` pairs from a CSV under `experiments/curves/`; the compiler resamples (linear) to the job's frame grid deterministically; `sourceDescription` recorded in the manifest.

#### 4.4.4 Validation (compile-time hard errors — CONFIG ERROR, architecture §E.4/§K)

ratios finite and strictly positive; times finite and strictly monotone; coverage of `[0, totalFrames)` with hold-first/hold-last extension policy (default `hold-last`, declared in spec); value discontinuities flagged; empty curve ⇒ CONFIG ERROR.

#### 4.4.5 Curve access reporting

`EngineEvent` engines sample `sampleRatioAt(i)`; out-of-range indices are clamped and recorded in a per-job `CurveAccessReport` {minIndexRequested, maxIndexRequested, clampCount} serialised into the manifest (information preservation, Operating Principles §79).

#### 4.4.6 Requested vs effective (creative mode) — `DEFINED`

The authored spec/signal is the **requested** curve and is never mutated. In creative mode, out-of-range jobs are rendered against the harness-derived **effective** curve: the ExperimentCompiler saturates the dense requested signal to the engine's declared `[minRatio, maxRatio]` (element-wise: `clamp(v, min, max)`), deterministically, in `double`. Manifest records: requested spec id + requested signal hash, effective signal hash + derivation note ("saturated to [min,max] of engine `<id>`"), requested/effective extrema, execution status (status vocabulary is an implementation decision fixed here: `RANGE_SATURATED`; in-range creative jobs: `IN_RANGE`; benchmark jobs carry no saturation status). Benchmark mode: out-of-range ⇒ JOB SKIP `ratio-out-of-range`, never rendered, never saturated (architecture §F.4/G.2).

### 4.5 Engine configuration — `DEFINED`

`EngineConfiguration` = typed parameter bag + REQUIRED `seed` (uint64). Parameter keys/semantics are engine-defined (per engine sheet §6); the harness validates only: seed present; all numeric values finite; unknown keys ⇒ CONFIG ERROR (typo protection — an engine-declared key set is exposed by the registry descriptor, §14, so the compiler can validate names without engines knowing experiment semantics). Serialisation into manifests: canonical JSON with sorted keys (deterministic).

### 4.6 Engine registry — `DEFINED` (mechanism; contents §14)

- **One authoritative registration point**: the file `src/core/engine_registry.cpp` is the only translation unit that registers production engines, via the single function `pitchlab::registerProductionEngines(EngineRegistry&)`, called explicitly from `main()`/harness entry (deliberately NOT static initialisation — deterministic ordering, no static-init-order hazard). This is the architecture §D.6 "in-code compile-time registration point".
- `EngineRegistry` (in-memory, immutable after seal): `registerEngine(EngineDescriptor)` (append; duplicate id ⇒ logic error), `seal()` (freezes; further registration ⇒ logic error), `count()`, `byIndex(i)` (deterministic registration order), `findById(id)` → optional.
- `EngineDescriptor` (freeze-state subset; factories are added when engines exist): `{EngineInfo info; Capabilities capabilities; bool isReferenceRole; const char* parameterKeys[];}` — plus, from implementation phase, the construction binding (factory).
- Registry content is mirrored read-only into every manifest (provenance); manifests never become authority. No second registry exists anywhere (no config, no script, no document list is authoritative — this document's §14 table is the *specification* of what the registry must contain, not a second registry).
- Test-only dummy engines may be registered in test binaries via the public API, flagged `test-only` in `EngineInfo::origin` (architecture §L "Reference comparison" layer).

### 4.7 Deterministic RNG — `DEFINED`

Harness-provided PCG64 (own implementation, no `<random>` device entropy, architecture §O.12). One stream per (job, consumer): the seed from `EngineConfiguration` is mixed with a consumer tag (e.g. `"engine"`, `"curve.random-walk"`, `"corpus"`) via a fixed 64-bit mixing function (SplitMix64) — exact mixing specified in the RNG unit test (golden values) so all consumers derive identically. Bit-exact reproducibility: same seed ⇒ identical streams.

### 4.8 Harness component interfaces (public surface) — `DEFINED` at signature level; internals `IMPLEMENTATION DETAIL`

```cpp
// ExperimentCompiler
struct CompileOutcome { RenderJobList jobs; SkipList skips; };
CompileOutcome compile(const fs::path& experimentToml);   // hard CONFIG ERRORs throw
                                                             // ConfigError{file, field, reason}

// OfflineRenderer
struct RenderSummary { /* per job: status ok|skipped|failed|incomplete, taints[], 
                          length accounting (§4.3), manifest path, output wav path */ };
RenderSummary render(const RenderJobList&);    // deterministic; block schedule from
                                               // config/harness.toml; emits WAV + manifest

// Analyzer
AnalysisResult analyse(const RenderResultRef&);   // files only; never touches engines

// Reporter
Report report(const std::vector<AnalysisResult>&); // aggregates; never mutates results
```

CLI entry points (architecture §B.1): `compile | render | analyze | report | verify | listen-index` + registry introspection `engines`. The `corpus_gen` tool is a separate executable in `tools/` (not a CLI subcommand — the corpus is *authored* input, not harness business logic; §11).

### 4.9 WAV I/O module contract — summary (full policy §9) — `DEFINED`

Own reader/writer (no libsndfile — architecture §J). Reader: RIFF/WAVE, PCM formats `{fmt 3 IEEE float 32/64-bit}` (WAVE_FORMAT_EXTENSIBLE when channels > 2 or channel-mask required), returns planar `double` buffers + metadata `{fs, channels, frames}`; rejects truncated/invalid files with CONFIG ERROR mapping. Writer: deterministic (no timestamps inside WAV), float64 masters / float32 listening copies (L-4/A-1).

### 4.10 Analysis contracts — summary (full §10) — `DEFINED` at interface level

Metric module = pure function `MetricResult compute(const RenderResultRef& output, const RenderResultRef* reference, const AnalysisContext& ctx)` with `AnalysisContext` carrying fs, channels, tolerances (from `config/tolerances.toml`), alignment data (emission-map reconstruction inputs from the manifest), expected curve data. `MetricResult` = `{metricId, value(s), status, notes}`; statuses at least `ok | not-applicable | tainted-input | tracker-unavailable` (the latter for tracker-dependent metrics while OD-12 is open).

---

## 5. Buffer ownership and lifecycle rules — `DEFINED` (no rule left implicit)

| Object | Owner | Allocation point | Lifetime | Reset semantics |
|---|---|---|---|---|
| Input file buffers (whole input signal) | OfflineRenderer, per job | job start | job end | n/a (destroyed) |
| Input block views handed to engines | Renderer (stack/loop-local) | per block | end of `process()` call | n/a |
| Input lookahead padding frames | Renderer | job start (contiguous with input) | job end | n/a |
| Output accumulation buffer | Renderer, per job | job start | job end (written to WAV) | n/a |
| Output block regions handed to engines | Renderer | per block | end of call | n/a |
| Engine internal DSP state | Engine instance | `prepare()` (all allocations) | job end (instance destroyed) | `reset()` clears state, keeps config; NOT reallocation |
| Pitch curve signal (dense) | Compiler creates → Renderer consumes | compile | job end | n/a |
| Engine instances | Renderer (via registry factory) | per job | **destroyed at job end; state never leaks across jobs** (architecture §F.1) | — |
| RNG streams | Harness | per job per consumer | job end | reseeded per job from seed |
| WAV/manifest files | Emitted; **nobody owns after emission** (immutable artifacts) | render/analyse time | until artifacts/ regenerated | regenerate = delete + re-run (bit-exact, §L) |

**Rules** `DEFINED`:

1. Engines never allocate in `process()`/`finish()` — all allocation in `prepare()`. Violation ⇒ engine defect (caught by the allocation-auditing test build, §13 T-A1: a global allocation counter installed only in test builds fails the test if `process()`/`finish()` allocate).
2. Processing is never in-place: `in` and `out` regions never alias (renderer guarantees separate buffers).
3. `configure()` before `prepare()`; `prepare()` before first `process()`; `finish()` exactly once, only after the renderer has delivered all real input; `reset()` reusable between jobs with identical `(fs, channels, maxBlock, N_in)` — the renderer still constructs a fresh instance per job (§F.1) and `reset()` exists for contract testing.
4. Error paths: engines throw only `EngineException`; the renderer catches, marks the job failed with the error string, run continues (architecture §K "engine initialisation failure" extended to processing failure).
5. Deterministic reset/reuse: after `reset()`, processing the same input with the same curve must produce bit-identical output to a fresh instance (tested, §13 T-D3).

---

## 6. Engine implementation sheets (v0.1) — `DEFINED` per sheet, caveats inline

Common to all five engines: own implementation, no dependencies; `Determinism = Deterministic` (varispeed/vardelay/pv classic+locked) or `SeededDeterministic` (granular — grain dither/jitter derives from the seed); exceptions only `EngineException`; allocation only in `prepare()`; no knowledge of test methodology, metrics, corpus semantics, file paths (architecture §D.1). "Declared capabilities" below become registry entries (§14) and are binding honesty declarations.

### 6.1 `native.varispeed` — rate-following reference engine

| Field | Specification |
|---|---|
| Purpose | Read-position-driven sample-rate conversion: `y[m] = x(r(m))`. The harness's reference engine where mathematically appropriate (architecture §H.2) and the *reference model* of the RateFollowing class. NOT a quality judgement (architecture: "reference, not best") |
| UsageClass / role | `Prototype`; registry `isReferenceRole = true` (manifests tag reference renders) |
| Processing model | Fractional read pointer advancing `ratio[r]` input frames per output frame (§D.4.2); samples reconstructed by the shared Kaiser windowed-sinc interpolator (§7) |
| I/O relationship | RateFollowing. Output length = integrated-curve expectation + flush (§4.3.4). Steady state: consumed ≈ produced·ratio |
| Pitch control | PerSample: ratio evaluated at the *read position* `r(m)` on the input-indexed curve — zipper-free by construction for continuous curves [RESEARCH FINDING B.1.1] |
| Read-position integration | Left-edge rectangular quadrature: `r(m+1) = r(m) + ratio[clamp(floor(r(m)), 0, N_in-1)]`, `r(0) = 0`. `double` accumulator. This exact rule is ALSO the harness's expectation rule (§4.3.4), fixed here so reference and policy cannot diverge |
| Latency | `inputLatencyFrames = K` (sinc half-width in input frames — the renderer's zero padding covers kernel support at the read position); `outputLatencyFrames = ceil(K / r_min)` with `r_min` = curve minimum ratio (per-job, computed in `prepare()`) — conservative bound on the flush tail [RECOMMENDED; value validated by T-LEN-CAL] |
| State | Read position `r`; input ring buffer of `2K` frames (renderer-delivered); anti-alias pre-filter state (§7.4); nothing else |
| Allocation | All in `prepare()`: ring, filter state, per-channel interpolation workspace |
| Anti-aliasing | Pre-shift low-pass per architecture §I.2: for upward shifts the input is low-passed at `Nyquist/r` **before** resampling. v0.1 policy: the pre-filter cutoff is piecewise-constant per input block, set to `min(Ny, Ny / max(1, r_max_lookahead))` where `r_max_lookahead` = curve maximum over the remaining render (computed at compile time as the whole-curve max — conservative, documented; block-boundary cutoff changes are a measured transient property). Creative aliasing: `allow_aliasing = true` engine parameter disables the pre-filter (recorded in manifest; creative mode only) |
| Block processing | Consumes what the kernel needs (up to the block offered); produces `outCapacity`-bounded output per call; tolerates any boundary |
| Flush | Emits while kernel support `r + K` overlaps real input; bounded by declared `outputLatencyFrames` |
| Curve handling | Samples `ratio` at read positions (PerSample) |
| Extreme ratios | Unbounded ratio mathematically [RESEARCH FINDING]; declared `[minRatio, maxRatio] = [0.0625, 16]` (engineering bound: interpolator/AA sanity; extrema battery 0.25×–8× comfortably inside); extreme upward shifts exit content above the audible band — metrics report band occupancy neutrally (architecture §I.2) |
| Sample rates | All six first-class rates; kernel and AA specified in seconds-relative terms (§7) |
| Stereo | Per-channel identical processing → perfect inter-channel waveform coherence (deterministic map) [RESEARCH FINDING B.1.1]; modulation rates scale with ratio (documented physics, not an artefact) |
| Determinism | Bit-identical for identical (input, config, fs, channels, block schedule); `SeededDeterministic` not needed (no stochastic element) |
| Failure conditions | `end-of-render-violation` (flush overrun); CONFIG ERROR on non-finite parameter values |
| Most relevant metrics | Realised duration ratio (defining property), pitch error (≈ 0 by construction on analytic corpus), spectral error (reference self-comparison sanity), CPU cost, HF band occupancy |
| Known research trade-offs | Formants shift 1:1 with ratio (no preservation — reference behaviour); drifts as a streaming effect (offline only — correct here); AA pre-filter is the only place the reference can deviate from "pure physics" (conservative over-filtering documented above) |

### 6.2 `native.vardelay` — variable-delay (Doppler-style) musical shifter

| Field | Specification |
|---|---|
| Purpose | `y(t) = x(t − D(t))` with instantaneous ratio `1 − D′(t)` [RESEARCH FINDING B.1.2]. In Pitch Lab it is a *musical* duration-preserving shifter (NOT the physical Doppler engine — §0.3 boundary); sustained shifts need delay wrap with crossfades |
| UsageClass | `Prototype` |
| Processing model | Circular delay buffer of depth `E` (max excursion) with fractional (sinc, §7 small-kernel) reads; delay follows the integrated ratio curve: `D′(t) = 1 − ratio(t)`, `D` integrated in `double`; when `D` reaches a bound (`0` or `E`) it wraps by `E` (or `−E`) with an equal-power crossfade of `W` frames |
| DurationBehaviour | `Preserving` (output timeline = input timeline) |
| Pitch control | PerSample (ratio is the delay derivative — best dynamic response of the preserving family [RESEARCH FINDING]) |
| Latency | `inputLatencyFrames = E + K` (buffer must fill before valid output; K = read kernel half-width); `outputLatencyFrames = W` (crossfade tail during flush). `E`/`W` are engine parameters (defaults §6.2 params table) — per-job constants, so latency is constant per job |
| Declared ratio range | `[0.5, 2.0]` sustained — the wrap cadence `|r−1|/E` keeps crossfade AM within declared bandwidth notes at defaults; beyond this the engine still *functions* but the registry declares the honest musical range (creative mode may exceed with recorded saturation) |
| State | Delay buffer (`E + K` frames), write index, current delay `D`, crossfade state, per-channel |
| Allocation | `prepare()` only |
| Block/flush | Preserving symmetric exchange (may buffer); flush emits the crossfade tail ≤ `W` |
| Curve handling | PerSample via delay derivative |
| Extreme ratios | Declared range above; extremes ⇒ benchmark skip / creative saturation (never silent clamp) |
| Sample rates | All six; `E`, `W` specified in seconds (converted at `prepare()`) |
| Stereo | Per-channel identical processing → coherent; documented wrap-AM applies identically |
| Determinism | Deterministic (no stochastic elements; crossfade schedule is curve-derived) |
| Failure conditions | end-of-render violation; length-policy taint if canonical length not met |
| Metrics | Pitch error/lag (control-rate honesty is near-per-sample), AM at crossfade rate (the characteristic artefact), realised duration (== input), stereo coherence, transient preservation |
| Research trade-offs | Wrap-splice AM (characteristic flutter, rate `|r−1|/W`); formants shift with ratio (physically consistent, musically imperfect); excursion limit vs sustained shift; HF loss avoided by sinc reads |

**v0.1 parameter table (defaults; tunable via `config/` + per-experiment overrides):** `excursion_seconds = 0.5`, `crossfade_frames = 2048` (rate-converted at prepare), `read_kernel = small-sinc (K = 8, β = 7.9)` [RECOMMENDED — values are engine parameters, not architecture].

### 6.3 `native.pv.classic` — classic phase vocoder (benchmark baseline)

| Field | Specification |
|---|---|
| Purpose | STFT analysis; per-bin phase propagation with instantaneous-frequency estimation; pitch shift = time-stretch + resample (order: resample-then-stretch for upward shifts is cheaper [RESEARCH FINDING B.1.7]); the mandatory spectral baseline [RESEARCH FINDING] |
| UsageClass | `Prototype` (benchmark baseline role — registry note) |
| Processing model | Analysis STFT (Hann, N/H per defaults), phase propagation per bin: `Δφ_k(n) = princarg[φ_k(n) − φ_k(n−1) − 2πkH/N]`; `ω̂_k(n) = 2πk/N + Δφ_k(n)/H`; synthesis phases advance by the stretched hop; output = overlap-added stretched signal, then resampled by the ratio through the shared resampler (§7) |
| DurationBehaviour | `Preserving` |
| Pitch control | `FixedBlock` (hop-rate) — `ControlRateSpec{FixedBlock, blockFrames = H}`; supportsDynamicRatio = true (ratio applied per hop; coherence degrades on fast curves — declared and measured) |
| Latency | `inputLatencyFrames = N + H` (analysis window + hop alignment), `outputLatencyFrames = N` (synthesis tail) at prepare-time scale |
| Declared ratio range | `[0.25, 4.0]` (engineering declaration; outside ⇒ skip/saturate) |
| State | Analysis/synthesis phase arrays, overlap buffers, resampler state, STFT frame FIFOs |
| Allocation | `prepare()`; FFT workspaces via pocketfft plan (§17 build spec) |
| Block/flush | Buffers internally (produces nothing until first full hop); flush emits remaining overlap-added tail ≤ `N` |
| Curve handling | Samples ratio at hop boundaries (the harness logs declared cadence; the analyzer measures effective control rate independently — §10) |
| Stereo | Per-channel independent phase propagation → possible decorrelation (declared; measured by stereo-coherence metric) [RESEARCH FINDING/INFERENCE] |
| Determinism | Deterministic |
| Bandwidth | `nyquistFraction = 0.45` with notes: "resampler stopband above 0.45·Ny; phasiness is the family artefact" |
| Failure conditions | length-policy taint; end-of-render violation; CONFIG ERROR on invalid FFT/window params |
| Metrics | Pitch error (hop quantisation visible on ramps), warble/instability (family artefact), spectral error vs varispeed reference, transient preservation (family weakness), CPU cost |
| Research trade-offs | Phasiness (vertical phase coherence loss — [VERIFIED-PAPER Průša & Holighaus 2022]); transient smearing; hop-rate control limit ("only correct for slowly varying sinusoids" — DAFx-12 [VERIFIED-PAPER]); formants shift ("Mickey Mouse") |

**v0.1 parameter defaults:** `window = hann`, `fft_size = 2048`, `hop = 512` (75% overlap — L-D'99 fractional-shift guidance [RESEARCH FINDING]) [RECOMMENDED; engine parameters].

### 6.4 `native.pv.phaselocked` — phase-locked PV (Laroche–Dolson '99 techniques)

| Field | Specification |
|---|---|
| Purpose | The spectral lane's dynamic-friendly engine: per-frame phase locking (identity variant) + peak-region frequency shifting with cumulated phase rotation `Z` that **explicitly supports per-frame varying shift** [RESEARCH FINDING B.1.8 — L-D '99 paper states the notation ω_{u+1} allows frame-varying shift] |
| UsageClass | `Prototype` |
| Processing model | Analysis STFT → spectral peak detection (magnitude greater than both neighbours each side) → region of influence assignment (midpoints between peaks / lowest-magnitude bin) → per-region synthesis phase: identity locking (`φ′_l = φ′_peak` for bins `l` in the peak's region) + peak shifted by the per-frame frequency delta with the cumulated rotation `Z_{u+1} = Z_u · e^{j·Δω_{u+1}·R}`; resynthesis by overlap-add; duration preserving by construction (frequency-domain shift, no stretch+resample needed) |
| DurationBehaviour | `Preserving` |
| Pitch control | `FixedBlock` (per frame/hop), `supportsDynamicRatio = true` (the L-D '99 designed mode) |
| Latency | As pv.classic (`N + H` / `N`) |
| Declared ratio range | `[0.25, 4.0]` |
| State | As pv.classic + peak/region maps + `Z` accumulator per peak/region |
| Allocation | `prepare()` only |
| Block/flush | As pv.classic |
| Curve handling | Ratio → per-frame frequency delta `Δω = (ratio−1) · bin centre ω` (integer-bin exact at 50% overlap; fractional shifts tolerated at 75% overlap with linear interpolation of sidebands — [RESEARCH FINDING: L-D '99, −21 dB vs −51 dB sidebands]) |
| Stereo | Per-channel independent; declared; measured |
| Determinism | Deterministic |
| Bandwidth | `nyquistFraction = 0.45` + notes "locking removes phasiness, not transient smearing" |
| Failure conditions | As pv.classic |
| Metrics | Pitch error on ramps/reversals (the most dynamic-friendly spectral method [RESEARCH FINDING]), warble, spectral error, transient preservation, CPU (locking is a negligible add-on [RESEARCH FINDING]) |
| Research trade-offs | Still "fails for percussive sounds and transients in general" [VERIFIED-PAPER]; identity vs scaled locking choice — v0.1 implements **identity** locking (what L-D '99's peak-shift implicitly implements [RESEARCH FINDING B.1.9]); patents expired [RESEARCH FINDING] |

**v0.1 parameter defaults:** as pv.classic (`hann`, 2048, 512) [RECOMMENDED].

### 6.5 `native.granular` — granular pitch engine (creative-leaning)

| Field | Specification |
|---|---|
| Purpose | Constant-rate output grains read from the input at speed `ratio`; the deliberate *textural* engine (creative-leaning per registry) [RESEARCH FINDING B.1.11, SuperCollider PitchShift lineage] |
| UsageClass | `Prototype` (creative-leaning note in registry) |
| Processing model | Output grain grid at hop `Hg` (constant); each grain of length `G` reads input starting at `grainPos` advancing at `ratio` per output frame (the read grid maps through the ratio curve); Hann or triangular window, overlap factor `G/Hg = 4` (declared); overlap-add to output |
| DurationBehaviour | `Preserving` |
| Pitch control | `FixedBlock` at grain hop; `supportsDynamicRatio = true` — ratio sampled at grain start (per-grain quantisation is the *measured* characteristic [RESEARCH FINDING]) |
| Latency | `inputLatencyFrames = G` (grain fill), `outputLatencyFrames = G` (final grain tail) |
| Declared ratio range | `[0.0 excluded…] [0.125, 8.0]` (SC documents 0–4 [RESEARCH FINDING]; v0.1 declares an honest engineering range) |
| State | Grain scheduler (read positions), overlap buffers, per-channel |
| Allocation | `prepare()` only |
| Stochastic elements | Grain-start jitter (dither) to break comb uniformity — `SeededDeterministic` from the job seed (PCG64 §4.7); jitter amount is an engine parameter (default 0 = pure uniform grid; > 0 activates seeded jitter) |
| Block/flush | Preserving symmetric; flush emits the last partial grain ≤ `G` |
| Curve handling | Ratio at grain boundaries |
| Extreme ratios | Range above; extremes are a *feature* domain for creative mode (documented: chord "crumble" with small grains [RESEARCH FINDING]) |
| Sample rates | All six; `G`, `Hg` in seconds |
| Stereo | **Synchronised grain schedule across channels** (same seed-derived schedule per channel — coherent by construction; the research warning "decorrelated unless synchronized" [RESEARCH FINDING] is answered by synchronisation; unsynchronised mode is NOT provided in v0.1) |
| Determinism | `SeededDeterministic` |
| Bandwidth | `nyquistFraction = 0.45`; notes: "grain-rate AM and comb coloration are the family artefacts; timeDispersion parameter mitigates comb" |
| Failure conditions | length-policy taint; end-of-render violation; invalid grain params (G < 4·Hg etc.) ⇒ CONFIG ERROR |
| Metrics | AM at grain rate (the characteristic artefact), spectral error, transient preservation (family weakness), warble, creative-mode listening |
| Research trade-offs | Grain-rate AM; comb filtering from uniform placement (SC `timeDispersion` concept → v0.1 jitter parameter); textural by design — not a quality engine [RESEARCH FINDING]; "Harmonizer" naming is an Eventide trademark — never used in display names [RESEARCH FINDING] |

**v0.1 parameter defaults:** `grain_seconds = 0.1`, `overlap = 4` (Hg = G/4), `window = hann`, `jitter_frames = 0` [RECOMMENDED].

---

## 7. Shared resampling primitives — `PARTIALLY DEFINED` (algorithm fixed, acceptance measurable, constants recommended)

### 7.1 Role

One band-limited interpolator core (architecture §0.5 item 1; §H.2 "band-limited Kaiser-sinc resampling"; §J "sinc resampler — donatable"). Consumers: varispeed (read-position interpolation + AA pre-filter), pv.classic (post-stretch resample), vardelay (fractional reads, small preset), granular (grain reads), corpus generator (fixed-rate synthesis is direct — no resampling needed for generated signals; used only if a corpus asset must be rate-converted, which v0.1 does not do — assets are authored at target rates).

### 7.2 Algorithm — `DEFINED`

Windowed-sinc interpolation, Kaiser window, evaluated per output sample at fractional input position `p`:

```text
y[m] = Σ_{i=-K}^{K} x[round(p) + i] · sinc(cutoff · (p − round(p) − i)) · kaiser(i + p − round(p), K, β)
```

with `sinc(x) = sin(πx)/(πx)`; `cutoff` = normalised pre-filter cutoff (§7.4); `K` = half-width in taps; `β` = Kaiser parameter. Fractional position `p` carried as `double` (the caller's accumulator — e.g. varispeed's read position). The kernel is evaluated directly (`sin` calls, `-ffp-contract=off`, deterministic) — **not** table-based, in v0.1 (table interpolation would add a quantisation layer; direct evaluation is the reference-grade choice; table optimisation is a post-v0.1 measured change only if CPU cost demands it [RECOMMENDED]).

### 7.3 Quality presets — `RECOMMENDED` (engine parameters, not architecture)

| Preset | K (half-width taps) | β target | Intended use |
|---|---|---|---|
| `small` | 8 | 7.9 (~80 dB stopband) | vardelay reads, granular reads |
| `standard` | 16 | 7.9 | pv.classic post-resample |
| `reference` | 24 | 8.8 (~90 dB stopband) | varispeed reference engine |

### 7.4 Anti-alias policy — `DEFINED` (semantics) / `RECOMMENDED` (margin)

- Upward shifts (read faster than output): input low-passed at `Ny/r` **before** interpolation (architecture §I.2; aliasing otherwise folds content down — correctness). The pre-filter is a windowed-sinc FIR (same Kaiser machinery, fixed cutoff per §6.1 policy), applied forward-only (offline, zero phase — deterministic and phase-linear; IIR rejected for determinism/phase reasons).
- Downward shifts: no pre-filter (spectrum moves down; no aliasing by construction — §I.2).
- Cutoff margin: filter cutoff set at `0.95 · Ny / max(1, r)` [RECOMMENDED] — the 5% margin keeps the transition band below the fold point; measured by the aliasing-indicator metric.
- Dynamic curves: cutoff follows the block-wise policy of §6.1 (whole-curve max in v0.1; conservative, documented).

### 7.5 Deterministic behaviour — `DEFINED`

- All kernel evaluation in `double`; no FMA contraction (`-ffp-contract=off` on the core library); no vectorised-math library calls whose accuracy varies (plain `libm` `sin`, documented: `libm` `sin` accuracy is not bit-stable across platforms — therefore the *reproducibility* guarantee is same-binary bit-exactness (§4.7/§L), NOT cross-platform bit-exactness; golden regression tests use tolerance bands (±20% relative headroom, provisional §N.9) for cross-platform drift).
- Ratio update: per-sample (position accumulator advanced by the instantaneous ratio); no per-block ratio quantisation inside the resampler (engines decide their cadence).

### 7.6 Edge handling — `DEFINED`

Input positions are clamped to `[0, N_in-1]` by zero-padding semantics: reads outside the real input read renderer-provided zero padding (§4.3.6) for the tail side; the leading side (negative positions) can only occur with lookahead kernels at position 0 — treated as zeros (documented; a 1-frame edge transient is below the −80 dBFS criterion for K ≥ 8 with Hann-family windows — validated by the unit test below).

### 7.7 Acceptance tests (unit, §13 T-R1/T-R2) — `DEFINED` as measurable criteria

- Stopband: attenuation ≥ 80 dB (preset-dependent: 80/80/90) above `cutoff + transition`, transition ≤ 5% of Nyquist, measured on a log-sweep/white-noise injected signal.
- Passband ripple ≤ ±0.1 dB below `cutoff − transition`.
- Impulse response round-trip at ratio 1.0 identity: output == input within −80 dBFS (L-5 criterion).
- DC and Nyquist edge cases: no NaN/Inf; bounded error.

---

## 8. Sample-rate / wideband contract — `DEFINED`

1. **Supported sample rates** (first-class, no other rates accepted by the harness): 44100, 48000, 88200, 96000, 176400, 192000. Engine support declared per engine; jobs at unsupported rates ⇒ JOB SKIP `sample-rate-unsupported` (§K).
2. **Preserved audio bandwidth** is a per-engine declaration (`BandwidthSpec`), not a global promise. **Actual engine bandwidth** is *measured* by the HF-energy and aliasing-indicator metrics. Declared-vs-measured mismatch ⇒ capability-honesty flag (§K row).
3. **No arbitrary internal 20 kHz ceiling** (architecture §I.1): all FFT sizes, windows, grain sizes, metric bands are specified in seconds/Hz and converted per rate at `prepare()`. Engines MAY run internal fixed-rate cores (none planned in v0.1); any such engine must declare the resulting ceiling in `BandwidthSpec`.
4. **Corpus assets are authored at target rates**; the harness never silently resamples (architecture §I.1.3). Explicit resampling = declared engine+quality in the experiment; output is a generated intermediate under `pitch-lab/artifacts/`, manifest-recorded.
5. A high sample rate does NOT imply ultrasonic preservation — exactly what the benchmark demonstrates (declared vs measured, §I.1.4). Per-engine bandwidth honesty summary: varispeed (with AA) ≈ 0.45 Ny honest at extreme ratios (content legitimately moves), pv family 0.45 Ny (resampler stopband), vardelay/granular 0.45 Ny (kernel-limited); all values are declarations verified by measurement.

---

## 9. WAV I/O contract — `DEFINED`

| Aspect | v0.1 policy |
|---|---|
| Supported input formats | RIFF/WAVE, `WAVE_FORMAT_IEEE_FLOAT` (fmt tag 3) with 32- or 64-bit sample containers; `WAVE_FORMAT_EXTENSIBLE` with those subformats when channels > 2 or channel mask present. 192 kHz + multichannel round-trip is a §L unit-test mandate |
| Rejected inputs | PCM integer (explicit CONFIG ERROR message "integer PCM not accepted — author assets as IEEE float"), compressed, truncated, inconsistent header sizes, unknown fmt tags |
| Output formats | **Masters: float64** (fmt 3, 64-bit; L-4/A-1). **Listening/export copies: float32** (fmt 3, 32-bit), produced by an explicit export step from the master (f64→f32 conversion, nearest-even) |
| Channel handling | Files are interleaved; internal representation planar; reader de-interleaves, writer interleaves. Channel order: file order, no mask reordering (mask recorded verbatim when present) |
| Sample-rate handling | Rate read from fmt; carried per job; never resampled implicitly |
| Metadata policy | Reader ignores unknown chunks (skips by size); writer emits ONLY `fmt` + `data` (+ `fact` for float) — deterministic bytes: same render ⇒ byte-identical WAV (no timestamps, no software tags inside the WAV; provenance lives in the sidecar manifest) |
| Naming | `artifacts/renders/<run-id>/<engine-id>/<asset-id>__<curve-id>__<fs>__<paramtag>/<basename>.wav` + `.manifest.json` (architecture §C.3/F.3); listening copies under `.../listening/` with `.f32.wav` suffix |
| Deterministic export | Manifest-driven regeneration: deleting a render and re-running reproduces byte-identical files for deterministic engines (§L reproducibility gate `pitchlab verify`) |
| Invalid input behaviour | CONFIG ERROR at corpus validation (finiteness scan = input integrity hash includes a NaN/Inf check — §K row) |
| Size limits | RIFF 32-bit sizes ⇒ 4 GiB cap; hitting it is a JOB FAILURE with explicit message (W64 upgrade is a post-v0.1 owner decision — OD-14) |
| Research vs listening separation | Masters and listening copies are distinct files with distinct suffixes; analysis reads masters only |

---

## 10. Analysis / metrics contract — `DEFINED` at interface level; every numeric tolerance `REQUIRES EMPIRICAL VALIDATION` (§N.9) unless analytic

Comparison axis: the **input timeline** is the common axis (architecture §H preamble); per-metric alignment declared in the module and configured in `config/metrics.toml`. Rate-followers vs preserving references warp via the emission map reconstructed from manifest data (curve hash + lengths + latency). No metric silently compares misaligned timelines. No global quality score (§O.3); aggregation presents per-metric, per-material-class tables.

### 10.1 Metric module sheets

| Module id | Inputs | Calculation (method class) | Alignment | Edge cases | Tolerance status | Deterministic reference values? |
|---|---|---|---|---|---|---|
| `pitch-error` | output master + expected f₀(t) (analytic from input + curve + emission map) + ReferencePitchTracker (OD-12) | tracker f₀ vs expected f₀; error stats (median/95%) | output timeline → input timeline via emission map (rate-followers) | unvoiced/silent spans excluded & reported; tracker unavailable ⇒ status `tracker-unavailable` | `REQUIRES EMPIRICAL VALIDATION` | Yes for analytic corpus (sine/harmonic: expected f₀ exact) |
| `pitch-lag` | as above, under ramps/reversals | cross-correlation of measured vs expected f₀ curves → lag in ms | as above | flat curves (no modulation) ⇒ `not-applicable` | provisional ±1 ms (arch §H.1) `REQUIRES EMPIRICAL VALIDATION` | Yes (deliberately block-quantised dummy engine produces known lag — §L) |
| `spectral-error` | output + varispeed reference master | log-spectral distance (own C++ STFT) | input-timeline common axis; cross-class warp via emission map | silent frames (log of ~0) handled by floor; different lengths (rate-followers) ⇒ warp first | provisional, `REQUIRES EMPIRICAL VALIDATION` | Partially (reference self-comparison = floor measurement) |
| `transient-preservation` | output + reference + onset times | attack correlation + onset-time error | onset-aligned windows | no onsets ⇒ `not-applicable` | provisional | Yes (synthetic impulse train: exact onset times) |
| `onset-timing` | output | onset detector (energy-based, C++) | output timeline | detection threshold effects documented | provisional | Yes on impulse corpus |
| `phase-coherence` | harmonic-stack outputs | harmonic alignment metric (audiojs-style) | frame-aligned | non-harmonic inputs ⇒ `not-applicable` | provisional | Partially |
| `stereo-coherence` | stereo output + input | inter-channel cross-correlation delta vs input | frame-aligned | mono ⇒ `not-applicable` | provisional | Yes (correlated/decorrelated synthetic pairs bracket the range) |
| `latency` | manifest (declared) + measured (onset/impulse alignment) | both reported; discrepancy ⇒ capability-honesty flag | impulse-aligned | — | provisional ±1 ms (arch §H.1) | Yes (impulse corpus) |
| `realised-duration` | output waveform + expected length (§4.3) | length ratio + conformance status | n/a | — | same tolerance state as §4.3.4 (OD-6) | Yes (constant-ratio analytic lengths) |
| `cpu-cost` | renderer-measured per block size {32…4096} | real-time factor, single thread, pinned; schedule documented | n/a | machine-dependent — reported as measurement, never a threshold in v1 | n/a (measurement) | No (environment-dependent; regression bands only) |
| `peak-rms-crest` | output waveform | peak/RMS/crest per channel | n/a | — | exact (waveform math, analytic) | Yes |
| `hf-energy` | output spectrum | band energy relative to job Nyquist (no 20 kHz ceiling — §8) | n/a | wideband assets: uses declared `bandContentHz` | exact band edges; interpretation provisional | Yes (synthetic band-limited signals) |
| `aliasing-indicator` | output spectrum + expected max output frequency (input band × ratio + emission map) | energy above expected max | n/a | upward extremes: expected max > Ny ⇒ metric inverts to "band occupancy" report | provisional | Yes (band-limited synthetic inputs) |
| `amplitude-modulation` | output envelope spectrum | envelope energy at expected grain/crossfade/hop rates | n/a | — | provisional | Yes for granular/vardelay (predictable rates) |
| `warble-instability` | f₀ track under static ratio | f₀ flutter variance | output timeline | requires tracker (OD-12) for general inputs; analytic inputs measurable without | provisional | Yes (sine) |

### 10.2 Tracker-dependent metrics under OD-12 — `PARTIALLY DEFINED`

`pitch-error`, `pitch-lag`, `warble-instability` (general inputs) require a `ReferencePitchTracker` implementation (§16 OD-12). Until resolved: modules ship with the interface + analytic-corpus paths where possible (sine/harmonic inputs have exact expected f₀; a *measured* f₀ still requires a tracker — so the full metric waits on OD-12; the analytic *expected* side and the harness plumbing do not).

### 10.3 Reference model (`assets/reference-notes/reference-model.md`) — `DEFINED`

- `native.varispeed` = first reference engine (band-limited; §H.2), UsageClass Prototype, `isReferenceRole = true`; reference render per (input, curve, fs) produced once per run by the same render path, manifest-tagged `reference`.
- Reference is NOT used for creative-mode judgement; not "best" — a mathematically appropriate anchor (varispeed is artifact-free by construction for band-limited inputs [RESEARCH FINDING B.1.1]).
- The intended reference *pitch tracker* is pYIN (owner) — blocked on OD-12 (C++-only route); recorded with the four investigated options.
- Future external perceptual anchors: architecture supports via UsageClass + job plan (no change needed).


---

## 11. Deterministic synthetic corpus — `DEFINED`

### 11.1 Generator

`tools/corpus_gen` — a stand-alone C++ tool (NOT a harness component; §4.8). Deterministic: every signal derives from fixed seeds via the harness PCG64 (§4.7) with consumer tag `"corpus"`. Generated ONCE, committed under `assets/corpus/syn-*` (authoritative inputs, version-pinned by git + manifest hashes). `corpus_gen --verify` re-generates in a temp dir and asserts byte-identical WAVs + metadata (regeneration gate). The same experiment re-run produces the same input signal (owner mandate, task §11).

### 11.2 Material classes (v0.1 synthetic set)

| Class | Items | Deterministic recipe (summary) |
|---|---|---|
| `sine` | 440 Hz, 5 s, at 44.1/48/96/192 kHz | phase-accumulated sine (`double`), exact frequency control |
| `harmonic` | saw-equivalent harmonic stack 220 Hz + 5 Hz ±0.5 st vibrato; harmonic stack 40 partials | band-limited additive synthesis, declared partial count |
| `polyphonic` | 3-tone chord (220/277.18/329.63 Hz) + octave-fifth stack | additive |
| `transient` | impulse train (1 ms spacing burst), filtered-noise percussive bursts | exact impulses; PCG64 noise through one-pole envelope |
| `noise` | white, pink (−3 dB/oct) | PCG64; pink via Voss/McCartney fixed-layer filtered white (layer depths fixed) |
| `sweep` | logarithmic 20 Hz → 0.45·Ny, 4 s | phase-integrated log sweep |
| `voice-like` | 120 Hz harmonic stack, 5 Hz vibrato, three fixed 2-pole resonators (formant-ish) | deterministic filters |
| `stereo` | correlated (identical channels) + decorrelated (independent PCG64 streams, shared envelope) | as research §B.5.1 item 10 |
| `wideband` | multi-tone stack to 80 kHz + band-limited noise 40–80 kHz, at 176.4/192 kHz | declared `bandContentHz` |

Every item: mono and stereo variants where meaningful; 5 s default duration (transients 2 s); amplitude normalised to −12 dBFS true peak (deterministic scaling computed from the exact signal, not measured). Metadata: §15.5 schema, `referenceStatus = "test-fixture"`, `sourceDescription = "generated by tools/corpus_gen v0.1, seed <seed>"`.

### 11.3 Curve battery (maps research §B.2 + architecture §G.3 into authoritative curve specs)

`experiments/curves/` files: `static-minus-24 … static-plus-24` (−24, −12, −5, −1, +1, +7, +12, +24 st), `identity-1x`, `ramp-slow-plus-1stps`, `ramp-fast-plus-12stps`, `ramp-negative-1stps`, `glide-exp-plus-12`, `reversal-plus12-minus12-100ms` (the killer test [RESEARCH FINDING B.2]), `extreme-{0.25,0.5,2,4,8}x`, `lfo-sine-5hz-depth-1st`, `random-walk-seeded` (seed fixed in file). Static 1:1 (`identity-1x`) is mandatory (task §13).

### 11.4 Reproducibility

Seeds: `corpus.toml` master seed (fixed constant, e.g. `0x50E5A1AB` — value is part of the committed generator config; changing it = new corpus version, owner-visible). Per-item seeds derived via SplitMix64(masterSeed, itemId). No wall-clock, no platform entropy. Unit test T-C1: regeneration bit-identity.

---

## 12. Real-world corpus — DESIGN ONLY (no acquisition in v0.1)

Schema (future `assets/corpus/<id>/metadata.toml` extension, tracked per asset):

```toml
id = "freesound-XXXXX-whoosh"
category = "whoosh"                 # §F.2 vocabulary
sampleRate = 96000
channels = 1
durationSec = 1.2
source = { provider = "Freesound", url = "https://...", identifier = "XXXXX", retrieved = "YYYY-MM-DD" }
creator = "..."
licence = { name = "CC-BY-4.0", url = "...", note = "attribution required" }   # MANDATORY
bandContentHz = [100, 40000]
referenceStatus = "real-world-recording"
provenance = ["original download <sha256>", "no transformations"]
transformHistory = []               # e.g. [{ op="trim", args="0.0-1.2s", tool="audacity" }]
localFile = { name = "signal.wav", sha256 = "..." }
```

Rules: no bulk downloads in this task (owner mandate L-8); licensing/source metadata tracked per asset (research §B.4 practice); real-world material never enters the technical benchmark's pass/fail path (separate perceptual layer, architecture §G.3 note); whip/whoosh measurement assets follow research §A.6.3 protocol when acquired.

---

## 13. Test strategy and verification matrix — `DEFINED`

Test levels (distinguished per task §13): `unit` (single module), `integration` (components through the harness), `golden/reference` (deterministic expected data), `benchmark` (§G suite runs — measurements, not pass/fail), `perceptual listening` (human, out of CI). A test that only verifies "did not crash" is NOT a correctness test — every correctness test asserts numeric/structural expectations.

### 13.1 Contract test matrix (every engine runs the SAME suite — architecture §L component layer)

| Id | Test | Level | Expectation |
|---|---|---|---|
| T-E1 | ratio = 1.0 identity | integration | output == bypass within −80 dBFS after declared-latency alignment (L-5 provisional criterion); engines must actually process (no shortcut path exists, §O.13) |
| T-E2 | constant ratio (e.g. 1.5, 0.75) | integration | measured pitch ≈ expected within provisional tolerance; length policy holds (§4.3.3/4.3.4) |
| T-E3 | positive/negative shifts (±1, ±7 st) | integration | as T-E2 both directions |
| T-E4 | sample-rate invariance | integration | same test at 44.1/96/192 kHz passes equivalently |
| T-E5 | stereo | integration | channel relationships within engine-declared expectations (varispeed/vardelay/granular-synchronised: coherent; PV: measured decorrelation reported) |
| T-E6 | determinism / repeated rendering | golden | same (input, config, schedule) twice ⇒ byte-identical WAVs; same across job re-runs (`pitchlab verify`) |
| T-E7 | block-boundary independence | integration | schedules {4096} vs {1024, remainder} ⇒ audio-equal within **−80 dBFS** (L-5; audio-equivalent, NOT bit-exact — bit-exactness is required only for the same schedule) |
| T-E8 | block-size variation | integration | schedule sweep {32, 64, …, 4096} renders valid output (stability, not equality) |
| T-E9 | end-of-input | integration | engine consumes exactly real input (+ padding rule §4.3.6); exhaustion signalled once |
| T-E10 | flush | integration | flush ≤ declared outputLatency; over-run ⇒ JOB FAILURE `end-of-render-violation` (failure-fixture tested) |
| T-E11 | output-length policy | golden | Preserving: `N_in + L` exactly; RateFollowing: within provisional ±1 block of integrated-curve expectation (OD-6; T-LEN-CAL calibrates) |
| T-E12 | extreme curve movement | integration | extremes 0.25×/8× render (or skip/saturate per mode) without NaN/Inf; taints recorded |
| T-E13 | rapid reversal | integration | ±12 st/100 ms curve: output finite; control-rate honesty measured (pitch-lag) |
| T-E14 | **T-LEN-CAL length calibration** | golden | varispeed reference over the full curve battery × fs set: reports the distribution of |actual − expected| lengths; output = calibration report consumed by OD-6 (owner ratifies authoritative tolerance) — no pass/fail, evidence only |
| T-E15 | invalid configuration | failure fixture | unknown engine id, bad TOML, missing seed, non-finite params, unknown parameter keys ⇒ CONFIG ERROR with file/field in message |
| T-E16 | unsupported range (benchmark) | failure fixture | out-of-range ratio ⇒ JOB SKIP `ratio-out-of-range`, not rendered |
| T-E17 | creative saturation | failure fixture | creative out-of-range renders; manifest records requested+effective+`RANGE_SATURATED`; engine received effective curve only |
| T-E18 | benchmark strictness | failure fixture | benchmark aggregation contains zero creative-namespace results, zero tainted results |
| T-E19 | engine registry correctness | unit | production registry: exactly the implemented engines, deterministic order, duplicate-id rejected, unknown-id lookup fails, seal() freezes; freeze-state expectation: **zero production engines** (§14) |

### 13.2 Additional layers

| Id | Test | Layer |
|---|---|---|
| T-A1 | allocation audit: `process()`/`finish()` never allocate (global new-hook in test builds) | unit (contract) |
| T-A2 | engine isolation: engine binaries/headers reference no harness/metrics/corpus symbols (include-scanner test) | unit (boundary) |
| T-D1..D3 | RNG golden streams; determinism same-binary; `reset()` reuse bit-identity | unit |
| T-R1/T-R2 | resampler stopband/ripple/impulse acceptance (§7.7) | unit |
| T-W1..W3 | WAV round-trip (f32/f64, 192 kHz, multichannel extensible); deterministic writer bytes; invalid-input rejection | unit |
| T-C1 | corpus regeneration bit-identity | golden |
| T-M1..Mn | per-metric analytic golden cases (§10.1 last column) + deliberately block-quantised dummy engine with known lag (pitch-lag golden) | unit/golden |
| T-K1 | failure-model fixtures: every §K case triggered by a fixture experiment; class + persistence asserted | failure |
| T-G1 | renderer manifest completeness: all §F.3 fields, length accounting, requested/effective/status on saturated jobs | integration |
| T-G2 | full reproducibility gate: re-run ⇒ byte-identical WAVs, manifest diffs empty (`pitchlab verify`) | integration |
| T-INF1 | freeze-state CI smoke (runs at freeze): toolchain C++20 feature set; FP determinism guards (`__FAST_MATH__` undefined, reproducible double accumulation); registry freeze-state (T-E19); version/phase constants | unit (infrastructure — explicitly NOT DSP correctness) |

### 13.3 CI mapping

CI (canonical, §1.4/build spec) runs at freeze: T-INF1 suite only (infrastructure). At implementation milestones, the same workflow runs the growing suite — the workflow file itself does not change (CTest discovers tests). Perceptual listening tests never run in CI (human layer). Benchmark runs (RTF measurements) are CI-executed measurements recorded as artifacts, not pass/fail gates (except NaN/taint gates).

---

## 14. Engine registry — freeze state and v0.1 end state — `DEFINED`

**Registration point:** `src/core/engine_registry.cpp::registerProductionEngines()` — the single authoritative location (§4.6). Adding an engine = editing that one TU + implementing the engine (architecture §D.6); never a config file.

**Freeze state (this cycle, verified by CI):** the production registration list is **EMPTY**. The five v0.1 engines exist in the registry only as *specified* entries (this §14 table); no engine code exists; the `pitchlab engines` CLI prints the empty list with the freeze notice. This is the honest state: registry content == implemented engines, always.

**v0.1 end state (implementation target — the registry MUST contain exactly this when v0.1 is complete):**

| id | UsageClass | role | DurationBehaviour | controlRate | channels | determinism | declared ratio range | bandwidth (nyq. fraction) | parameter keys (v0.1) |
|---|---|---|---|---|---|---|---|---|---|
| `native.varispeed` | Prototype | reference | RateFollowing | PerSample | MonoAndStereo | Deterministic | [0.0625, 16] | 0.45 + AA notes | `resample_quality`, `allow_aliasing` |
| `native.vardelay` | Prototype | — | Preserving | PerSample | MonoAndStereo | Deterministic | [0.5, 2.0] | 0.45 + wrap-AM note | `excursion_seconds`, `crossfade_frames`, `read_kernel` |
| `native.pv.classic` | Prototype | baseline note | Preserving | FixedBlock(512) | MonoAndStereo | Deterministic | [0.25, 4.0] | 0.45 + phasiness note | `window`, `fft_size`, `hop` |
| `native.pv.phaselocked` | Prototype | — | Preserving | FixedBlock(512) | MonoAndStereo | Deterministic | [0.25, 4.0] | 0.45 + locking note | `window`, `fft_size`, `hop`, `locking_mode` |
| `native.granular` | Prototype | creative-leaning note | Preserving | FixedBlock(grain hop) | MonoAndStereo | SeededDeterministic | [0.125, 8.0] | 0.45 + AM/comb note | `grain_seconds`, `overlap`, `window`, `jitter_frames` |

Test-only dummies (block-quantised engine with known control lag, etc.) live only in test binaries (§L). Post-v0.1 entries (wsola, tdpsola, externals…) appear per architecture §D.6 phase table — NOT in v0.1.

---

## 15. Configuration and experiment schemas (TOML) — `DEFINED` (syntax frozen for v0.1; marked where an owner could later extend)

### 15.1 `config/harness.toml`

```toml
# Authoritative harness defaults. NEVER engine identity (architecture §D.6).
version = 1
block_frames = 4096            # owner-locked default (L-3); configurable
manifest_format = "json"
export = { master_format = "float64", listening_format = "float32" }   # L-4 / A-1
```

### 15.2 `config/metrics.toml` + `config/tolerances.toml`

`metrics.toml`: alignment parameters (emission-map quadrature declaration, warp policy, per-metric enable flags). `tolerances.toml`: every provisional numeric tolerance in one place, each entry carrying `status = "provisional"` and a comment citing the calibration test (T-LEN-CAL, T-E7/T-E1 calibration runs, §N.9):

```toml
version = 1
[tolerances]
block_boundary_dbfs      = -80.0    # L-5 provisional — calibrate on Tier-A engines
ratio1_identity_dbfs     = -80.0    # T-E1 — provisional
length_rate_following_frames = 4096 # provisional ±1 block — OD-6/T-LEN-CAL calibrates
latency_declared_vs_measured_ms = 1.0   # provisional (arch §H.1)
pitch_error_median_cents = 25.0     # provisional — REQUIRES EMPIRICAL VALIDATION
regression_band_relative = 0.20     # golden regression headroom ±20% (arch §L provisional)
```

### 15.3 Curve spec (`experiments/curves/*.toml`) — schema sketch from architecture §E.2, frozen:

```toml
id = "reversal-plus12-minus12-100ms"
kind = "reversal"             # static|ramp_lin|ramp_exp|lfo|random|reversal|breakpoints|external
from = { semitones = +12.0 }  # or { ratio = 2.0 } (ratio preferred; semitones authoring surface)
to   = { semitones = -12.0 }
time = { ms = 100 }
hold = { ms = 400 }
seed = 0                      # REQUIRED for stochastic kinds
allow_discontinuity = false
extend = "hold-last"          # hold-first | hold-last
```

### 15.4 Experiment file (`experiments/suites/*.toml`, `experiments/creative/*.toml`)

```toml
id = "dynamic-basic"
kind = "benchmark"            # benchmark | creative (architecture §F.4)
[suite]
inputs  = ["syn-sine-440-5s-48k", "syn-harmonic-saw-220-48k"]   # asset ids (corpus registry)
curves  = ["ramp-slow-plus-1stps", "reversal-plus12-minus12-100ms"]
engines = ["native.varispeed", "native.pv.classic"]             # registry ids — unknown id = CONFIG ERROR
sampleRates = [48000]
channels = [1, 2]
[[engine_config]]             # per engine parameter set → EngineConfiguration
engine = "native.pv.classic"
params = { fft_size = 2048, hop = 512 }
seed = 7
[output]
master = true                 # float64 master render
listening = false             # float32 listening copies
[analysis]
metrics = ["spectral-error", "pitch-error", "cpu-cost"]
```

### 15.5 Asset metadata (`assets/corpus/<id>/metadata.toml`) — architecture §F.2 schema verbatim (fields: id, category, sampleRate, channels, durationSec, sourceDescription, referenceStatus, bandContentHz).

---

## 16. Open decisions and blockers (supersedes/architecture §N index for v0.1) — `OPEN DECISION` unless stated

**Resolved by owner mandate (this cycle):** §N.1 (pure C++ — L-1), §N.2 (TOML — L-2), §N.3 (4096 — L-3), §N.4 (float64 masters/f32 listening — L-4), §N.5 (audio-equivalent −80 dBFS provisional — L-5).

| Id | Item | Status | Notes |
|---|---|---|---|
| OD-6 (arch §N.6) | Rate-following length tolerance VALUE | open; procedure defined (T-LEN-CAL) | provisional ±1 block until calibration evidence |
| OD-7 (arch §N.7) | Tier-C adapter build order | open; post-v0.1 | unaffected by freeze |
| OD-8 (arch §N.8) | Registry growth / dynamic loading | open; revisit >20 engines | freeze state: static registry |
| OD-9 (arch §N.9) | All metric tolerance values | open; `REQUIRES EMPIRICAL VALIDATION` | calibration on Tier-A engines; tolerances.toml marked provisional |
| OD-10 (arch §N.10) | Corpus acquisition (real-world) | open; post-v0.1 | §12 schema ready |
| OD-11 (arch §N.11) | >2-channel depth | open | v0.1 tests mono+stereo only |
| **OD-12 (arch §N.12)** | **Reference pitch tracker (pYIN) under C++-only** | **open — investigated (worklog Task 10-c). No acceptable off-the-shelf C++ pYIN: c4dm/pyin GPL-2+, LibPyin GPL-3, Essentia AGPL-3, aubio GPL-3 (plain YIN), Sleepwalking/libpyin BSD-3 (full pYIN, C99, unmaintained since 2020, minimal API, small algorithmic deviations — build-verified during research). Options: (A) vendor BSD-3 libpyin+libgvps with validation gate [S–M effort]; (B) clean-room C++ pYIN from Mauch & Dixon 2014 + librosa parameters [M effort; ~800–1200 LOC; RECOMMENDED — licence-clean, fits own-primitives philosophy]; (C) interim plain C++ YIN with documented deviation [S effort; loses probabilistic voicing + HMM octave robustness]; (D) defer tracker, ship pitch-error as tracker-unavailable [S effort; flagship metric waits]. Owner decision required** | not silently resolved (L-7) |
| OD-13 (arch §N.13) | Report output format | open; presentation choice | markdown+CSV recommended by architecture |
| **OD-14 (new)** | WAV >4 GiB (W64) support | open | only if long multichannel 192 kHz masters exceed RIFF limit |
| **OD-15 (new)** | Publishing generated artifacts (e.g. reports) to GitHub as release evidence | open (owner) | replaces Task-9 implicit push (conflict C-2); architecture default: gitignore + regenerate |
| **OD-16 (new)** | Architecture v1.2 change cycle to fold in Amendment A-1 (f64 bus) | queued | text amendment only; no behaviour change vs this spec |

**Blocker assessment (task §22 stop conditions):** no stop condition is triggered that blocks *starting* implementation: all public APIs affecting multiple components are specified (§4); ownership/lifecycle/frame/curve semantics defined; build environment defined and CI-verified (§ build spec); all dependencies declared; the C++-only vs pYIN issue is investigated and recorded as OD-12 — it blocks only the tracker-dependent metrics (§10.2), not the engines or harness, and is explicitly recorded rather than silently decided. Package state: **READY WITH KNOWN LIMITATION** (§18).

---

## 17. Implementation order and acceptance gates — `RECOMMENDED` (sequencing advice; the scope itself is architecture-mandated)

1. Core types + RNG + WAV I/O + resampler (+ their unit tests T-D/T-W/T-R) — gates: acceptance tests §7.7 pass.
2. Curve library + validation (T-E15 fixtures) + corpus generator (T-C1) — gate: curve battery compiles deterministically.
3. Engine registry mechanism + harness skeleton (compiler → renderer → analyzer → reporter, CLI) with the varispeed engine first — gates: T-E1..T-E11 + T-LEN-CAL evidence pack for OD-6.
4. vardelay, pv.classic, pv.phaselocked, granular — each engine enters the SAME contract suite (T-E1..T-E13).
5. Metrics (analytic goldens first; tracker-dependent metrics wait on OD-12).
6. Full §G.3 suites + `pitchlab verify` reproducibility gate (T-G2) — v0.1 acceptance = architecture §L.

Each implementation cycle closes per Operating Principles §75/§119–§122 (worklog current-state, recoverable state, truthful closure).

---

## 18. Final implementation-gate assessment (this freeze cycle)

| Gate area (task §21) | Status | Evidence |
|---|---|---|
| Architecture: SoT, reconciliation, scope, non-goals | COVERED | §2 (map + conflicts C-1..C-3 + amendments); §1.1; non-goals inherited |
| Interfaces: contracts, ownership, lifecycle, state, frame accounting, error paths | COVERED | §4, §5, §4.3, §13 failure fixtures |
| DSP: engines, resampling, curves, benchmark/creative, sample-rate, WAV | COVERED | §6, §7, §4.4, §4.4.6, §8, §9 |
| Analysis: metrics, alignment, tolerance status, synthetic corpus | COVERED (tolerances provisional by design) | §10, §11; OD-9 |
| Environment: canonical CI, dependencies, no ambient state, reproducibility | COVERED | build/CI spec; CI workflow running at freeze (T-INF1) |
| Testing: matrix, deterministic fixtures, CI path | COVERED | §13 |
| Documentation: specs committed, worklog updated, open decisions recorded, contradictions recorded | COVERED | this document + build spec + worklog Task 10 |
| Recovery: repository state recoverable, artefacts committed, no chat-only knowledge | COVERED | all decisions are in repository documents; git state at a known commit; worklog current-state block |

**Package state: `PACKAGE READY WITH KNOWN LIMITATION`.** Known limitations: OD-6/OD-9 tolerance values provisional (calibration procedures defined); OD-12 reference tracker undecided (options + recommendation recorded); Amendment A-1 pending architecture v1.2 fold-in (OD-16); OD-15 artifact publication policy. Nothing required to *start* implementation is undefined.

**Exact next implementation action:** owner resolves OD-12 (tracker option A/B/C/D) → implementation begins at §17 step 1 (core types + RNG + WAV I/O + resampler with acceptance tests), closing each cycle per the Operating Principles master lifecycle.
