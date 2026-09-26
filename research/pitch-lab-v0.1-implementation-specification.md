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

Canonical build/test target = GitHub Actions, pinned `ubuntu-24.04` x64, GCC 13.3.0 (empirically re-pinned 2026-09-26 from live CI evidence — build spec v1.1 §1/§3), C++20, CMake 3.31.6 (checksum-pinned tarball), Ninja, CTest. Full specification, justification and exact commands: `research/pitch-lab-build-and-ci-environment.md`. Local builds are permitted but never authoritative; a clean checkout must configure/build/test with zero ambient dependencies (the freeze build vendors **no** third-party code). CI activation status (2026-09-26): **ACTIVATED and PROVEN** — run 36237247935 (head 593a2d5) green end-to-end on the canonical runner (all steps success, CTest 3/3, evidence artefact downloaded and content-verified); full run-by-run chronology including two honestly-recorded intermediate failures and their empirical fixes: build spec §12.1 / OD-17.

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

#### 4.2.1 Frozen implementation behaviour of the engine contract and registry factory — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 3; mirrors the §7.2.1/§4.4.3.1 pattern)

§4.2/§4.6 fix semantics; this subsection resolves the underdetermined details of the contract's concrete shape so the implementation has exactly ONE reading. Nothing here relaxes §4.2/§4.5/§4.6; anything not listed inherits those texts verbatim.

1. **`EngineException`** (introduced with the first engine, per `errors.h`'s forward note): `class EngineException : public std::runtime_error` carrying the throwing engine's id + reason. Engines throw ONLY this (§4.2); any other exception escaping an engine ⇒ JOB FAILURE.
2. **`ParameterValue`** = `std::variant<std::string, double, bool, int64_t>`; `EngineConfiguration` = `{std::vector<std::pair<std::string, ParameterValue>> parameters (sorted by key at construction); uint64_t seed}`. Harness validation stays exactly §4.5 (seed present, numeric values finite, key names ∈ descriptor `parameterKeys`); values' semantics are engine-owned.
3. **`ProcessContext` carries the effective curve view**: `const PitchCurveView* curve` joins `{sampleRate, channels, maxBlockFrames, totalInputFrames}`. Rationale: §4.3.5 requires ratio-dependent latency bounds to be computed "from the curve within `prepare()`" — impossible unless `prepare()` sees the curve. The view is the SAME object (identical pointer/frames) handed to every `process()` call; the renderer owns its storage for the whole job (§5). The engine reads only the EFFECTIVE curve (§4.4.6).
4. **`PitchCurveView` reading rule for `PerSample` engines**: the dense array is indexed at `floor(r)` clamped to `[0, frames−1]` — the clamp events (index ≥ frames, only reachable after the read position passes the real-input end) are recorded by the RENDERER's expectation replay into the `CurveAccessReport` (§4.4.5); no engine-side accessor is added. The `EngineEvent`/`sampleRatioAt` machinery of §4.4.5 is deferred until an `EngineEvent` engine exists (none in the v0.1 registry, §14).
5. **Registry factory**: `EngineDescriptor` gains `std::unique_ptr<PitchEngine> (*factory)() = nullptr`. `registerEngine()` REJECTS a null factory (`std::logic_error`) — registry content == implemented engines means an entry is constructible by definition (the anti-fake rule); test dummies register with real (trivial) factories. `Capabilities` gains the engine-declared supported-rate set `supportedSampleRates[]` + count (§8 "engine support declared per engine"); jobs at rates outside it ⇒ JOB SKIP `sample-rate-unsupported` (compiler-side check, §8.1).
6. **Renderer output-capacity policy** (makes the §4.3.2 loop starvation-free for rate-followers): per `process()` call the renderer supplies `outCapacity = ceil((maxBlockFrames + 2·inputLatencyFrames + 64) × max(1/minRatio, maxRatio)) + outputLatencyFrames + 64` computed from the job's engine descriptor + declared latencies. Rationale: a rate-follower can emit ≈ `block/ratio` frames per call (ratio < 1) or `block·ratio`-worth of read advance (ratio > 1); insufficient capacity would push steady production into `finish()` and falsely violate the flush bound. With this policy the process/flush split is schedule-invariant (see §6.1.1 item 6).
7. **`inputExhausted` semantics**: a sticky per-job flag — set in the `process()` call whose cumulative consumption reaches the full padded stream (`N_in + inputLatencyFrames`), reported as-is by every later call and by `finish()`; the exhaustion TRANSITION happens exactly once (T-E9 counts transitions, not repeat reports).

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

#### 4.4.3.1 Curve compiler — frozen implementation behaviour — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 2; mirrors the §7.2.1 pattern)

§4.4.3 fixes semantics and leaves the exact reading of several fields/formulas as `IMPLEMENTATION DETAIL` to be "fixed in the curve-compiler unit tests". This section records the frozen reading BEFORE implementation; the unit tests pin it. Nothing here relaxes §4.4.1–§4.4.6; anything not listed inherits the §4.4 text verbatim.

1. **TOML parsing (v0.1).** Curve specs, `config/corpus.toml` (§11) and later experiment files are parsed by an OWN strict subset parser (`src/core/toml_lite`), not a general TOML library — the v0.1 dependency set stays exactly {doctest} (build spec §7; the §J v0.1 note: "the v0.1 bootstrap vendors nothing beyond the build toolchain and test framework"). Accepted syntax (frozen v0.1 authoring surface): comments; bare keys `[A-Za-z0-9_-]+`; basic double-quoted strings (escapes `\" \\ \n \t \r` only); integers (decimal ± and `0x` hex, TOML style); floats (decimal, exponent, leading sign; `inf`/`nan` NOT accepted — §4.4.4 rejects non-finite values anyway); booleans; inline tables `{ k = v, … }`; arrays of scalars `[v, …]`; `[table]` headers and `[[array-of-table]]` headers. Anything else ⇒ `ConfigError` with file, line, and a parse-reason. Trailing commas are accepted (authoring kindness, TOML-legal in arrays; in inline tables TOML 1.0 forbids them — the parser REJECTS trailing commas in inline tables, staying TOML-conformant). Unknown keys in a spec ⇒ `ConfigError{field = key}` (typo protection, §4.5 pattern).
2. **Value/time authoring tables.** A value table is `{ semitones = x }` XOR `{ ratio = y }` (both/ neither ⇒ `ConfigError{field = <field>}`); `ratio = 2^(semitones/12)` via `exp2(st/12)` in `double` (§4.4.1: semitones live only here). A time table is `{ ms = x }` XOR `{ s = y }`, converted to seconds in `double`; times must be finite and > 0 (a zero-duration window would make several kinds degenerate — rejected; the extension policy covers "nothing happens yet").
3. **Kind `static`**: `value` (value table, REQUIRED). Compiled: every frame = the value.
4. **Kind `ramp_lin`** (endpoint form): `from`, `to` (value tables, REQUIRED), `time` (REQUIRED), optional `hold` (pre-hold at `from`, default 0). Compiled: hold `from` for `hold` seconds; then LINEAR interpolation **in the ratio domain**: `ratio[i] = a + (b−a)·u`, `u = (i−i₁)/(T·fs)` clamped to [0,1]; then hold `to`.
5. **Kind `ramp_exp`**: EITHER the endpoint form (`from`, `to`, `time`, optional `hold`) with **geometric interpolation in the ratio domain** `ratio[i] = a·(b/a)^u`, OR the rate form (`from`, `rate = { stps = x }` REQUIRED, `time` REQUIRED — the ramp must be bounded; unbounded multiplicative growth is rejected). Rate form is the §4.4.3 sentence verbatim: per-frame growth factor `g = exp2(stps/(12·fs))` computed ONCE, then applied multiplicatively per frame (`ratio[i+1] = ratio[i]·g`, recurrence — NOT the closed form `from·g^i`; the two differ in rounding and the recurrence is canonical). Battery `ramp-*-stps` curves use the rate form (constant semitone-per-second = geometric ratio growth); `glide-exp-*` curves use the endpoint form.
6. **Kind `reversal`**: `from`, `to` (REQUIRED), `time` (transition window, REQUIRED), optional `hold` (pre-hold at `from`, §15.3 example semantics: `+12 st` held 400 ms, transition over 100 ms, then hold `to` — the transition is the gesture; the derivative discontinuity at both window edges is always allowed per §4.4.3). Interpolation is LINEAR IN THE RATIO DOMAIN (same as `ramp_lin`; the canonical domain is ratio, §4.4.1).
7. **Kind `lfo`**: `wave = "sine"|"triangle"|"saw"|"random-walk"` (REQUIRED), `rate` (Hz, float > 0, REQUIRED), `depth` (`{ semitones = x }`, REQUIRED), optional `centre` (value table, default ratio 1.0), and for `wave = "random-walk"`: `min`/`max` (value tables, REQUIRED), `seed` (integer ≥ 0, REQUIRED). LFOs operate in the log₂ (semitone) domain: `ratio[i] = exp2((centre_st + depth_st·w(i/fs))/12)`, `w ∈ [−1, +1]`. Waveforms (period `1/rate`, phase from t = 0): `sine: sin(2π·rate·t)`; `triangle: 4u−1` for `u < 0.5`, `3−4u` for `u ≥ 0.5` (`u = frac(rate·t)`); `saw: 2u−1` (ramp −1→+1, hard reset — a VALUE discontinuity per cycle: `wave = "saw"` REQUIRES `allow_discontinuity = true` else `ConfigError` — the concrete v0.1 instantiation of §4.4.4's "value discontinuities flagged"; other kinds cannot author a value jump at all: breakpoint times are strictly monotone and external CSVs interpolate linearly). Random-walk LFO = item 8 at the LFO rate with the declared bounds/centre.
8. **Kind `random`**: `rate` (steps/second, > 0, REQUIRED), `depth` (`{ semitones }`, REQUIRED), `centre` (value table, REQUIRED), `min`/`max` (value tables, REQUIRED), `seed` (REQUIRED). Semantics: zero-order-hold random walk in the semitone domain — at each step boundary (every `fs/rate` frames) the walk position `p ← clamp(p + U(−depth, +depth), min_st, max_st)` and holds until the next step; `ratio[i] = exp2(p(i)/12)`. `U` comes from `makeConsumerStream(seed, "curve.random-walk")` via `nextDouble01()·2−1` (§4.7 consumer tag; reseeded per compilation — §4.4.3). Clamping (not reflection) is the frozen boundary rule.
9. **Kind `breakpoints`**: `points` (REQUIRED, non-empty array of inline tables): each `{ t = {ms|s}, value = {semitones|ratio}, law = "lin"|"exp" }` — `law` (default `"lin"`) declares the interpolation of the segment ENDING at that point (the first point's `law` is ignored). Times strictly increasing (validated, `ConfigError{field="points"}`); the curve starts at `points[0]`. Segment interpolation: `lin` = linear in ratio; `exp` = geometric in ratio (`a·(b/a)^u`), which requires `from`/`to` of the same sign — ratios are strictly positive by §4.4.4, so always valid.
10. **Kind `external`**: `source = { file = "name.csv" }` (REQUIRED, resolved relative to the curve file's directory), `mode = "dense"|"pairs"` (REQUIRED). `dense`: the file holds one ratio value per line at declared `rate` (REQUIRED field for dense, Hz > 0); row `j` is at time `j/rate`; the compiler linearly interpolates between rows onto the job grid. `pairs`: each line is `time,value` (time in SECONDS, strictly increasing — validated); linear interpolation between points. CSV syntax (both modes): `#` comments, blank lines, optional header line starting with `#`, CRLF tolerated; exactly one (dense) / two (pairs) numeric columns else `ConfigError{field="source.file"}` with the line number. `sourceDescription` (manifest, §4.4.3) = the resolved file path; the manifest itself is §17-step-3 machinery.
11. **Extension policy** (`extend`, default `"hold-last"`). The curve's defined window is `[t₀, t_end]` (t₀ = 0 for all time-parameterised kinds — only `breakpoints`/`external` can start later). LEADING uncovered region `[0, t₀)`: always holds the curve's FIRST defined value (nothing else exists to hold). TRAILING uncovered region `(t_end, N)`: `hold-last` ⇒ holds the last defined value; `hold-first` ⇒ holds the FIRST defined value (one-shot gesture semantics: return to the initial value after the event). Both spellings are total and deterministic.
12. **Post-compilation validation** (§4.4.4 "ratios finite and strictly positive" applied to the SIGNAL, not just the spec): after compiling, every frame is checked (finite, > 0); a violation ⇒ `ConfigError{field = kind, reason names the first offending frame}`. This catches any formula degeneracy (e.g. geometric underflow) at compile time. `totalFrames ≤ 0` ⇒ `ConfigError{field="totalFrames"}`. Empty curve (`points = []`, empty CSV, zero rows) ⇒ `ConfigError` (§4.4.4).
13. **Determinism**: compilation is a pure function of (spec, sampleRate, totalFrames) — stochastic kinds reseed their consumer stream per compilation (item 8). Same-binary bit-exact (libm `sin`/`exp2`/`pow` are same-binary deterministic; cross-platform drift is covered by tolerance bands in tests, per §7.5's reproducibility stance).
14. **Battery files** (§11.3, 21 files): `static-minus-{24,12,5,1}`, `static-plus-{1,7,12,24}`, `identity-1x`, `ramp-slow-plus-1stps` / `ramp-fast-plus-12stps` / `ramp-negative-1stps` (rate form, `time = { s = 5.0 }` matching the §11.2 5 s corpus default), `glide-exp-plus-12` (endpoint form 1.0 → +12 st over `{ s = 5.0 }`), `reversal-plus12-minus12-100ms` (§15.3 verbatim: ±12 st, `{ ms = 100 }`, `hold = { ms = 400 }`), `extreme-{0.25,0.5,2,4,8}x` (static, ratio authoring), `lfo-sine-5hz-depth-1st` (sine, 5 Hz, ±1 st, centre identity), `random-walk-seeded` (rate 4 steps/s, depth 1 st, centre 0, bounds ±12 st, `seed = 0x5EEDC0DE`).

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

#### 4.8.1 Frozen implementation behaviour of the harness (compiler → renderer → manifest) — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 3)

§4.8 fixes the public surface; this subsection resolves the concrete details so the cycle-3 harness has exactly ONE reading. Nothing here relaxes §4.8/§4.3/§4.4.6/§9; anything not listed inherits those texts verbatim. Layout: harness sources live in `src/harness/` (experiment compiler, offline renderer, manifest/JSON writer); the engine contract header is `src/core/pitch_engine.h`; engines live in `src/engines/` (the registration TU `src/core/engine_registry.cpp` is the glue — engine TUs themselves include ONLY core headers; T-A2 scans `src/engines/`).

1. **Experiment schema validation (§15.4, strict)**: `id` (non-empty string), `kind` = `"benchmark"|"creative"` REQUIRED; `[suite]` arrays `inputs`/`curves`/`engines`/`sampleRates`/`channels` REQUIRED, non-empty, string/int elements; `[[engine_config]]` REQUIRED for EVERY engine listed in `[suite].engines` (missing ⇒ CONFIG ERROR field `engine_config`; entry for an engine not in the suite, or two entries for the same engine ⇒ CONFIG ERROR); each entry: `engine` (string), `params` (inline table; keys validated against the descriptor's `parameterKeys` — unknown key ⇒ CONFIG ERROR field = key, §4.5), `seed` (integer ≥ 0 REQUIRED — §4.5). `[output] master` (bool, default `true`), `listening` (bool, default `false`). `[analysis] metrics` (string array; accepted and recorded; execution is §17 step 5 — no metric runs in this cycle). Unknown keys anywhere ⇒ CONFIG ERROR (typo protection, toml_lite tables are allow-listed per section).
2. **Asset resolution**: `inputs` are corpus asset ids: `<root>/assets/corpus/<id>/{metadata.toml,signal.wav}`; missing ⇒ CONFIG ERROR. The WAV's `{sampleRate, channels, frames}` (read through §9) is AUTHORITATIVE and cross-checked against `metadata.toml` (mismatch ⇒ CONFIG ERROR — corpus integrity).
3. **Job expansion + skips (cross-product with visible skips, never silent adaptation)**: jobs = `inputs × curves × engines × sampleRates × channels` pairings that survive: job rate MUST equal the asset's rate (pairings with a different rate ⇒ SKIP `rate-mismatch` — §8.4 no silent resampling); requested channels MUST equal the asset's channel count (⇒ SKIP `channel-mismatch` — no silent channel adaptation); engine rate support (⇒ SKIP `sample-rate-unsupported`, §8.1); engine channel support (`MonoAndStereo` ⇒ 1–2; ⇒ SKIP `channels-unsupported`); benchmark + curve outside engine `[minRatio, maxRatio]` ⇒ SKIP `ratio-out-of-range` (§4.4.6, never rendered); creative + out-of-range ⇒ the job is created with the saturated EFFECTIVE curve (element-wise clamp; manifest `RANGE_SATURATED`). SkipList entries carry the pairing + reason and are printed/reported (nothing silently dropped).
4. **Curve compilation per job**: `compileCurveSignal(spec, jobFs, N_in)` (§4.4.3.1) ⇒ REQUESTED signal; the compiler computes its hash + extrema; saturation (creative only) produces the EFFECTIVE signal + hash + extrema + derivation note (§4.4.6 vocabulary verbatim). The engine receives the effective signal only.
5. **Renderer loop (the §4.3.2 pseudocode, executable)**: build the padded stream (real `N_in` frames + `inputLatencyFrames` zeros — the padding is a renderer-owned zero buffer, delivered as regular trailing blocks); deliver blocks per the job's **block schedule**: `Constant(n)` (default `n` = harness.toml `block_frames` = 4096) or `Pattern([f1..fk])` (cycled; the final block is the natural remainder) — the schedule is recorded in the manifest; per-call out capacity per §4.2.1 item 6; after each call append `outputFramesProduced` frames; stall guard verbatim §4.3.2; `finish()` once; flush-bound check (`finish` output ≤ declared outputLatency ⇒ else JOB FAILURE `end-of-render-violation`); consumption guard `consumedTotal ≤ N_in + padding`; THEN: full-output NaN/Inf scan (first offender frame/channel recorded; ⇒ JOB FAILURE `non-finite-output`, no WAV written); length policy (item 6); WAV + manifest emission. The renderer never truncates or zero-pads output to force a length (§4.3.2).
6. **Length policy (executable)**: expected length: `RateFollowing` = `M₀ + declared outputLatencyFrames` where `M₀` = smallest `m` with `r(m) ≥ N_in`, `r` replayed by the renderer with the EXACT engine recurrence (§6.1.1 item 2 — same binary/op order ⇒ bit-identical replay); `Preserving` = `N_in + declared outputLatencyFrames` EXACTLY. Actual = total produced. `|actual − expected| > tolerance` ⇒ TAINT `length-policy` (render completes; §4.3.3/§4.3.4). Tolerance source: `config/tolerances.toml` `length_rate_following_frames` (provisional 4096; OD-6 — T-LEN-CAL produces the evidence; the calibration NEVER silently promotes an observed value into this file). The same replay computes the `CurveAccessReport` (§4.4.5: min/max requested index, clamp count — the clamp events are the flush-region index clamps of §6.1.1 item 2). Access-report window: the replay runs over `[0, expected)` — a SUPERSET of the engine's actual accesses (the engine's flush is bounded by the declared `outputLatencyFrames`, §4.3.5, and its M₀ is bit-identical to the replay's), so the reported clamp count is the expected-timeline model, never an undercount; when the actual length differs from the expectation the report documents the model, not a fabricated per-frame engine log.
7. **Manifest (sidecar, §9 naming; JSON, canonical)**: `artifacts/renders/<run-id>/<engine-id>/<asset-id>__<curve-id>__<fs>__<paramtag>/<basename>.manifest.json` next to `<basename>.wav`; listening copy `<dir>/listening/<basename>.f32.wav`. `run-id` = experiment id (path-safe sanitisation); `paramtag` = first 12 hex chars of SHA-256 over the canonical engine-config JSON (sorted keys); `fs` as integer Hz; `basename` = input WAV stem. ALL paths recorded in the manifest are RELATIVE to the pitch-lab root (regeneration in any checkout ⇒ byte-identical manifest). Fields: `schema` = `"pitchlab.manifest.v1"`; engine `{id, version, usageClass, origin, license, isReferenceRole, capabilities mirror (incl. supportedSampleRates), parameters (canonical, sorted), seed}`; input `{assetId, file, sha256, sampleRate, channels, frames}`; curve `{requested {specId, sha256, min, max}, effective {sha256, min, max, derivation}, access {minIndexRequested, maxIndexRequested, clampCount}}`; render `{sampleRate, channels, blockSchedule {kind, frames[]}, declaredLatency {inputFrames, outputFrames}, inputFramesActual, inputFramesConsumed, inputPaddingFrames, outputFramesProduced, flushFrames, expectedOutputFrames, actualOutputFrames, lengthDeltaFrames, lengthPolicy {status, toleranceFrames}}`; output `{master {file, sha256, format, frames} | null, listening {...} | null}`; execution `{status "ok"|"failed", failureReason?, taints[], saturationStatus "IN_RANGE"|"RANGE_SATURATED"|absent-for-benchmark}`; build `{pitchlabVersion, phase, compiler}`; experiment `{id, kind, file}`. Creative-saturation semantics preserved verbatim (§4.4.6). The master WAV's sha256 is computed AFTER writing (the file bytes; deterministic writer ⇒ regeneration-identical).
8. **Canonical JSON writer (own, deterministic)**: object keys SORTED (std::map); 2-space indentation; strings escaped per JSON (control chars as `\u00XX`); `int64` printed as integers; `double` printed with `%.17g` (round-trip exact, deterministic); `true/false/null`. Same value tree ⇒ byte-identical string (no timestamps, no locale, no map-order variance). Hashes: OWN SHA-256 (FIPS 180-4 clean-room; NIST test vectors pinned in its unit test; input = raw file bytes for assets/outputs, the little-endian IEEE-754 double array bytes for curve signals — the byte order is recorded in the manifest schema comment).
9. **Job failure model (§K, renderer-caught)**: engine throws `EngineException` (or anything else) ⇒ job FAILED with the error string, run continues; flush overrun ⇒ `end-of-render-violation`; non-finite output ⇒ `non-finite-output`; WAV size cap ⇒ `WavSizeLimitError` caught ⇒ job failed (OD-14 message). Failed jobs still emit a manifest with `status "failed"` + reason (provenance of the failure); no WAV. Failed jobs do not abort the run (§K).
10. **CLI (this cycle)**: `pitchlab compile <experiment.toml> [--root dir]` (validation dry-run: prints jobs + skips; no rendering) and `pitchlab render <experiment.toml> [--root dir]` (compile + render all jobs; prints per-job status; exit 0 = all jobs ok (skips are not failures), 1 = any job failed, 2 = CONFIG ERROR with the message). `--root` default: working directory (the pitch-lab root containing `assets/`, `experiments/`, `config/`, `artifacts/`). `analyze|report|verify|listen-index` remain honestly "not implemented yet" (§17 steps 5–6).
11. **Determinism of the whole render path**: same (experiment TOML, corpus, engine config, block schedule, binary) ⇒ byte-identical WAVs AND byte-identical manifests (deterministic writer §9; canonical JSON; relative paths; no run-unique data — `run-id` = experiment id, NOT a timestamp). T-E6 asserts exactly this.

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

#### 6.1.1 Frozen implementation behaviour of `native.varispeed` — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 3)

The §6.1 sheet fixes the model; this subsection resolves its underdetermined details so the engine has exactly ONE reading. Nothing here relaxes §6.1/§4.3/§7.2.1; anything not listed inherits those texts verbatim. All curve-dependent decisions (r_min/r_max, cutoff) use the EFFECTIVE curve (§4.4.6) — the engine never sees the requested one.

1. **Engine parameters** (registry keys, §14): `resample_quality` = `"small"|"standard"|"reference"` (string; DEFAULT `"reference"` — the §7.3 design anchor for this engine; maps to the shared preset table); `allow_aliasing` = bool, DEFAULT `false` (disables the AA pre-filter only — creative mode; recorded in the manifest; the read-kernel cutoff policy is unchanged). Unknown values ⇒ CONFIG ERROR at compile time (§4.5 key-set validation happens in the compiler; value validation happens in `configure()`). The seed is accepted, recorded, and unused (the engine is `Deterministic`; §6.1 "SeededDeterministic not needed").
2. **Read-position recurrence (verbatim §6.1, made executable)**: `r(0) = 0`; producing output frame `m`: `y[m] = interpolateAt(x̂, r(m))` (shared primitive, §7); then `r(m+1) = r(m) + ratioEff[clamp(floor(r(m)), 0, N_in−1)]` (left-edge rectangular rule; `double` accumulator; this exact recurrence is ALSO the renderer's expectation rule, §4.3.4 — same binary, same op order ⇒ the replay is bit-identical by construction).
3. **Cutoff + pre-filter policy (whole-curve-max, constant per job)**: `r_max = max(ratioEff)`, `r_min = min(ratioEff)` (scanned once in `prepare()`). Kernel cutoff `c = antiAliasCutoff(r_max)` (§7.2.1 item 6). If `r_max > 1` AND `!allow_aliasing`: the AA pre-filter is ACTIVE — FIR coefficients = `designAntiAliasFir(c, quality)` (the shared design function), applied STREAMING with engine state (item 4); the pre-filtered signal `x̂` replaces the raw input for all interpolation. Otherwise (r_max ≤ 1 or allow_aliasing): NO pre-filter, `x̂ ≡ x` (the filter stage is bypassed, not run with a unity kernel). This is the v0.1 instantiation of §6.1's "whole-curve max, conservative, documented" policy: the cutoff is CONSTANT for the job (no per-block cutoff changes ⇒ no cutoff transients; the "piecewise-constant per input block" general form degenerates to one piece).
4. **Streaming pre-filter**: `x̂[n] = Σ_{j=−K..K} h[j]·x[n+j]` — ascending `j`, plain `double` accumulator, taps outside the padded stream read ZERO: the SAME formula and tap order as `applyFirZeroPhase` (§7.2.1 item 8), evaluated per `n` in ascending order as input becomes available (needs raw `x[n+K]` delivered). Consequently the engine's `x̂` is bit-identical to a whole-buffer `applyFirZeroPhase` over the padded stream, and each output frame is a pure function of absolute input frames ⇒ varispeed output is bit-identical across block schedules — an OBSERVED STRONGER PROPERTY (like T-R2g at primitive level), deliberately NOT the engine-level contract: T-E7 keeps asserting the owner-locked L-5 audio-equivalence (−80 dBFS) criterion, never bit-identity.
5. **Declared latency (per job, from `prepare()`)**: `inputLatencyFrames = K + (pre-filter active ? K : 0)` — the honest total input lookahead (read-kernel half-width K, §6.1; plus the pre-filter's own K-frame lookahead when active — the sheet's "K" reading assumed the un-filtered path; with the FIR in the path the read position's RAW support is ±2K). `outputLatencyFrames = ceil(K / r_min)` (sheet value; r_min over the effective curve). With the padding = inputLatencyFrames and the gating of item 6, the flush bound holds by construction (item 7).
6. **`process()` gating (strict-delivery)**: the engine consumes all frames offered UP TO its pre-allocated window capacity (with the frozen renderer capacity policy of §4.2.1 item 6 the windows never fill — consume-all in practice; the partial-consumption fallback keeps §5 rule 1 (no allocation in `process()`) valid for ANY renderer offering any `outCapacity ≥ 0`, with the renderer's stall guard as the bounded backstop for a pathologically starved renderer). Offered frames are copied into the raw window (`inputFrameIndex == cumulative consumed` asserted); the engine extends `x̂` as far as raw availability permits (`x̂[n]` computable iff `n+K ≤ delivered−1`; `x̂[n]` for `n < 0` is the partial FIR with leading zero taps — NOT zero — so the filtered window starts at `−K`), and produces output frames in order while ALL of: (a) emission condition `round(r(m)) ≤ N_in−1+K` (kernel support overlaps real input, §6.1 "emits while kernel support r+K overlaps real input" — read-kernel support on the pre-filtered timeline, centred at the read position); (b) every REAL frame the output frame depends on has been delivered: with the pre-filter `round(r(m)) + 2K ≤ delivered`, without it `round(r(m)) + K ≤ delivered` (positions ≥ N_in are semantic zeros — never awaited); (c) `outCapacity` not exhausted. Production never waits on padding-delivery per se — only on REAL frames (b) — so the schedule only moves frames between process calls, never into the flush. With `inputLatencyFrames = K + (FIR active ? K : 0)` padding, the final process call produces exactly the frames with `round(r) ≤ N_in−1`, and the flush is exactly the `round(r) ∈ [N_in, N_in−1+K]` region — arithmetic coincidence by construction: the interpolation taps (up to `N_in+K−1`) and the FIR's raw dependency (up to `N_in+2K−1`) are covered EXACTLY by the delivered stream + padding with no semantic-zero synthesis anywhere in the steady path.
7. **`finish()` flush (bounded by construction)**: after the last process call (all real input + padding delivered), the engine extends `x̂` to its semantic end (final taps read zeros beyond the padded stream), then emits the remaining emission-condition frames — exactly the frames with `round(r(m)) ∈ [N_in, N_in−1+K]`: a read-travel of `< K − 0.5` input frames, i.e. `≤ ceil((K−0.5)/r_min) ≤ ceil(K/r_min) = outputLatencyFrames`. A defensive hard cap at `outputLatencyFrames` frames emitted by `finish()` remains (any overrun ⇒ JOB FAILURE `end-of-render-violation`, §4.3.5 — unreachable by the arithmetic above). The process/flush SPLIT is therefore schedule-invariant (item 6(b) is absolute-position gating), and the flush content is deterministic.
8. **Windows (the sheet's "ring buffer", honest sizing)**: raw and (when active) pre-filtered per-channel sliding windows over the absolute input timeline — implemented as pre-allocated vectors with front-compaction (memmove only; NO allocation after `prepare()`), sized `maxBlockFrames + 8K + 64` each. The 2K-frame ring of the sheet is the degenerate no-FIR steady-state window; the general form covers the pre-filter lookahead and per-call production without changing any semantics.
9. **Channels**: the read position `r` and all control decisions are engine-level (SHARED across channels); each channel's `x̂` and interpolation are computed independently with identical code ⇒ identical input channels produce bit-identical output channels (perfect coherence, §6.1); no per-channel state that could diverge.
10. **Non-finite guards**: `configure()` rejects non-finite parameter values (CONFIG ERROR); the curve is validated upstream (§4.4.3.1 item 12), so `r` stays finite; the renderer's output scan (§4.8.1) catches any residual non-finite output as JOB FAILURE.
11. **Determinism**: bit-identical for identical (input, config, fs, channels, block schedule); across DIFFERENT block schedules also bit-identical (item 4 observed property). Recorded in the manifest as `Deterministic` (registry declaration) — the cross-schedule bit-identity is an observation, not a contract.

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

#### 6.2.1 Frozen implementation behaviour of `native.vardelay` — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 4; mirrors the §6.1.1/§7.2.1 pattern)

The §6.2 sheet fixes the model; this subsection resolves its underdetermined details so the engine has exactly ONE reading. Nothing here relaxes §6.2/§4.2.1/§4.3/§7; anything not listed inherits those texts verbatim. All curve-dependent decisions use the EFFECTIVE curve (§4.4.6) — the engine never sees the requested one.

1. **Engine parameters** (registry keys, §14): `excursion_seconds` (double; DEFAULT 0.5; must be finite and > 0, else CONFIG ERROR; converted once in `prepare()`: `E = round(excursion_seconds · fs)`, and `E ≥ 1` else CONFIG ERROR — an excursion shorter than one frame is not a usable delay line; no upper cap: an absurd excursion fails at harness level through allocation failure, never a silent clamp). `crossfade_frames` (integer; DEFAULT 2048; must be ≥ 0, else CONFIG ERROR). **Parameter-unit ambiguity, resolved here and recorded:** the §6.2 table says "rate-converted at prepare" and the sheet's sample-rate row says "E, W specified in seconds (converted at `prepare()`)"; the parameter is nonetheless NAMED in frames and is used **verbatim as W frames at the job sample rate** — the seconds-based alternative would require inventing an undeclared reference rate; the frame reading keeps `outputLatencyFrames = W` exact at every rate with no hidden scaling. `read_kernel` (string; DEFAULT and ONLY accepted value `"small-sinc"` → the §7.3 Small preset K = 8, β = 7.9; any other value ⇒ CONFIG ERROR — the sheet fixes this one kernel). `W = 0` ⇒ wraps are instant jumps (degenerate, honest). The seed is accepted, recorded, and unused (Deterministic engine, §6.2).
2. **Delay recurrence (§6.2 verbatim, made executable)**: state `v` (double) = the wrapped active delay, `v(0) = 0` — the no-delay initial state (rationale: at ratio 1.0 the output is then `y(t) = x(t)` up to the kernel's integer-offset residues (measured ~3·10⁻¹⁷ — the §7.7.1/T-R2a identity behaviour: libm `sin(π·i)` is not exactly zero, so the side taps at integer offsets leave ~10⁻¹⁶-scale residues), far inside the T-E1 −80 dBFS criterion — the cleanest identity semantics an interpolating delay can have; pre-input positions read the §7.6 semantic zeros, so no buffer-priming logic exists). Per output frame `t`, AFTER the frame's read: `v ← v + (1 − ratio[clamp(t, 0, N_in−1)])` — the curve is sampled at the OUTPUT-timeline frame `t` (§4.3.1: a Preserving engine maps output frame k to input frame k — one shared timeline; PerSample control at that cadence); `double` accumulator; tail frames `t ≥ N_in` sample the clamped last curve value (the machine continues; the output ends at the length cap of item 7).
3. **Wrap condition (exact)**: at each advance, `v < 0` (strictly below the lower bound) ⇒ LOWER wrap — the active delay jumps by `+E`; `v > E` (strictly above the upper bound) ⇒ UPPER wrap — the active delay jumps by `−E`. `v == 0` and `v == E` exactly are VALID delays (no wrap; reads at delay 0 and delay E are in range).
4. **Crossfade mechanics (bounded, causal — at most ONE active fade)**: a wrap blends the pre-wrap and post-wrap read trajectories — which are exactly E apart and advance at the same per-frame rate — with equal-power gains. The placement is FORCED by causality and is direction-asymmetric: (a) **LOWER wraps**: the pre-wrap trajectory would run to NEGATIVE delay (future — unreadable), so the blend must COMPLETE BEFORE the crossing: the fade occupies the `n = min(W, k)` frames ENDING at the crossing frame, where `k` = frames-to-crossing from a deterministic forward simulation of the recurrence over the next W curve values (the engine looks ahead in the CURVE — legal and schedule-invariant: the whole effective curve is in engine memory for the whole job; the audio strict-delivery gate remains causal; the simulation is exact because the curve is fully known — no anticipation failure exists). (b) **UPPER wraps**: the pre-wrap trajectory runs to delay `> E` (retained past — readable, item 5), so the blend runs AFTER the crossing: the fade occupies the W frames STARTING at the crossing frame (clipped only by the output-length end). Gains over the `n` fade frames `j = 0..n−1`: `g_new = sin(π/2·(j+1)/n)`, `g_old = cos(π/2·(j+1)/n)` (equal-power; `W = 1` degenerates to an instant switch; a clipped fade `k < W` compresses the slope over `k` frames — the blend still completes exactly at the crossing). (c) **One fade at a time (bounded state)**: a wrap event occurring while a fade is active executes as an INSTANT active-delay jump (no new fade); the active fade's two branch trajectories are INDEPENDENT doubles (both advanced by the same per-frame `(1 − ratio)` increments; separation fixed at ±E) and are unaffected by mid-fade active jumps; when the fade completes, the output switches to the current active wrapped delay — possibly with a jump. This is the frozen behaviour for curves pinning the delay at a boundary (rapid reversals, or `E < W` degenerates): the wrap machinery saturates and the jumps are the honest artefact — the §6.2 "wrap/crossfade AM is intentional experimental behaviour; do not hide it with smoothing not in the SoT" rule. The fade-before detection does not run while a fade is active.
5. **Retention window (the "circular delay buffer", honest form)**: per-channel absolute-indexed sliding windows (pre-allocated in `prepare()`, front-compaction memmove only — the varispeed §6.1.1 item 8 pattern, private to the engine TU). Deepest read tap = delay `E + w + K`, `w` = branch excursion during a fade `≤ W·s_max`, `s_max = max|1 − ratio|` over the effective curve (scanned once in `prepare()`; ≤ 1 for every executed job because creative saturation clamps into the declared range — the curve-max sizing keeps direct-drive safety); the window's worst span is the END state (padding fully delivered at `N_in + E + K` while the tail still needs taps back at `N_in + W − 1 − E − w − K`): capacity = `maxBlockFrames + 2E + ceil(W·s_max) + 2K + 128` per channel; overflow ⇒ EngineException (sizing bug — never silent; unreachable by the sizing).
6. **Declared latency (sheet verbatim, per job)**: `inputLatencyFrames = E + K`, `outputLatencyFrames = W`. The renderer pads the input with `E + K` zeros (§4.3.6) and the engine CONSUMES the full padded stream (consume-all in `process()`); exhaustion is flagged when cumulative consumption reaches `N_in + E + K` (§4.2.1 item 7). The E-slack beyond the end-reads' strict need (`W + K`) is sheet-declared trailing room consumed as padding. This resolves the §6.2 "buffer must fill before valid output" parenthetical: with `v(0) = 0` the production gate is the K-frame kernel lookahead, and E appears in the DECLARED input latency as the buffer-depth term of the max-delay read machinery, not as an output-production lag.
7. **Frame accounting / output length (Preserving, exact)**: `process()` emits output frames `t ∈ [0, N_in)` only (strict-delivery gate: the frame's highest read tap `t − d(t) + K` must lie within the delivered stream); the tail `t ∈ [N_in, N_in + W)` is emitted by `finish()` — exactly W frames (equality with the declared `outputLatencyFrames`; reads beyond the padded-stream end are §7.6 semantic zeros with finish-mode zero taps). Canonical length = `N_in + W` ⇒ `lengthDelta = 0` exactly for every job (§4.3.3). Production is in output order; the engine stops at `N_in + W` regardless of fade state (a fade active at the end is truncated by the length cap — honest).
8. **Reads**: `y(t) = interpolateAt(window_c, t − d(t), cutoff = 1.0, Small)` per channel — shared absolute read positions and engine-level (NOT per-channel) branch/fade state ⇒ identical inputs produce bit-identical outputs (perfect coherence, §6.2 Stereo). NO anti-alias pre-filter (the §6.2 sheet specifies none, unlike §6.1): upward excursions (ratio > 1) alias above Nyquist/ratio — declared honestly in the capabilities' bandwidth notes; the read-kernel cutoff is the full-band 1.0 (fractional-read reconstruction, not rate conversion).
9. **Allocation / block processing**: ALL allocation in `prepare()` (per-channel windows; fixed-size fade state: two branch-delay doubles + counters — no dynamic containers in the processing path; T-A1 audits this). `process()` consumes all offered frames up to window capacity (with the frozen renderer capacity policy of §4.2.1 item 6 the windows never fill), produces what the gates allow (≤ outCapacity), tolerates any block boundary.
10. **Determinism**: bit-identical for identical (input, curve, config, fs, channels, block schedule); across DIFFERENT block schedules also bit-identical (every output frame is a pure function of absolute input frames + the absolute curve — no cross-frame audio state; an observed stronger property exactly as varispeed §6.1.1 item 4, recorded as observation, NOT contract — T-E7 keeps asserting the L-5 −80 dBFS audio-equivalence). Gain computations use `std::sin`/`std::cos` (same-binary determinism, §7.5).
11. **Invalid configuration** (CONFIG ERROR — the varispeed §6.1.1 precedent: value/domain errors throw ConfigError from `configure()`/`prepare()`; runtime contract violations throw EngineException from `process()`/`finish()`): non-finite or ≤ 0 `excursion_seconds`; `E < 1` after conversion; negative or non-integer-typed `crossfade_frames`; `read_kernel` ≠ "small-sinc"; invalid fs/channels/maxBlock/totalInputFrames; missing/mismatched curve view.
12. **Range behaviour**: declared [0.5, 2.0] (§14). Benchmark jobs outside ⇒ JOB SKIP `ratio-out-of-range` (compiler-side); creative jobs saturate into the declared range (§4.4.6 — the engine only ever sees the effective curve, so `|1 − ratio| ≤ 1` holds for every executed job).

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

#### 6.3.1 Frozen implementation behaviour of `native.pv.classic` — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 4; mirrors the §6.1.1/§6.2.1/§6.5.1 pattern)

The §6.3 sheet fixes the model; this subsection resolves its underdetermined details so the engine has exactly ONE reading. Nothing here relaxes §6.3/§4.2.1/§4.3/§7; anything not listed inherits those texts verbatim. All curve-dependent decisions use the EFFECTIVE curve (§4.4.6).

1. **Engine parameters** (registry keys, §14): `window` (string; DEFAULT and ONLY accepted value `"hann"` — the sheet fixes hann for v0.1; any other value ⇒ CONFIG ERROR); `fft_size` (integer; DEFAULT 2048; must be a power of two ≥ 32, else CONFIG ERROR — pocketfft handles arbitrary sizes, but v0.1 freezes the power-of-two STFT family so the hop/ratio arithmetic has one reading); `hop` (integer; DEFAULT 512; must satisfy `hop ≤ fft_size/2` (≥ 50% overlap — the L-D'99 guidance domain) and `hop ≥ 1`, else CONFIG ERROR). The seed is accepted, recorded, and unused (Deterministic).
2. **Pipeline order (the sheet's verbatim text is normative)**: analysis STFT → phase propagation → synthesis with the STRETCHED hop → overlap-add of the stretched signal → resample by the ratio through the shared §7 resampler (Standard preset — the §7.3 table's designated role: "pv.classic post-resample", K = 16, β = 7.9). The B.1.7 note ("resample-then-stretch is cheaper for upward shifts") is a recorded performance observation, NOT v0.1 behaviour — the stretch-then-resample order applies at every ratio.
3. **Analysis (fixed grid)**: frames n = 0, 1, 2, … at input positions `a_n = n·hop` (constant analysis hop); the windowed frame `w_a(a-frame)` is transformed by the vendored pocketfft persistent plan (`pocketfft::detail::pocketfft_r<double>`, constructed in `prepare()`, executed on engine-owned buffers — the header's public convenience functions allocate per call and are NOT used in the processing path; T-A1 audits this; `POCKETFFT_CACHE_SIZE 0` — no global plan cache, no hidden state). Forward transform unnormalised; bins k = 0..N/2 (the packed halfcomplex layout unpacked into engine arrays). Analysis taps beyond the padded stream read §7.6 semantic zeros (evaluated with finish-mode zero taps — the varispeed §6.1.1 item 7 pattern).
4. **Control sampling (hop boundaries)**: `ρ_n = ratio[clamp(floor(a_n), 0, N_in−1)]` — the curve sampled at the ANALYSIS boundary (input-indexed, the shared Preserving timeline position); the resampler's ratio at output frame m is `ratio[clamp(floor(m), 0, N_in−1)]` (output-timeline sampling — the final stage's control; the two sampling points are both the shared-timeline frame indices where each stage operates).
5. **Phase propagation (sheet formulas, made executable)**: per channel, per bin k ∈ [1, N/2−1] (DC and Nyquist excluded — see item 7): `Δφ_k(n) = princarg[φ_k(n) − φ_k(n−1) − 2πk·hop/N]` with `princarg(x) = x − 2π·round(x/2π)` (wrap to (−π, π]; `round` = llround on x/2π scaled — the exact wrap operation is frozen here); `ω̂_k(n) = 2πk/N + Δφ_k(n)/hop` (radians per input frame). Frame 0 has no predecessor: `Δφ` and `ω̂` are UNDEFINED at n = 0 — the synthesis phase accumulators are INITIALISED to the analysis phases (`φ′_k(0) = φ_k(0)`) and the first synthesis frame uses them directly (the standard initialisation, frozen).
6. **Synthesis (stretched grid, integer hops)**: the synthesis hop `h_n = max(1, round(ρ_n · hop))` — INTEGER frames (the stretched timeline is the integer sample grid; the rounding is the frozen realisation of the accumulated stretch); the synthesis position accumulator `s_0 = 0`, `s_{n+1} = s_n + h_n` (int64; the realised stretch over a run = Σh/Σhop). Synthesis spectrum: magnitudes `|X_k(n)|` verbatim (identity magnitude propagation — the classic), phases from the accumulators with the STANDARD classic recurrence: **`φ′_k(n) = φ′_k(n−1) + ω̂_k(n) · h_{n−1}`** (the advance INTO frame n uses the freshest IF estimate `ω̂_k(n)` — computed at frame n from the pair (n−1, n) — times the hop `h_{n−1}` that led into frame n; frame 0 initialises `φ′ = φ(0)`; frame 1 therefore advances from the initial phases with `ω̂(1)·h_0` — the standard one-frame transient). The synthesis frame = IFFT (c2r, factor 1/N) of the modified spectrum, windowed by the synthesis Hann `w_s`, overlap-added into the stretched accumulator at `[s_n, s_n + N)`.
7. **DC/Nyquist**: bins 0 and N/2 are REAL; the phase propagation for them degenerates (k = 0 and k = N/2 make the nominal-rotation term zero or π-ambiguous) — frozen: DC and Nyquist phases are propagated by the SAME recurrence with their own Δφ (the formula is well-defined for k = 0; for k = N/2 with even N the nominal term is `2π·(N/2)·hop/N = π·hop` — princarg-wrapped normally) — no special-casing, the recurrence handles them; their magnitudes propagate verbatim. (The phaselocked engine §6.4.1 may exclude them from locking — recorded there.)
8. **Synthesis OLA + normalisation (two parallel accumulators)**: the stretched signal accumulator is an integer-indexed ring (per channel, pre-allocated in `prepare()`, cells zeroed on the clear horizon as the resampler's read position passes — no allocation in the processing path). The synthesis frames ADD `w_s(j)·frame[j]` at `[s_n, s_n+N)`. The RECONSTRUCTION NORMALISATION is the local sum of the WINDOW PRODUCT over the synthesis grid: `norm(s) = Σ_k (w_a·w_s)(s − s_k)` (the product table precomputed in `prepare()`: the analysis window shape enters the frame content's amplitude profile, so the product — not `w_s` alone — is the correct reconstruction gain; for constant `h` and Hann·Hann at 75% overlap the interior sum is the classic constant). Because the resampler reads at FRACTIONAL positions, the normalisation is realised as a SECOND parallel ring accumulator holding the window-product sum (ADD `(w_a·w_s)(j)` at `[s_n, s_n+N)` per frame, engine-level — shared across channels since the synthesis grid is shared); the resampler reads BOTH accumulators at the same fractional position through the same shared sinc interpolator, and the output frame is `signal-read / window-read` (guard: `window-read ≤ 1e-12 ⇒ y = 0`). This keeps the fractional resampling consistent for both quantities and the interior gain exact.
9. **Resampler (the shared §7 primitive, whole-curve-max AA policy)**: read position `p` (double, ENGINE-LEVEL — shared across channels like varispeed §6.1.1 item 9): `p(0) = 0`; per output frame m: the signal and window-sum reads (item 8) use `interpolateAt(·, p, cutoff, Standard)`; then `p ← p + ρ(m)` (the left-edge rectangular rule; `ρ(m)` = the output-timeline ratio of item 4). Production gate: output m is produced when the synthesis head has passed the read's kernel support (`s_head + N > llround(p) + K`) AND (process mode) every real input frame the underlying analysis needed has been delivered. Cutoff: `antiAliasCutoff(ρ_max)`, `ρ_max = max(ratio)` over the effective curve (the whole-curve conservative policy of §6.1.1 item 3, imported by "through the shared resampler (§7)"). AA pre-filter instantiation of §7.4 for the stretch-then-resample pipeline: when `ρ_max > 1`, a SEPARATE streaming FIR stage sits between the signal accumulator and the resampler — each synthesis frame's OLA contribution is ALSO pushed through the streaming zero-phase FIR (the shared `designAntiAliasFir`, the varispeed §6.1.1 item 4 machinery with engine state) into a SECOND signal accumulator which the resampler reads; when `ρ_max ≤ 1` there is no pre-filter and the resampler reads the raw accumulator. The FIR is a convolution over the stretched timeline; running it as a separate post-OLA stage preserves the OLA additivity (applying it at frame-write time would not). The window-sum accumulator is NOT filtered (the normalisation lives on the unfiltered grid). This is the honest instantiation of §7.4 for this pipeline — recorded here because §6.3's sheet defers to §7.
10. **Latency (sheet verbatim)**: `inputLatencyFrames = N + H`, `outputLatencyFrames = N`. The renderer pads `N + H` zeros; the engine consumes the full padded stream; exhaustion at `N_in + N + H` (§4.2.1 item 7). The analysis continues over the padding (zero-tap frames); analysis frames needed beyond the padded stream read §7.6 semantic zeros in finish mode. The end game: the synthesis continues while `s_n` is below the stretched extent needed by the resampler's read horizon (bounded: the read position at the output end `p_end ≈ Σρ ≤ ρ_max·(N_in + N)` — the analysis extent needed `a ≤ (p_end + K + N)/ρ_min`-ish — all bounded per job; the engine stops the synthesis when the resampler's output reaches `N_in + N`).
11. **Frame accounting / output length (Preserving, exact)**: `process()` emits output frames `m ∈ [0, N_in)`; `finish()` emits the tail `[N_in, N_in + N)` — exactly N frames (equality with the declared `outputLatencyFrames`; reads of the accumulator beyond the synthesised extent are §7.6 semantic zeros — the output decays naturally). Canonical length = `N_in + N` ⇒ `lengthDelta = 0` exactly (§4.3.3).
12. **Stereo (per-channel independent phase — the sheet's declared decorrelation)**: the analysis spectra, phase accumulators, synthesis spectra and accumulators are PER-CHANNEL (independent); the control (ρ_n, h_n, s_n) and the resampler read position p are ENGINE-LEVEL (shared — the stretch grid and the resample grid are identical across channels). Identical input channels produce identical output channels (the per-channel state evolves identically from identical data — no stochastic divergence); NON-identical channels decorrelate (the phase propagation amplifies inter-channel phase differences — the declared family behaviour, measured by the stereo-coherence metric).
13. **Determinism**: bit-identical for identical (input, curve, config, fs, channels, block schedule). Across DIFFERENT block schedules also bit-identical (the STFT grid `a_n = n·hop`, the synthesis grid `s_n`, the accumulators' content and the resampler read positions are all ABSOLUTE-position functions; the production gating only moves output frames between process/finish calls, never changes their content — an observed stronger property like the other engines, recorded as observation; the T-E7 contract assertion remains the L-5 −80 dBFS audio-equivalence).
14. **Invalid configuration** (CONFIG ERROR — the varispeed/vardelay/granular precedent): `window` ≠ "hann"; `fft_size` not a power of two or < 32; `hop` outside [1, fft_size/2]; invalid fs/channels/maxBlock/totalInputFrames; missing/mismatched curve view.
15. **Range behaviour**: declared [0.25, 4.0] (§14). Benchmark jobs outside ⇒ JOB SKIP `ratio-out-of-range`; creative jobs saturate into the declared range (§4.4.6).
16. **pocketfft usage rule (the vendoring realisation)**: the engine TU includes `<pocketfft/pocketfft_hdronly.h>` with `POCKETFFT_CACHE_SIZE 0` and uses the persistent plan object `pocketfft::detail::pocketfft_r<double>` as a member (constructed in `prepare()`; `exec(buf, fct, r2h)` on engine-owned buffers; the packed halfcomplex layout unpacked by engine loops). The header's public convenience functions (`r2c()`/`c2r()`) allocate scratch per call and MUST NOT be used in `process()`/`finish()` (T-A1). This rule is part of the frozen engine behaviour.

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

#### 6.4.1 Frozen implementation behaviour of `native.pv.phaselocked` — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 4; mirrors the §6.1.1/§6.2.1/§6.3.1/§6.5.1 pattern)

The §6.4 sheet fixes the model (the L-D'99 peak-shift formulation: analysis STFT → peak detection → region assignment → identity phase locking → per-region frequency translation → OLA — duration preserving by construction, NO time-stretch, NO resampler); this subsection resolves its underdetermined details so the engine has exactly ONE reading. Nothing here relaxes §6.4/§4.2.1/§4.3; anything not listed inherits those texts verbatim (the pv.classic sheet is the structural sibling: "As pv.classic" rows import §6.3.1's machinery where cited). All curve-dependent decisions use the EFFECTIVE curve (§4.4.6).

1. **Engine parameters** (registry keys, §14): `window` (string; DEFAULT and ONLY accepted value `"hann"` — any other value ⇒ CONFIG ERROR); `fft_size` (integer; DEFAULT 2048; power of two ≥ 32, else CONFIG ERROR); `hop` (integer; DEFAULT 512; must satisfy `1 ≤ hop ≤ fft_size/2` (≥ 50% overlap), else CONFIG ERROR); `locking_mode` (string; DEFAULT and ONLY accepted value `"identity"` — the §14 end-state table lists this key while §6.4 fixes identity locking for v0.1: the key exists, and requesting any other mode (e.g. `"scaled"` — specified but NOT implemented in v0.1) is a CONFIG ERROR naming the value, never a silent substitution). The seed is accepted, recorded, and unused (Deterministic).
2. **Pipeline order (the sheet's model, made executable)**: analysis STFT on the fixed grid → per-bin phase-difference IF estimation → spectral peak detection → region-of-influence assignment (midpoints) → identity phase locking realised as the per-region RIGID ROTATION by the cumulated complex accumulator `Z` (item 8 — the sheet's `Z_{u+1} = Z_u·e^{jΔω_{u+1}·R}` formula is the definition) → per-region frequency translation `Δω_i = (ρ−1)·ω̂_peak` with linear sideband interpolation of the region's placement (item 9) → IFFT → windowed OLA on the SAME grid as the analysis (synthesis hop == analysis hop `H` — integer hops; the output timeline EQUALS the input timeline). There is NO time-stretch, NO shared-§7 resampler, NO AA pre-FIR anywhere in this engine (the sheet's "no stretch+resample needed" verbatim; the frequency-domain translation replaces the entire classic post-stage). The translation is per-REGION (each region follows its OWN peak's Δω_i), never a uniform whole-spectrum shift.
3. **Analysis (fixed grid — the §6.3.1 item 3 machinery verbatim)**: frames n = 0, 1, 2, … at input positions `a_n = n·H` (constant hop); the windowed frame is transformed by the vendored pocketfft persistent plan (the §6.3.1 item 16 rule verbatim: `POCKETFFT_CACHE_SIZE 0`, the plan object a member constructed in `prepare()`, `exec()` on engine-owned buffers, the header's public convenience functions forbidden in the processing path — T-A1 audits). Forward transform unnormalised; the packed halfcomplex layout unpacked per channel into engine arrays (re/im per bin); analysis taps beyond the padded stream read §7.6 semantic zeros in finish mode (process mode is gated on full delivery — the underrun throw, §6.3.1 item 3's pattern).
4. **Control sampling (analysis boundary — the §6.3.1 item 4 pattern)**: `ρ_n = ratio[clamp(floor(a_n), 0, N_in−1)]` — the effective curve sampled at the frame's analysis start on the shared Preserving timeline; the ratio belongs to the CURRENT frame n (it enters that frame's `Δω_i`). NO interpolation between PV frames: the control is hop-quantised (the declared `FixedBlock(H)` cadence — the sheet's ControlRate row with the default hop; the descriptor declares `blockFrames = 512`, the default H, as in §14). Reversal needs no special handling (the per-frame ρ_n simply changes; the machinery holds no ρ-state). `supportsDynamicRatio = true` (the L-D'99 per-frame-varying shift, B.1.8).
5. **Instantaneous frequency (the §6.3.1 item 5 formula, per bin)**: per channel, per bin k: `Δφ_k(n) = princarg[φ_k(n) − φ_k(n−1) − 2πk·H/N]` with `princarg(x) = x − 2π·llround(x/2π)` (wrap to (−π, π] — the exact frozen wrap operation); `ω̂_k(n) = 2πk/N + Δφ_k(n)/H` (radians per input frame), evaluated with the expression order `kTwoPi·k/N + dphi/H`. The engine evaluates `ω̂` ONLY at peak bins (item 8); the estimator is per-bin on the ANALYSIS side (the previous frame's phases at the same bin — no cross-frame peak association for the IF). Frame 0 has no predecessor: `ω̂` is UNDEFINED there and unused (item 8's frame-0 rule). The previous-frame phases are stored per channel per bin.
6. **Peak detection (deterministic; the sheet's definition made executable)**: on the CURRENT frame's magnitude spectrum `m_k = |X_k|`, over candidate bins k ∈ [1, N/2−1] (DC and Nyquist are never candidates — item 10): k is a peak iff `m_k > m_{k−1}` AND `m_k > m_{k+1}` (STRICT on both sides — the sheet's "magnitude greater than both neighbours each side"; the neighbour values at bins 0 and N/2 participate in the comparisons). No magnitude threshold and no minimum spacing (the sheet specifies none; strict adjacent maxima are ≥ 2 bins apart by construction; an exact tie with a neighbour disqualifies both bins — deterministic). Peaks are collected by ONE ascending scan (k = 1 … N/2−1) into a fixed-capacity array; the peak set is a pure function of the magnitude spectrum. No hash-map or unordered traversal anywhere.
7. **Region assignment (midpoints — the frozen rule)**: peaks `k_1 < k_2 < … < k_M`; boundaries `b_i = floor((k_i + k_{i+1})/2)` for i = 1..M−1; `region_i = [b_{i−1}+1, b_i]` with `b_0 = 0` and `b_M = N/2−1` — the regions tile [1, N/2−1] EXACTLY (every interior bin has exactly one owner). Equal-distance ties (k_i + k_{i+1} odd ⇒ the midpoint is fractional) resolve to the LEFT peak (the floor). Edge behaviour: the outermost regions extend to the spectrum edges (bin 1 and bin N/2−1). If M = 0 (no peaks — e.g. a silent or flat-frame spectrum): the frame is synthesized VERBATIM per item 8's fallback — no regions, no locking, no translation (the L-D translation is peak-defined; an unshaped frame passes honestly).
8. **Identity phase locking + the cumulated rotation Z (the L-D'99 semantics, frozen exactly)**. The locking relation is `φ′_l = φ′_peak + (φ_l − φ_peak)` for bins l in the peak's region — the intra-region phase DIFFERENCES are preserved IDENTICALLY (the L-D'99 "identity" variant; the alternative "scaled" locking would multiply the differences by the frequency-scale factor and is NOT v0.1 behaviour). Equivalently and normatively for the implementation: the region's complex spectrum is RIGIDLY ROTATED — `X′_l = X_l · z_i` — where `z_i` is the region's cumulated rotation, a per-channel, per-bin COMPLEX accumulator array `Z_k` initialised to 1 at prepare()/reset(). [RECORDED CLARIFICATION: the sheet's parenthetical "(φ′_l = φ′_peak)" is realised as this difference-preserving relation, NOT as "all region phases equal the peak phase" — the all-equal reading zeroes the Dirichlet-lobe phase alternation of a windowed sinusoid (adjacent lobe bins differ by π), a first-lobe-scale (−6 dB for Hann) reconstruction error even at ρ = 1, and is not the published technique; the sheet's own Z-rotation formula is the rigid-rotation realisation, and rigid rotation is what the L-D'99 peak-shift implicitly implements (B.1.9).] Per frame n ≥ 1, per region i with peak bin k_i: (a) the region's rotation is READ at the peak bin: `z = Z[k_i]` — the value written there by the PREVIOUS frame's region assignment (the array IS the cross-frame association: no explicit peak tracking, no re-initialisation on new or moved peaks — a peak inherits whatever rotation its bin carried; this is the frozen missing-peak/new-peak/region-reassignment behaviour); (b) the translation `Δω_i = (ρ_n − 1)·ω̂_{k_i}(n)` (the sheet's relation: the peak's IF times the ratio deviation); (c) the rotation update `z ← z · e^{j·Δω_i·H}` (the sheet's `Z_{u+1} = Z_u · e^{j·Δω_{u+1}·R}` with R = H — the synthesis hop equals the analysis hop; the rotation is applied AT the current frame with the current Δω; the complex multiply is `(zr·cosθ − zi·sinθ, zr·sinθ + zi·cosθ)` with `θ = Δω_i·H` — the frozen operation order; Z is never renormalised); (d) the region's bins are synthesized as `X′_l = X_l · z` placed per item 9, and the array is propagated: `Z[l] ← z` for EVERY l ∈ region_i (piecewise-constant per region — what the next frame's peaks read). Frame 0 (the first analysis frame per channel): NO peaks, NO regions, NO rotation — the synthesis spectrum is the analysis spectrum VERBATIM (magnitudes at original positions, own phases; the §6.3.1 item 5 one-frame-transient precedent; ω̂ is undefined at frame 0); Z stays 1. For a stable sinusoid the synthesis peak phase `φ′_peak(n) = φ_peak(n) + arg Z(n)` advances by exactly `ρ·ω̂·H` per frame (the analysis phase advance + the accumulated rotation), i.e. the output sinusoid frequency is `ρ·ω̂`; at ρ = 1 (Z ≡ 1, δ ≡ 0 exactly) the engine is the exact STFT→ISTFT identity.
9. **Magnitude placement (region-shifted with linear sideband interpolation — the sheet's own clause, resolved as the frozen answer)**: for each bin l ∈ region_i with rotated value `X′_l = (v_r, v_i)`: the target position `t = l + δ_i` (double) with `δ_i = Δω_i · N / 2π` (the frequency translation expressed in bins; the frozen expression order: `deltaBins = deltaOmega · N / kTwoPi`). The contribution is distributed by LINEAR interpolation: weight `(1 − f)` to bin `floor(t)` and weight `f` to bin `floor(t) + 1`, `f = t − floor(t)` (f = 0 ⇒ only floor(t) receives the full value; the zero-weight upper write is skipped). Each target bin is written IFF it lies in [1, N/2−1] — the interior band; DC and Nyquist are NEVER written by contributions (item 10). The range check is PER TARGET BIN: a contribution straddling the band edge is partially placed and its out-of-band part is TRUNCATED — no wrap-around (cyclic shift), no clamping of δ, no renormalisation of the remainder (the honest bandwidth behaviour of a frequency-domain translator: content pushed past Nyquist or below DC is dropped; the sheet's `nyquistFraction = 0.45` is the declared honest band). Contributions ACCUMULATE into the per-frame synthesis spectrum (complex accumulation): regions with different δ_i may overlap at boundaries (summed) or leave gaps (bins receiving nothing stay zero) — the documented L-D sideband/comb family behaviour, never smoothed. The frozen accumulation order: regions in ascending peak-bin order; within a region, l ascending; within a bin, the floor target before the ceil target; channels ascending in the outer loop.
10. **DC/Nyquist (excluded from locking — the allowance §6.3.1 item 7 records for this engine, exercised here)**: bins 0 and N/2 are excluded from peak candidacy (item 6), region assignment (item 7), rotation and translation; their synthesis values are copied VERBATIM from the analysis (`X′_0 = X_0`, `X′_{N/2} = X_{N/2}` — the identity propagation: DC has no frequency to translate, Nyquist translation is downward-ambiguous; item 9's band keeps translated contributions out of both bins).
11. **Synthesis / OLA (the §6.3.1 items 6/8 machinery on the unstretched grid)**: the synthesis spectrum (per channel, per frame) is packed halfcomplex, inverse-transformed (c2r, factor 1/N) on the persistent plan, windowed by the synthesis Hann `w_s`, and overlap-added at `[s_n, s_n + N)` with `s_n = a_n = n·H` — the synthesis grid EQUALS the analysis grid (constant integer hops; every position is an absolute-timeline function; there is no stretched accumulator and no second grid). The OLA signal accumulators are per channel; the window-product-sum accumulator (`w_a·w_s` adds at the same positions) is ENGINE-LEVEL (the grid is shared across channels). All three are the §6.3.1 item 8 absolute-indexed rings: zero-extendable, add-at-position, front-compaction by memmove, no allocation after `prepare()`.
12. **Output emission (integer reads, the OLA completion-boundary gate — the pv.classic cycle-4 lessons applied by construction)**: output frame m (m ∈ [0, N_in) in `process()`; [N_in, N_in + N) in `finish()`) reads the accumulator pair at ABSOLUTE position m: `y(m) = signal(m)/windowSum(m)` with the guard `windowSum(m) ≤ 1e-12 ⇒ y = 0` (the §6.3.1 item 8 guard; the Hann zero endpoint makes m = 0 the documented 0/0 edge — identical to pv.classic's T-E1 note). A cell m is COMPLETE iff every synthesis frame covering it has been placed: after placing the frame at s, cells m < s + H are complete (no future frame starts at or below them) — since the grid is H-periodic, the process-mode production gate is exactly `sCur_ > m` (`sCur_` = the next placement position = n·H after n frames). This is an OLA-COMPLETION rule reasoned from the timeline, never a sequential-stream or FIR-kernel rule (the pv.classic finding: cells within the newest frame's span are incomplete and schedule-dependent — a sequential-stream gate read them and seeded cross-schedule divergence). The analysis advance runs BEFORE and INDEPENDENT of the production loop and its output cap (the other pv.classic finding: an advance placed behind the output-cap break froze the synthesis and deadlocked the padded-stream consumption at small blocks); `produceInto()` with `outCapacity ≤ 0` still advances the analysis first and returns 0.
13. **Frame accounting / pacing / flush (the §6.3.1 items 10/11 pattern on this grid)**: `inputLatencyFrames = N + H`, `outputLatencyFrames = N` (the sheet: "As pv.classic"). The renderer pads N + H zeros; the engine consumes the full padded stream (consume-all; sticky exhaustion at cumulative consumption = N_in + N + H). The analysis loop: frames are processed while `a_n < N_in + N` (the synthesis need horizon: frames starting at or beyond N_in + N contribute to no output cell) AND, in process mode, `a_n + N ≤ consumedTotal` (strict full delivery); the frames whose input region extends beyond the padded stream (a_n ∈ (N_in + H, N_in + N)) are processed in finish mode with §7.6 semantic-zero taps (the pv.classic end game). `process()` emits m ∈ [0, N_in); `finish()` emits the tail [N_in, N_in + N) — exactly N frames (equality with the declared outputLatencyFrames). Canonical length = `N_in + N` ⇒ `lengthDelta = 0` exactly (§4.3.3). There is no resampler read position, no FIR progress and no stretched-grid accumulator in this engine: the output timeline position IS m.
14. **Stereo (per-channel independent spectral state — the sheet's declared decorrelation)**: the analysis spectra, previous-phase state, peak sets, region maps, Z rotation arrays, synthesis spectra and OLA signal accumulators are PER-CHANNEL (independent); the control (ρ_n) and the synthesis grid are ENGINE-LEVEL (shared); the window-sum accumulator is engine-level. Identical input channels produce bit-identical output channels; non-identical channels decorrelate through the independent peak/region/rotation evolution — the declared family behaviour (measured later by the stereo-coherence metric, §10; NOT "fixed" by any synchronisation).
15. **Determinism**: bit-identical for identical (input, curve, config, fs, channels, block schedule). Across DIFFERENT block schedules ALSO bit-identical (observed stronger property, as the other four engines): the STFT grid, the synthesis grid, the spectral state evolution and the accumulator contents are absolute-position functions of the frame index; the completion-gated emission only moves output frames between process/finish calls, never changes their content. The T-E7 contract assertion remains the L-5 −80 dBFS audio-equivalence, never bit-identity. The peak scan, region assignment, rotation, placement and accumulation orders are frozen ascending (item 9); no unordered-container dependence; no RNG (the seed is unused).
16. **Invalid configuration** (CONFIG ERROR — the four-engine precedent): `window` ≠ "hann"; `locking_mode` ≠ "identity"; `fft_size` not a power of two or < 32; `hop` outside [1, fft_size/2]; invalid fs/channels/maxBlockFrames/totalInputFrames; missing/mismatched curve view; non-finite or non-positive curve ratios (the §6.3.1 prepare-time scan).
17. **Range behaviour**: declared [0.25, 4.0] (§14). Benchmark jobs outside ⇒ JOB SKIP `ratio-out-of-range`; creative jobs saturate into the declared range (§4.4.6) — the ENGINE never clamps: the effective curve arrives pre-saturated, and whatever ρ it carries is translated literally (extreme δ_i values truncate at the band edges per item 9, honestly).
18. **pocketfft usage rule (the §6.3.1 item 16 rule, binding here verbatim)**: the engine TU includes `<pocketfft/pocketfft_hdronly.h>` with `POCKETFFT_CACHE_SIZE 0` and uses the persistent plan object `pocketfft::detail::pocketfft_r<double>` as a member (constructed in `prepare()`; `exec(buf, fct, r2h)` on engine-owned buffers; the packed halfcomplex layout unpacked by engine loops). The header's public convenience functions allocate scratch per call and MUST NOT be used in `process()`/`finish()` (T-A1 audits: zero allocations in the processing path).

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

#### 6.5.1 Frozen implementation behaviour of `native.granular` — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 4; mirrors the §6.1.1/§6.2.1 pattern)

The §6.5 sheet fixes the model; this subsection resolves its underdetermined details so the engine has exactly ONE reading. Nothing here relaxes §6.5/§4.2.1/§4.3/§7; anything not listed inherits those texts verbatim. All curve-dependent decisions use the EFFECTIVE curve (§4.4.6).

1. **Engine parameters** (registry keys, §14): `grain_seconds` (double; DEFAULT 0.1; finite and > 0, else CONFIG ERROR; converted once in `prepare()`: `G = round(grain_seconds · fs)`; `G ≥ 4` else CONFIG ERROR — a grain shorter than 4 frames cannot overlap-add meaningfully; no upper cap — an absurd grain fails at harness level via allocation failure, never a silent clamp). `overlap` (integer; DEFAULT 4; must be ≥ 4, else CONFIG ERROR — the sheet's failure row "G < 4·Hg ⇒ CONFIG ERROR" read literally with Hg derived from the parameter; no upper cap); the derived hop `Hg = round(G / overlap)` with `Hg ≥ 1` else CONFIG ERROR (overlap too large for the grain length). `window` (string; DEFAULT `"hann"`; accepted values exactly `"hann"` | `"triangular"`, else CONFIG ERROR). `jitter_frames` (integer; DEFAULT 0; ≥ 0, else CONFIG ERROR; 0 = the pure uniform grid). The seed is REQUIRED and CONSUMED (SeededDeterministic, §6.5).
2. **Window formulas (frozen, G frames, zero endpoints)**: hann — `w(j) = 0.5·(1 − cos(2π·j/(G−1)))`, j ∈ [0, G); triangular — `w(j) = 1 − |2j − (G−1)|/(G−1)`. Precomputed into a G-entry table in `prepare()` (deterministic `cos`; same-binary reproducibility §7.5).
3. **Grain grid + read positions (§6.5 verbatim, made executable)**: grains k = 0, 1, 2, … with `k·Hg < N_in + G` (the output end); grain k covers output frames `[k·Hg, k·Hg + G)`; the output grid positions are EXACT (never jittered — the OLA normalisation of item 5 depends on the regular grid). Read grid: `r_0 = 0`, `r_{k+1} = r_k + Hg·ratio_k` (left-edge rectangular per-grain accumulation — the quantised-control grid; the alternative of integrating the TRUE curve was rejected as an inconsistent hybrid of two quantisation levels); `ratio_k = ratio[clamp(floor(k·Hg), 0, N_in−1)]` (the curve sampled at the grain boundary on the shared Preserving timeline, §4.3.1 — per-grain quantisation is the §6.5-declared measured characteristic). Within grain k, the read at grain-local frame j: `p = r′_k + ratio_k·j` (a double — fractional reads through the shared sinc interpolator, §7 Small preset, cutoff 1.0; NO anti-alias pre-filter — the sheet specifies none: ratio > 1 aliasing is the declared textural behaviour, noted in the capabilities). Reads outside `[0, N_in)` are §7.6 semantic zeros.
4. **Jitter (SeededDeterministic, frozen generation order)**: when `jitter_frames > 0`, ONE draw per grain in grain order k = 0, 1, 2, …: `u_k = nextDouble01()` from the engine's consumer stream — `makeConsumerStream(seed, "engine.native.granular")` (the §4.7 exact derivation, consumer tag frozen here) — and `δ_k = (2u_k − 1)·jitter_frames`; the jittered read start `r′_k = r_k + δ_k` applies to the READ only (the grid accumulation of item 3 uses the UNJITTERED `r_k` — the jitter is transient, never fed back). The SAME draw sequence serves every channel (the synchronised schedule, §6.5: identical input channels ⇒ bit-identical outputs). `jitter_frames = 0` draws NOTHING (the RNG is still constructed — the stream is consumed only when jitter is active).
5. **Overlap-add + normalisation (the OLA policy)**: `y[n] = (Σ_k w(n − k·Hg) · x(p_k(n))) / s[n]` with `s[n] = Σ_k w(n − k·Hg)`, summed over the active grains (`k·Hg ≤ n < k·Hg + G`), ascending k, plain double accumulators — content first, window sum second (the exact order is part of the determinism). Guard: `s[n] ≤ 1e-12 ⇒ y[n] = 0` (never divide by ~0; reachable only at degenerate window geometry at the extreme output edges). This LOCAL-SUM normalisation leaves the interior (≈ constant `overlap/2` for hann) unchanged and normalises the START (only grain 0's window overlaps) and the END (the grid stops at the output end) to the surviving content: the output begins at grain 0's content and ends at the last grain's content — no additional fade-in/out exists (nothing in the sheet adds one; the normalisation is the minimum needed for gain-correct OLA; the deliberately NOT normalised-away grain-rate AM/comb coloration remain the §6.5 family artefacts).
6. **Latency (sheet verbatim, per job)**: `inputLatencyFrames = G`, `outputLatencyFrames = G`. The renderer pads G zeros (§4.3.6); the engine consumes the full padded stream (consume-all) and flags exhaustion at `N_in + G` (§4.2.1 item 7). Strict-delivery gate: output frame n is produced when every ACTIVE grain's read satisfies `p + K ≤ delivered − 1` OR `p ≥ N_in` (the zero region beyond the real input is never awaited — the varispeed §6.1.1 item 6 principle; for ratio > 1 the reads run ahead of n and the engine simply produces later — no stall, the input keeps arriving).
7. **Frame accounting / output length (Preserving, exact)**: `process()` emits output frames `n ∈ [0, N_in)`; `finish()` emits the tail `[N_in, N_in + G)` — exactly G frames (equality with the declared `outputLatencyFrames`). Canonical length = `N_in + G` ⇒ `lengthDelta = 0` exactly (§4.3.3); the engine stops at `N_in + G` regardless of grain state.
8. **Stereo**: engine-level grain state (grid, `r_k`, `ratio_k`, `δ_k`, window table — SHARED); per-channel input windows, reads and accumulation ⇒ identical inputs produce bit-identical outputs (the §6.5 synchronised schedule; no unsynchronised mode exists in v0.1).
9. **State/allocation (all in `prepare()`)**: the window table (G doubles); the grain-state ring (`r_k`, `ratio_k`, `δ_k` for the active window — `ceil(G/Hg) + 2` entries — fixed-size arrays, no dynamic containers in the processing path; T-A1 audits this); per-channel sliding input windows (the §6.1.1 item 8 pattern, private to the TU) sized `maxBlockFrames + ceil(r_max · 2G) + 2·jitter + 2K + 256` (the active grains' read spread plus generous margin — at any output frame all active grains read ≈ the same trajectory, so the true span is small; the sizing is deliberately conservative); overflow ⇒ EngineException (never silent). **Retention rule (the mechanism that keeps the window small):** the drop floor = the lowest read of the CURRENT active set (tracked at every attempted output frame) minus `K + jitter + 16` — the content below the ADVANCING read trajectory is dead (future reads are higher: ratio > 0, the active set only shifts to newer grains). **Read horizon + consume-and-discard:** the highest input position the OLA will ever touch is computed once in `prepare()` by simulating the unjittered grid (the same accumulation as the live schedule; the last grain's read end plus jitter/K/margin); `process()` stores offered frames below the horizon (up to window capacity — partial consumption is the in-order fallback) and consume-and-DISCARDS frames at or beyond it (counted as consumed, never stored — they are never read). This is REQUIRED for ratio < 1, where the reads lag the output timeline by up to `(1−r_min)·N_in` frames: without the horizon the unread backlog would have to be retained (O(N) memory) or the padded stream could never be fully consumed (a deadlock at the §4.3.6 end-of-loop check — exactly the failure the first implementation exhibited at ratio 0.25/0.75 before this rule was derived). The descriptor's ControlRate is `{FixedBlock, blockFrames = 0}` — the honest static declaration: the control hop is `Hg = G/overlap`, parameter- and rate-dependent, not knowable at registry level; 0 records "per-grain hop; derive from the recorded parameters" (the analyzer measures the effective control rate independently, §10).
10. **Determinism (SeededDeterministic)**: identical (input, curve, config, seed, fs, channels, block schedule) ⇒ byte-identical; across DIFFERENT block schedules also bit-identical (each output frame is a pure function of absolute input frames + the absolute grain schedule; the RNG draws are grain-indexed, not call-indexed — observed stronger property, documented like varispeed §6.1.1 item 4; the T-E7 contract assertion remains the L-5 −80 dBFS audio-equivalence).
11. **Invalid configuration** (CONFIG ERROR — the varispeed/vardelay precedent): non-finite or ≤ 0 `grain_seconds`; `G < 4`; `overlap < 4` or non-integer-typed; `Hg < 1` after rounding; `window` ∉ {hann, triangular}; negative or non-integer-typed `jitter_frames`; invalid fs/channels/maxBlock/totalInputFrames; missing/mismatched curve view.
12. **Range behaviour**: declared [0.125, 8.0] (§14). Benchmark jobs outside ⇒ JOB SKIP `ratio-out-of-range` (compiler-side); creative jobs saturate into the declared range (§4.4.6 — the engine only ever sees the effective curve).

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

### 7.2.1 Frozen implementation behaviour of the resampling primitive — `DEFINED` (implementation clarification, recorded 2026-09-26 BEFORE coding, cycle 1)

§7.2 is normative; this subsection resolves its underdetermined details so the implementation has exactly ONE reading. No reading below changes the §7.2 model (windowed-sinc, Kaiser window, per-sample fractional evaluation, direct `sin`, `double`, no tables). Each item records the interpretation and why it is forced by other frozen clauses. Status: this subsection is an **implementation clarification of ambiguity, not a redesign**; items flagged ⟡ additionally surface a tension between a `[RECOMMENDED]` constant and a `DEFINED` acceptance criterion and are queued for owner ratification (see OD-18).

1. **Kernel as a single fixed function of `u = p − n` (time-invariance).** Let `n0 = round(p)` (round-half-away-from-zero, `llround`; deterministic), taps `n = n0 + i`, `i ∈ {−K..K}`. The interpolation kernel is the single fixed function `h(u) = sinc_lp(c, u) · w(u)` evaluated at `u = p − n` for each tap (this is the reading in which the §7.2 `kaiser(i + p − round(p), K, β)` factor is the window evaluated at the tap's kernel-argument). Rationale: §7.7's passband-ripple/stopband acceptance criteria presuppose an LTI kernel; the alternative literal reading (window argument sign flipped, effective window centred at the mirror point `2·round(p) − p`) would make the effective kernel fractional-phase-dependent — contradicting those criteria.
2. **Amplitude factor.** `sinc_lp(c, u) = c · sin(π·c·u)/(π·c·u)` (with `sinc_lp(c, 0) = c`); `c` is the cutoff in **Nyquist units** (`c = 1` ⇔ passband to Nyquist; the ideal-LP impulse amplitude is the factor `c`). Without it a `c < 1` kernel has DC gain `1/c` (+0.45 dB at `c = 0.95`), failing the ±0.1 dB passband criterion; with it the kernel reduces to the plain interpolating `sinc` at `c = 1` (identity preserved).
3. **Kaiser window.** `w(u) = I0(β·sqrt(1 − (u/K)²))/I0(β)` for `|u| < K`, `w(u) = 0` for `|u| ≥ K`; `I0` = modified Bessel function of the first kind, own power-series implementation (fixed ascending-term summation order; early exit when a term falls below `1e-17·|sum|`, at most 48 iterations — deterministic for identical `u`; same-binary bit-exact per §7.5).
4. **Tap summation.** Ascending `i` from `−K` to `K`, plain `double` accumulator, no pairwise/vectorised reordering (determinism).
5. **Edge handling (§7.6).** Taps with `n ∉ [0, N)` read ZERO (leading and trailing sides — zero-padding semantics). The position `p` itself is never clamped by the primitive (the caller owns it; §7.6 wording "positions are clamped … by zero-padding semantics" is implemented as: reads outside the real input return zeros).
6. **⟡ Cutoff policy.** `c(r) = (r > 1) ? 0.95/r : 1.0` — kernel cutoff full-band for ratio ≤ 1, §7.4 margin cutoff for ratio > 1. Rationale: the §7.7 ratio-1 identity criterion (output == input within −80 dBFS) is unachievable with `c = 0.95` at `r = 1` (the kernel would low-pass integer-position reads: an impulse loses ~5% of its energy ⇒ ≈ −13 dB error); the §7.4 margin exists to keep the transition below the *fold point*, which exists only for `r > 1`. The §7.4 literal formula `0.95·Ny/max(1,r)` gives 0.95 at `r ≤ 1`; the refinement to `c = 1` for `r ≤ 1` is the only reading consistent with the DEFINED identity criterion. Dynamic jobs use `c(r_max_whole_curve)` for the whole job (§7.4/§6.1, conservative — documented). **Queued for owner ratification with the transition evidence (OD-18).**
7. **Presets (frozen v0.1 values, §7.3).** `small` K=8 β=7.9; `standard` K=16 β=7.9; `reference` K=24 β=8.8.
8. **AA pre-filter (§7.4).** Symmetric FIR, taps `h[n] = sinc_lp(c, n) · w(n)` at integer `n ∈ {−K..K}` — *the same kernel machinery by construction* ("same Kaiser machinery"); `(K, β)` = the active preset's; forward convolution, delay-compensated (`y[k] = Σ_j h[j]·x[k+j]`, zero-padded edges — the offline zero-phase form of §7.4); applied by the caller only when the effective ratio > 1. No per-phase normalisation anywhere (kernel AND FIR unnormalised; ripple is *measured*, not forced to zero — per-phase normalisation would break the LTI kernel property of item 1).
9. **Position accumulation (§7.5).** Per output frame: read at `p` (double), then `p += ratio_m` (the instantaneous ratio for that output frame — supplied per-frame or constant). The caller owns `p` (§7.2). Block calls carry `p` in/out; one call and any sequence of block splits producing the identical per-frame ratio sequence are **bit-identical** (same sequence of double additions — tested, T-R). **Contract relation (clarified 2026-09-26, contract-consistency check):** this is a *primitive-level invariant* — deliberately stronger than, and distinct from, the owner-locked engine-level block-boundary criterion (L-5 / T-E7: audio-equivalent within −80 dBFS, provisional). The primitive is stateless per output frame, so bit-identity genuinely holds by construction (same binary); engines with internal state (block-wise AA pre-filtering per §7.4/§6.1; PV/granular engines) do NOT inherit it and are not held to it. The T-R test pins the invariant as a regression property of the primitive; it does not relax, replace, or promote above the engine-level L-5 contract.
10. **Invalid configuration.** `c ∉ (0, 1]`, non-finite `c`/ratio/`p`, negative frame/output counts, null buffer pointers with non-zero counts, in-place (`y == x`) calls ⇒ `ConfigError` (§4.8 vocabulary) with the field name in the message.

#### 7.2.2 The absolute-position fractional read — `DEFINED` (amendment, recorded 2026-09-26 BEFORE coding, cycle 4 — required by the L-5/T-E7 criterion for accumulator engines)

`interpolateAtAbs(x, frameCount, baseIndex, position, cutoff, quality)`: the §7.2 fractional read with an EXACT-fraction, schedule-invariant arithmetic order. `position` is the caller's ABSOLUTE input-timeline coordinate; `baseIndex` is the absolute index of `x[0]`. The taps are the SAME absolute set as §7.2 for the same position: `n = llround(position) + i, i ∈ {−K..K}`; the fraction is `u = position − (double)llround(position)` — computed by the Sterbenz-exact subtraction (|position − n| ≤ 0.5 with n ≥ 1 makes the subtraction exact), whereas the §7.2 relative-position variant computes the fraction from `p − base` with double rounding that leaves a ~ulp(p)-scale, window-placement-dependent seed. `x[n − baseIndex]`; taps outside `[0, frameCount)` read ZERO (§7.6). Same kernel, same tap order, same determinism class as §7.2.

**Rationale (empirical, cycle 4):** engines with CROSS-FRAME ACCUMULATOR state — the phase-vocoder phase propagation — amplify any arithmetic seed to signal scale: the first cross-schedule output divergence (~1e-10, seeded by the relative-position rounding) was measured growing to 0.29 within 24k frames (the `princarg` wrap's `round` flips cascade through the phase recurrence). The exact-fraction read removes the schedule-dependent seed, making such engines' outputs bit-identical across block schedules (the L-5 −80 dBFS T-E7 criterion then holds by construction). The accumulator-free engines (varispeed, vardelay, granular) KEEP the frozen §7.2 relative reads: their cross-schedule residues stay at the last-bit scale, inside the criterion — their frozen behaviour, measured evidence and pinned artefact hashes are unchanged.

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

### 7.7.1 Measured acceptance evidence (cycle 1, 2026-09-26) — `EMPIRICAL RESULT` + `OPEN DECISION` (OD-18)

The acceptance criteria were measured on the frozen implementation (kernel-level: DTFT of the worst-case fractional-phase subfilters over 64 phases at `c = 0.475`; system-level: composite §7.4 path and analytic signals; GCC 14.2 local, `-ffp-contract=off`; the CI runner re-proves via `tests/resampler_test.cpp` T-R1/T-R2):

| §7.7 criterion | small (K=8, β=7.9) | standard (K=16, β=7.9) | reference (K=24, β=8.8) | verdict |
|---|---|---|---|---|
| kernel stopband ≥ 80/80/90 dB beyond `cutoff + T_A` | floor **−75.9 dB** | floor **−78.0 dB** | floor **−87.3 dB** | **NOT MET** (2–4 dB short) |
| transition `T_A` ≤ 5% of Nyquist | 31.6% | 16.0% | 11.8% | **NOT MET** (2.3–6.3× over) |
| passband ripple ≤ ±0.1 dB below `cutoff − T_A` | ±0.002 dB | ±0.0012 dB | ±0.0004 dB | **MET** (~50× margin) |
| ratio-1 identity within −80 dBFS | 2.2e-16 | 3.3e-16 | 3.9e-16 | **MET** (−309 dBFS) |
| DC/Nyquist: no NaN/Inf; bounded | verified (DC mean error −102 dB; edge transients bounded per §7.6) | as left | as left | **MET** |
| composite AA path (prefilter × kernel, r=2): injected out-of-band tones in the output | −179 dB / −182 dB (stopband / transition-edge probe) | — | measured on reference | **MET** (far beyond 80 dB) |

(`T_A` = worst-phase frequency where `|H|` first reaches the preset's −A dB line, minus `cutoff/2`. "floor" = worst `|H|` beyond `T_A` over all phases — i.e. once below the line, the small/standard presets' response recurs ~2–4 dB above it, so no single transition width satisfies the stopband criterion at those constants.)

**Interpretation (recorded, not silently resolved):** the §7.3 `[RECOMMENDED]` constants do not meet the §7.7 kernel-level stopband-depth and transition-width numbers. The composite resampling path (the §7.4 anti-alias usage that the "injected signal" measurement instructions of §7.7 actually exercise) exceeds the 80 dB criterion by ~100 dB because the pre-filter and read kernel band-limit twice. Correctness-relevant behaviour is therefore comfortably inside specification; the shortfall is a constants-vs-criteria tension inside a `PARTIALLY DEFINED` section. Recorded as **OD-18** (owner ratification): (i) ratify the constants as-is, (ii) raise β (≈ +0.7 per preset for true 80/80/90 kernel floors), or (iii) raise K for narrower transitions (5% of Ny at 80 dB needs K ≈ 100+). Nothing in this finding blocks the varispeed engine (§17 step 3): the reference preset's composite behaviour is the design anchor. Test-side: `tests/resampler_test.cpp` asserts the criteria that genuinely hold (ripple, identity, DC/Nyquist, composite suppression) plus clearly-labelled regression bands pinning the frozen kernel floors/transitions (≥4×/5.9 dB headroom over the measurements, cross-platform safe); the OD-18 evidence prints into the CTest JUnit output on every CI run.

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

`pitch-error`, `pitch-lag`, `warble-instability` (general inputs) require a `ReferencePitchTracker` implementation (§16 OD-12 — **RESOLVED 2026-09-26: owner chose the clean-room C++ pYIN, option B**). The tracker is later-cycle work (after engines/harness per §17 order); until it exists: modules ship with the interface + analytic-corpus paths where possible (sine/harmonic inputs have exact expected f0; a *measured* f0 still requires the tracker — so the full metric waits on the tracker implementation; the analytic *expected* side and the harness plumbing do not).

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
| T-R1/T-R2 | resampler stopband/ripple/impulse acceptance (§7.7) + block-split bit-identity (§7.2.1 item 9 — **primitive-level invariant**, deliberately stronger than the engine-level L-5/T-E7 audio-equivalence criterion; regression pin on the stateless design, not a normative contract test) | unit |
| T-CV* | curve compiler: TOML-subset parsing, spec validation (T-E15 curve slice: bad TOML / non-finite / unknown keys / missing seed with file+field), exact-formula pins per kind (§4.4.3.1), battery compile determinism (§17 step-2 gate) | unit |
| T-W1..W3 | WAV round-trip (f32/f64, 192 kHz, multichannel extensible); deterministic writer bytes; invalid-input rejection | unit |
| T-C1 | corpus regeneration bit-identity | golden |
| T-M1..Mn | per-metric analytic golden cases (§10.1 last column) + deliberately block-quantised dummy engine with known lag (pitch-lag golden) | unit/golden |
| T-K1 | failure-model fixtures: every §K case triggered by a fixture experiment; class + persistence asserted | failure |
| T-G1 | renderer manifest completeness: all §F.3 fields, length accounting, requested/effective/status on saturated jobs | integration |
| T-G2 | full reproducibility gate: re-run ⇒ byte-identical WAVs, manifest diffs empty (`pitchlab verify`) | integration |
| T-INF1 | freeze-state CI smoke (runs at freeze): toolchain C++20 feature set; FP determinism guards (`__FAST_MATH__` undefined, reproducible double accumulation); registry freeze-state (T-E19); version/phase constants | unit (infrastructure — explicitly NOT DSP correctness) |

### 13.3 CI mapping

CI (canonical, §1.4/build spec) runs at freeze: T-INF1 suite only (infrastructure). At implementation milestones, the same workflow runs the growing suite — the workflow file itself does not change (CTest discovers tests). Perceptual listening tests never run in CI (human layer). Benchmark runs (RTF measurements) are CI-executed measurements recorded as artifacts, not pass/fail gates (except NaN/taint gates). Activation status (2026-09-26): **ACTIVATED and PROVEN** — run 36237247935 green end-to-end (all steps success; CTest 3/3; `ci-evidence` artefact downloaded and content-verified: configure.log, build.log, test.log, JUnit `test-results.xml` with 3 tests / 0 failures); the activation cost two honestly-recorded intermediate failures (GCC drift caught by the anti-drift assertion — re-pinned 13.2.0→13.3.0; sandbox-vs-runner `~/.local` gap — fixed with `mkdir -p`), each diagnosed from live logs and fixed as recorded amendments in build spec v1.1 §12.1, OD-17. The workflow now runs automatically on every push. **Implementation phase (cycle 1, 2026-09-26):** the suite grew to 7 CTest tests — T-INF1 (3, unchanged) + `types_test` (T-T1) + `rng_test` (T-D1/T-D2) + `wav_io_test` (T-W1..T-W3) + `resampler_test` (T-R1/T-R2, including the OD-18 evidence printed into the JUnit output). These are real component-correctness tests (unit level); no engine exists and none is faked (T-E19 registry-empty assertion still green). **Cycle 2 (2026-09-26):** the suite grew to **9 CTest tests** (+ `curve_test` T-CV/T-E15 curve slice, + `corpus_gen_test` T-C1) plus the `corpus_gen` tool target; the committed corpus under `assets/corpus/` rides the same workflow (clean checkout → CTest verifies regeneration against the committed bytes). **Cycle 3 (2026-09-26, §17 step 3):** the suite grew to **14 CTest tests** (+ `hash_test` T-H1 own SHA-256, + `json_writer_test` T-J1 canonical JSON, + `experiment_compiler_test` T-E15 harness slice + skip/saturation semantics, + `varispeed_contract_test` T-E1..T-E11/T-A1/T-D3 through the real harness, + `length_calibration_test` T-E14/T-LEN-CAL evidence pack) plus the `pitchlab` CLI's real `compile`/`render` subcommands. The engine-registry smoke (T-E19) now asserts the intended content: exactly ONE implemented engine (`native.varispeed`); the other four remain unimplemented and unregistered. The T-LEN-CAL evidence prints into the CTest/JUnit output on every CI run (§13.4).

### 13.4 Measured engine test evidence (cycle 3, 2026-09-26) — `EMPIRICAL RESULT`

**T-LEN-CAL (T-E14, OD-6 evidence pack):** native.varispeed (reference preset K=24) over the full 21-curve battery × 6 first-class rates = 126 renders, 0.5-second deterministic input per rate. All 126 renders complete, all |actual − expected| lengths within the provisional ±4096, all flush counts within declared outputLatency. Observed distribution: |Δ| = 0 in 44 cases, 1 in 43, 2 in 30, 3 in 3, 7 in 4, 8 in 2; **worst case 8 frames** (`ramp-fast-plus-12stps` @ 44.1 kHz; worst relative 4.29·10⁻⁴); per-rate worst 7–8 frames. Constant ratios with N divisible by r: |Δ| = 0 EXCEPT r = 0.25 (|Δ| = 2: flush = ceil((K−0.5)/r) = 94 < ceil(K/r) = 96 — the designed arithmetic of flush-count vs declared-bound rounding). The deltas arise from the flush-region rounding (M₀ overshoot δ ∈ [0, r) plus the ceil((K−0.5−δ)/r) vs ceil(K/r_min) comparison) — deterministic arithmetic, not accumulated numerical error. **OD-6 remains OPEN**: the evidence is produced (it re-prints into every CI run via `length_calibration_test`); no observed value has been promoted into `config/tolerances.toml` (asserted by the test); the provisional ±1 block (4096) remains normative-provisional until the owner ratifies.

**T-E7/T-E1 engine-level audio-equivalence (L-5, provisional −80 dBFS):** T-E1 identity max |out−in| measured ~1e-15 (criterion 1e-4); T-E7 cross-schedule max difference ~0 (the engine is bit-identical across block schedules by construction — §6.1.1 item 4's observed stronger property; the CONTRACT assertion remains the −80 dBFS bound, never bit-identity).

**T-E6 determinism:** repeated full compile→render→WAV+manifest runs are byte-identical (fresh roots, path-normalised manifests); the committed example experiment `experiments/suites/example-varispeed-basic.toml` regenerates byte-identical masters, listening copies and manifests (sha256-pinned in the worklog Task 15 entry).

---

## 14. Engine registry — freeze state and v0.1 end state — `DEFINED`

**Registration point:** `src/core/engine_registry.cpp::registerProductionEngines()` — the single authoritative location (§4.6). Adding an engine = editing that one TU + implementing the engine (architecture §D.6); never a config file.

**Freeze state (superseded by cycle 3, 2026-09-26):** the production registration list was EMPTY at freeze (verified by T-E19). **Current state (cycle 3, §17 step 3):** the registry contains EXACTLY the implemented engines — currently one:

| id | UsageClass | role | DurationBehaviour | controlRate | channels | determinism | declared ratio range | bandwidth (nyq. fraction) | parameter keys (v0.1) | status |
|---|---|---|---|---|---|---|---|---|---|---|
| `native.varispeed` | Prototype | reference | RateFollowing | PerSample | MonoAndStereo | Deterministic | [0.0625, 16] | 0.45 + AA notes | `resample_quality`, `allow_aliasing` | **IMPLEMENTED (cycle 3)** |

The remaining four v0.1 engines (§17 step 4) exist in this document only as *specified* entries — no code, no registration (the anti-fake rule: registry content == implemented engines, asserted by T-E19):

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
| OD-6 (arch §N.6) | Rate-following length tolerance VALUE | open; **evidence produced (cycle 3, §13.4)**; owner ratification pending | provisional ±1 block until ratification — T-LEN-CAL measured worst |Δ| = 8 frames over the full battery × rates (distribution in §13.4); nothing promoted into tolerances.toml |
| OD-7 (arch §N.7) | Tier-C adapter build order | open; post-v0.1 | unaffected by freeze |
| OD-8 (arch §N.8) | Registry growth / dynamic loading | open; revisit >20 engines | freeze state: static registry |
| OD-9 (arch §N.9) | All metric tolerance values | open; `REQUIRES EMPIRICAL VALIDATION` | calibration on Tier-A engines; tolerances.toml marked provisional |
| OD-10 (arch §N.10) | Corpus acquisition (real-world) | open; post-v0.1 | §12 schema ready |
| OD-11 (arch §N.11) | >2-channel depth | open | v0.1 tests mono+stereo only |
| **OD-12 (RESOLVED 2026-09-26, owner mandate "PITCH LAB V0.1 IMPLEMENTATION CYCLE 1")** | **Reference pitch tracker (pYIN) under C++-only** — investigated (worklog Task 10-c). No acceptable off-the-shelf C++ pYIN: c4dm/pyin GPL-2+, LibPyin GPL-3, Essentia AGPL-3, aubio GPL-3 (plain YIN), Sleepwalking/libpyin BSD-3 (full pYIN, C99, unmaintained since 2020, minimal API, small algorithmic deviations — build-verified during research). Options were: (A) vendor BSD-3 libpyin+libgvps with validation gate; (B) clean-room C++ pYIN from Mauch & Dixon 2014 + librosa parameters [~800–1200 LOC]; (C) interim plain C++ YIN with documented deviation; (D) defer tracker. **OWNER DECISION: option B — the C++-only clean-room route, as specified.** Tracker-dependent metrics (§10.2) now have their implementation path; the tracker itself is later-cycle work (after the engines/harness, per §17 order — it blocks only metric implementation, not the current cycles) | resolved: clean-room C++ pYIN (B) |
| OD-13 (arch §N.13) | Report output format | open; presentation choice | markdown+CSV recommended by architecture |
| **OD-14 (new)** | WAV >4 GiB (W64) support | open | only if long multichannel 192 kHz masters exceed RIFF limit |
| **OD-15 (new)** | Publishing generated artifacts (e.g. reports) to GitHub as release evidence | open (owner) | replaces Task-9 implicit push (conflict C-2); architecture default: gitignore + regenerate |
| **OD-16 (new)** | Architecture v1.2 change cycle to fold in Amendment A-1 (f64 bus) | queued | text amendment only; no behaviour change vs this spec |
| **OD-17 (RESOLVED 2026-09-26)** | **CI activation on GitHub** — history: PAT initially lacked the `Workflows` permission (push rejected 2026-09-25); owner granted it 2026-09-26, but a workspace reset had destroyed the gitignored local `ci.yml` and `.env` token values. Recovery: workflow re-authored from the canonical spec (tracked in git; empirical `--output-junit` path fix), token restored by the owner (never logged, never committed), commits rebased onto the remote mode-normalised tip (no force-push) and pushed. Activation required two honestly-recorded empirical fixes (GCC re-pin 13.2.0→13.3.0 per run 36237006396; `mkdir -p $HOME/.local` per run 36237155501). **Proof of activation: run 36237247935 (head 593a2d5) — conclusion success, all steps green, CTest 3/3, `ci-evidence` artefact downloaded and content-verified.** Full chronology: build spec §12.1 | resolved; CI runs on every push; scope was T-INF1 infrastructure at activation and now grows with the real test suite (CTest discovery; cycle 1 added T-T1/T-D/T-W/T-R) |
| **OD-18 (new, 2026-09-26 — cycle 1 empirical)** | **Resampler §7.3 constants vs §7.7 kernel-level criteria.** Measured (§7.7.1): kernel stopband floors −75.9/−78.0/−87.3 dB vs the 80/80/90 targets; worst-phase transition widths 31.6/16.0/11.8% of Nyquist vs the 5% criterion; passband ripple and identity criteria MET with large margin; the COMPOSITE §7.4 AA path (the injected-signal reading of §7.7) exceeds 80 dB by ~100 dB. Options: (i) ratify the constants as-is (composite behaviour is the correctness-relevant anchor; also ratify the §7.2.1 item 6 cutoff refinement `c = (r>1) ? 0.95/r : 1`); (ii) raise β ≈ +0.7 per preset for true 80/80/90 kernel floors; (iii) raise K (5%-of-Ny transition at 80 dB ⇒ K ≈ 100+, CPU cost). Not a blocker for §17 step 3 (varispeed): reference preset composite behaviour is the design anchor | open (owner ratification from the §7.7.1 evidence; tests pin the frozen behaviour via regression bands and print the evidence into every CI run) |

**Blocker assessment (task §22 stop conditions):** no stop condition is triggered that blocks *starting* implementation: all public APIs affecting multiple components are specified (§4); ownership/lifecycle/frame/curve semantics defined; build environment defined, locally validated AND CI-activated with a proven green run on the canonical runner (OD-17 resolved 2026-09-26 — run 36237247935); all dependencies declared; the reference tracker route is owner-resolved (OD-12 → clean-room C++ pYIN, 2026-09-26). **Cycle 1 (2026-09-26) implemented §17 step 1 without a stop condition**; one empirical tension was recorded rather than silently resolved (OD-18 — resampler constants vs kernel-level §7.7 criteria; §7.7.1 evidence; not a blocker for step 3). Package state: **READY WITH KNOWN LIMITATION** (§18).

---

## 17. Implementation order and acceptance gates — `RECOMMENDED` (sequencing advice; the scope itself is architecture-mandated)

1. Core types + RNG + WAV I/O + resampler (+ their unit tests T-D/T-W/T-R) — gates: acceptance tests §7.7 pass. **STATUS (cycle 1, 2026-09-26): DONE** — implemented (`src/core/types.h`, `rng.{h,cpp}`, `wav_io.{h,cpp}`, `resampler.{h,cpp}`) with tests `types_test`, `rng_test` (T-D1/T-D2), `wav_io_test` (T-W1..T-W3), `resampler_test` (T-R1/T-R2); local CTest 7/7; §7.7 acceptance measured in §7.7.1 — ripple/identity/DC-Nyquist/composite-AA criteria MET, kernel stopband-depth and transition-width criteria NOT met at the §7.3 recommended constants (recorded as OD-18, owner ratification; regression bands pin the frozen behaviour in CI). The cycle gate is therefore **passed with one owner-ratification item** (nothing silently weakened; no assertion faked).
2. Curve library + validation (T-E15 fixtures) + corpus generator (T-C1) — gate: curve battery compiles deterministically. **STATUS (cycle 2, 2026-09-26): DONE** — implemented: the frozen curve-compiler behaviour was recorded FIRST (§4.4.3.1, before coding: own strict TOML-subset parser `toml_lite` — the v0.1 dependency set stays {doctest}; all kind semantics incl. ramp_lin/reversal linear-in-ratio, ramp_exp geometric endpoint form + the §4.4.3-verbatim multiplicative rate form, log₂-domain LFOs, ZOH clamped random walk, extension policy, saw discontinuity rule); `src/core/curve.{h,cpp}` (parse+validate+compile: ConfigError with file/field on every §4.4.4 violation, unknown-key rejection, post-compilation signal validation); the 21-file curve battery (§11.3/§4.4.3.1 item 14) in `experiments/curves/`; the deterministic corpus generator `src/core/corpus_gen.{h,cpp}` + committed `config/corpus.toml` (masterSeed 0x50E5A1AB, 17 items: all §11.2 classes instantiated; "mono and stereo variants where meaningful" = the two stereo-noise classes are stereo, all others mono — recorded, not silent) + `tools/corpus_gen` (generate / `--verify` byte-identity regeneration gate, §11.1); the corpus committed under `assets/corpus/syn-*/` (float64 WAVs + §15.5 metadata.toml per item, ~50 MB, version-pinned by git). Tests: `curve_test` (T-CV0..T-CV12: TOML-subset matrix, T-E15 curve-slice failure matrix — 28 cases with field assertions, exact-formula pins per kind per §4.4.3, battery compile ×2 rates × determinism, = 17 doctest cases / >1.05 M assertions) and `corpus_gen_test` (T-C1: same-binary regeneration bit-identity, committed-corpus bit-exact equality, itemSeed golden, metadata §15.5 fields, −12 dBFS true-peak normalisation, recipe sanity probes, config validation matrix). Local CTest 9/9. Gate verdict: **passed** — the curve battery compiles deterministically (bit-identical recompilation at 44.1/48 kHz); corpus regeneration is byte-identical (tool `--verify` + in-test). The T-C1 committed-corpus comparison is bit-exact on the generating toolchain; if a future runner libm disagrees it will fail loudly (that is the gate working, §7.5 same-binary stance).
3. Engine registry mechanism + harness skeleton (compiler → renderer → analyzer → reporter, CLI) with the varispeed engine first — gates: T-E1..T-E11 + T-LEN-CAL evidence pack for OD-6. **STATUS (cycle 3, 2026-09-26): DONE** — implemented: the engine contract (§4.2 + frozen implementation behaviour §4.2.1: EngineException, ProcessContext with the effective curve view, sticky inputExhausted semantics, registry factory binding with the anti-fake null-factory rejection, capabilities' supportedSampleRates, the renderer out-capacity policy); `native.varispeed` (§6.1 + frozen §6.1.1: read-position recurrence, whole-curve-max cutoff + streaming AA pre-filter with FIR-state, strict-delivery gating, bounded flush by construction, sliding-window state with no per-call allocation) — the FIRST real engine, registered at the single production point (registry content == 1 engine; the other four unimplemented and unregistered); the harness (§4.8 + frozen §4.8.1: ExperimentCompiler with strict §15.4 validation + visible skips + creative saturation, OfflineRenderer executing the §4.3.2 loop with block schedules/stall guard/flush enforcement/NaN scan/length policy, canonical-JSON manifests with SHA-256 provenance, CLI compile/render, deterministic byte-identical regeneration); own SHA-256 + canonical JSON writer primitives (dependency set stays {doctest}). Tests: `hash_test` (T-H1 NIST vectors), `json_writer_test` (T-J1), `experiment_compiler_test` (T-E15 harness slice + T-E16/T-E17 semantics + skip matrix), `varispeed_contract_test` (T-E1..T-E11 through the real harness + T-A1 allocation audit + T-D3 reset reuse), `length_calibration_test` (T-E14/T-LEN-CAL evidence — §13.4). Local CTest 14/14 (~34 s); full suite clean under ASAN+UBSAN. Gate verdict: **passed** — T-E1..T-E11 green with the engine-level −80 dBFS audio-equivalence criterion (observed: bit-identical outputs, reported not required), T-LEN-CAL evidence produced (worst |Δ| = 8 frames, §13.4; OD-6 remains open for ratification — nothing silently promoted). Analyzer/reporter subcommands and the four remaining engines are §17 step-4/5 work (honest "not implemented yet").
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
| Environment: canonical CI, dependencies, no ambient state, reproducibility | COVERED (OD-17 resolved — CI activated and proven) | build/CI spec v1.1; workflow executed green on the canonical runner (run 36237247935: T-INF1 3/3, evidence artefact content-verified); GCC re-pinned 13.3.0 from live evidence |
| Testing: matrix, deterministic fixtures, CI path | COVERED | §13 |
| Documentation: specs committed, worklog updated, open decisions recorded, contradictions recorded | COVERED | this document + build spec + worklog Task 10 |
| Recovery: repository state recoverable, artefacts committed, no chat-only knowledge | COVERED | all decisions are in repository documents; git state at a known commit; worklog current-state block |

**Package state: `PACKAGE READY WITH KNOWN LIMITATION`.** Known limitations: OD-6/OD-9 tolerance values provisional (calibration procedures defined); OD-12 reference tracker undecided (options + recommendation recorded); Amendment A-1 pending architecture v1.2 fold-in (OD-16); OD-15 artifact publication policy. Nothing required to *start* implementation is undefined.

**Exact next implementation action:** owner resolves OD-12 (tracker option A/B/C/D) → implementation begins at §17 step 1 (core types + RNG + WAV I/O + resampler with acceptance tests), closing each cycle per the Operating Principles master lifecycle.
