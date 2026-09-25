# PITCH LAB — Architecture & Design Document

**Document status:** DESIGN ONLY — not implemented. No code in this document is final or built; interfaces are architectural contracts, not implementations. Per Operating Principles §81, the status chain of everything in this document is **DESIGNED** — nothing here is implemented, tested, verified, integrated or released.
**Version:** v1.1 (architecture correction pass over v1.0; v1.0 archived at `research/archive/pitch-lab-architecture-design-v1.0.md`)
**Date:** 2026-09-25
**Author role:** senior audio DSP architect / software engineer (AI-assisted)
**Governing document:** *AI Assisted Software Engineering Operating Principles v3.0* — **AVAILABLE** and read in full (130 sections); stored at `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md`. The principles govern the *engineering process*; this document governs *Pitch Lab architecture*. The v1.0 open issue (governing document unavailable) is **closed**: the reconciliation against the full principles is recorded in §P.

**Primary upstream input:** `research/doppler-whip-pitch-research-report.md` (unified research report, same repository). Statements inherited from it are tagged **[RESEARCH FINDING]**. Statement classification used throughout this document:

| Tag | Meaning |
|---|---|
| **[RESOLVED DESIGN]** | Decision made in this document, justified, safe for the implementation agent to follow |
| **[OPEN DECISION]** | Genuinely undecidable from current requirements; listed in §N; must not be decided silently by the implementer |
| **[ASSUMPTION]** | Environmental/organisational assumption; if false, the affected design point must be revisited |
| **[RESEARCH FINDING]** | Inherited verified/inferred claim from the research phase (see the research report's own tag system) |
| **[IMPLEMENTATION DETAIL]** | Detail deliberately left to the implementation agent; principle and constraints given here |

## Version history / change record (v1.0 → v1.1)

Per Operating Principles §12/§14, every meaningful change is recorded with reason, scope, affected sections and validation. This is a **targeted correction pass**: no redesign, no new features, no implementation. Valid v1.0 decisions are preserved unless explicitly listed here.

| # | Change (v1.0 → v1.1) | Reason | Affected |
|---|---|---|---|
| 1 | **Rate-following / varispeed contract made explicit:** the block exchange must represent input frames consumed, output frames produced, output-timeline progress, and end-of-render semantics (end-of-input notification + bounded flush). Canonical output length for `Preserving` engines amended from "output == input" to "input + declared output latency" (resolves a v1.0 internal contradiction between the length policy and the no-self-compensation latency rule). Worked example added. Exact C++ signatures remain implementation detail; semantics are normative. | Correction item 1; implementers would otherwise have to invent output-length and flush behaviour (Operating Principles §10) | §A, §D, §D.1, §D.4, §E, §G, §H, §K, §L, §N, §O |
| 2 | **One authoritative engine registry:** engine identity (stable id, implementation binding, display name, version, usage class, licence/origin, capabilities) is defined in **code, at the single compile-time registration point**. The v1.0 "static list in `config/engines.toml` reflected in code" dual representation is removed. Config files hold tunable parameters/defaults only, never engine identity. Unknown engine id in an experiment = CONFIG ERROR. | Correction item 2; dual source of truth violates Operating Principles §2/§80 | §A, §B, §D.6, §J, §K, §N, §O |
| 3 | **Source vs generated tree separation:** generated outputs move under `artifacts/` (`artifacts/renders/`, `artifacts/analysis/`, `artifacts/reports/`); the optional Python analysis source moves to `src/analysis-py/`. The confusable pair `src/analysis/` vs top-level `analysis/` no longer exists. | Correction item 3; ambiguous directory names (Operating Principles §38 terminology control) | §A, §B.2, §B.3, §C, §F.1, §F.4, §H, §G.4 |
| 4 | **Creative-mode ratio semantics:** creative mode distinguishes **requested ratio** / **effective ratio** / **execution status**. Out-of-range creative jobs render against a harness-saturated effective curve; the manifest preserves the requested curve, the effective curve, and a status (e.g. `RANGE_SATURATED`). Engines never silently clamp. Benchmark mode remains strict: out of range → skip. | Correction item 4; silent information loss violates Operating Principles §79 | §E, §F.4, §G.2, §K, §L, §N, §O |
| 5 | **Operating Principles v3.0 reconciliation performed and recorded** (§P). v1.0 OPEN ISSUE #0 and assumption A5 closed. | Correction item 5 | header, §0.4, §P, §N, closing |
| 6 | **v0.1 implementation scope made explicit and binding** (§0.5): shared resampling primitives + Varispeed + Variable Delay + Classic PV + Phase-Locked PV + Granular, plus the harness itself. All other engines (WSOLA, TD/FD-PSOLA, SMS, WORLD, HNM, neural, external adapters) and all future systems are out of v0.1. | Correction item 6; scope control (Operating Principles §32) | §0.2, §0.5, §D.6, §N, §O |
| 7 | Section cross-reference repair: v1.0's `DurationBehaviour` comment pointed to "D.4" (which was the rejected-fields section). v1.1 inserts the real duration contract as **§D.4** and renumbers the previous D.4/D.5/D.6 to D.5/D.6/D.7. | Documentation consistency (Operating Principles §37) | §D.4–D.7, §O.14 |
| 8 | Terminology: "clamp" reserved for the *forbidden* silent behaviour; curve-authoring LFO bounds renamed "min/max bounds". | Operating Principles §38 | §E.2, §F.4, §D.3 |

Correction-pass constraint honoured: **no new components, managers, abstraction layers, databases, servers, plugin frameworks, plugin scanning, dynamic loading or GUI architecture were introduced.** All fixes operate on existing components (compiler, renderer, analyzer, reporter, passive registries) and existing files/trees. The project remains a local offline research tool.

---

## 0. Scope, purpose, non-goals

### 0.1 Purpose (restated from requirements)

Pitch Lab is a **local DSP research laboratory** for sound-design-oriented pitch processing. Its product is **knowledge and tested DSP components**, not shippable audio software. It must support:

1. **Technically controlled benchmarks** — identical inputs, identical pitch trajectories, multiple engines, measured results.
2. **Deliberately creative / unstable experimentation** — extreme ratios, unstable modulation, aggressive parameters, where "artefacts" may be the goal.

It must distinguish **technical correctness**, **perceptual quality**, and **creative usefulness** — and must never collapse them into one score. **[RESOLVED DESIGN]**

### 0.2 Non-goals (binding)

Explicitly out of scope for the first implementation, per the task and the operating principles:

- the VST3 product itself, any plugin wrapper, any DAW integration
- a polished GUI (a minimal file/CLI workflow only)
- implementation of the physical Doppler / propagation / shock / whip / spatialisation / feedback systems
- real-time audio playback in v1 (deterministic offline renderer first)
- AI/neural pitch processing
- premature optimisation
- silently deciding unresolved product behaviour

The architecture must nevertheless **not block** any of these (see §M). **[RESOLVED DESIGN]**

The concrete first-phase engine scope is defined in §0.5; the non-goals above apply to the whole project lifetime, the §0.5 list to the v0.1 implementation phase.

### 0.3 Governing separation from physical Doppler

Pitch Lab models **musical/engineering pitch processing**: transforms driven by a **pitch-ratio control curve** that either preserve duration (duration-preserving processors) or follow the rate (varispeed family — §D.4). The physical Doppler/propagation system is a **duration-modulating, physics-driven** process and belongs to a separate future component with a **different contract** (research report §B.3.3). Pitch Lab must support *Doppler-like pitch curves* (arbitrary time-varying ratio) but must **not** model velocity, Mach numbers, or propagation. No Doppler concept appears in any Pitch Lab API. **[RESOLVED DESIGN — boundary inherited from [RESEARCH FINDING])**

### 0.4 Assumptions (explicit, revisited if false)

| # | Assumption |
|---|---|
| A1 | Single user, single workstation, offline operation; no concurrent writers to the project tree |
| A2 | A C++20 toolchain (clang/gcc/MSVC) and CMake are available on the development machine |
| A3 | Git is used; the renderer stamps outputs with the source version (fallback: static version file if git unavailable) |
| A4 | Test corpus assets will be authored/obtained/licensed later; some categories (whoosh, whip) require the measurements defined in the research report §A.6.3 |
| A5 | Python 3.11+ is available for the optional offline analysis layer (fallback: pure-C++ analysis path is designed-in as an alternative) |

*(v1.0 assumption A6 — "the Operating Principles file will be re-supplied" — is closed: the file is now stored in the repository and the reconciliation is recorded in §P.)*

### 0.5 Implementation scope — v0.1 (binding)

The architecture supports many future engines, but the **first implementation phase is explicitly limited to**:

```text
v0.1 IN SCOPE
  1. shared resampling primitives (band-limited resampler core, shared by engines
     and by the varispeed reference role)
  2. native.varispeed            (rate-following; harness reference engine)
  3. native.vardelay             (variable delay)
  4. native.pv.classic           (classic phase vocoder)
  5. native.pv.phaselocked       (phase-locked phase vocoder)
  6. native.granular             (granular pitch)
  + the harness itself: ExperimentCompiler, OfflineRenderer, Analyzer, Reporter,
    CLI, engine registry (in-code), curve library, C++ metric modules required
    for §L contract tests and §G suite execution
```

**Do NOT implement in v0.1** (remain future/prototype scope; their architecture in this document is retained but unimplemented):

- WSOLA, TD-PSOLA, FD-PSOLA, SMS, WORLD, HNM, neural methods
- Tier-C external adapters (Signalsmith, pbshift, Rubber Band, SoundTouch) — the adapter *architecture* (§D.2) stays designed; no adapters are built in v0.1
- Doppler, propagation, shock, spatialisation, feedback
- VST wrapper, polished GUI

**[RESOLVED DESIGN — correction item 6]** The engine contract, registry and harness methodology are unchanged by this scope limit: later engines are added by registering them at the compile-time registration point (§D.6) — **without changing the test-harness contract, the engine contract, or any methodology**. Scope creep past §0.5 requires an explicit owner decision.

---

## A. Executive architecture

```text
                ┌─────────────────────────────────────────────────────────┐
                │  AUTHORITATIVE INPUTS (files under version control)     │
                │  experiments/*.toml   assets/corpus/**   config/*.toml  │
                │  (experiment defs, corpus + metadata, harness defaults) │
                └────────────────────────────┬────────────────────────────┘
                                             ▼
                                   ExperimentCompiler
                        (matrix expansion: inputs × curves × engines
                         × configs × sample rates; capability filtering
                         with recorded skip reasons; creative-mode
                         ratio saturation with requested/effective record)
                                             ▼
        ┌───────────────────────── RenderJob queue ─────────────────────────┐
        │  per job: InputAsset, PitchCurveSignal (requested; effective if   │
        │  saturated), EngineConfiguration, seed, sample rate, channels,    │
        │  destination, provenance                                          │
        └───────┬───────────────────────────────────────────────┬───────────┘
                ▼                                               │
        OfflineRenderer (deterministic, block-streaming)        │  skipped jobs
        │  drives PitchEngine via the engine contract           │  recorded with reason
        │  owns latency accounting, length policy per           ▼
        │  DurationBehaviour (input-consumed / output-produced  (no render)
        │  accounting, end-of-input + flush), NaN/Inf scanning,
        │  manifest emission
                ▼
        RenderResult  =  artifacts/renders/**.wav  +  artifacts/renders/**.manifest.json
                ▼
        Analyzer (independent stage; metric modules as pure functions;
         optional Python offline trackers for reference pitch analysis)
                ▼
        AnalysisResult  =  artifacts/analysis/**.json
                ▼
        BenchmarkRun aggregator (comparison matrices; never mutates results)
                ▼
        Report  =  artifacts/reports/**   (tables/plots + listening indexes)
```

**Engine side (compiled into the same binary, isolated by directory and contract):**

```text
PitchEngine (abstract contract — §D)
├── engines/native/          (own implementations, Tier A/B)
│    varispeed · variable_delay · phase_vocoder · phase_locked_pv ·
│    granular · wsola · td_psola · fd_psola · transient_pv · sinusoidal · sms
└── engines/external/        (adapters only — no third-party code in native/)
     ├── linked adapters     (permissive-licence engines: Signalsmith, pbshift)
     └── subprocess adapters (copyleft engines: Rubber Band, SoundTouch —
                              isolated via CLI process boundary)
```

The **engine registry is the single authoritative engine identity source** (stable id, implementation binding, display name, version, usage class, licence/origin, capabilities), defined **in code at one compile-time registration point** (§D.6). It is part of the binary, not a configuration file. Every engine carries a **UsageClass** ∈ {`prototype`, `benchmark-only`, `external-integration`, `external-benchmark`, `research-reference`, `rejected`, `future`}; the class is recorded in the registry and in every manifest; it constrains nothing at runtime (a `benchmark-only` engine still renders) but is **authoritative metadata for interpretation and for what may later enter the VST**. **[RESOLVED DESIGN — correction item 2]**

**Four components, no more:** ExperimentCompiler, OfflineRenderer, Analyzer, Reporter — plus the passive registries (corpus, curves, engines). No GUI, no server, no database, no manager classes beyond these. **[RESOLVED DESIGN — minimal abstraction per operating principles]**

## B. Component model, ownership, project layout

### B.1 Components and responsibilities

| Component | Responsibility (complete list) | Explicitly NOT responsible for |
|---|---|---|
| **CorpusRegistry** | Enumerate InputAssets from `assets/corpus/**` + metadata files; validate metadata; serve paths & hashes | Creating, editing or resampling assets; judging asset quality |
| **CurveLibrary** | Parse PitchCurveSpec files; validate; compile spec → dense PitchCurveSignal at a requested control rate (deterministically, with seed) | Knowing anything about engines; smoothing (engine's business) |
| **EngineRegistry** | **The single authoritative engine identity registry**, defined in code at one compile-time registration point (§D.6); expose EngineInfo/Capabilities; bind stable id → implementation; construct engine instances | Testing engines; enforcing capabilities at runtime (harness does that); being configurable (identity is not configuration) |
| **ExperimentCompiler** | Parse experiment TOML; expand the test matrix; apply capability filters with recorded skip reasons; perform creative-mode ratio saturation (producing the effective curve + status record); emit RenderJob list + job plan manifest | Rendering; analysing; inventing missing test definitions (missing input = error, not default) |
| **OfflineRenderer** | For each RenderJob: instantiate engine, configure (params+seed), prepare (fs/channels/block), block-stream the input, drive the engine with the curve view, account **input frames consumed / output frames produced / end-of-input + flush** per §D.4, enforce output length policy per DurationBehaviour, account latency, scan output for NaN/Inf, write WAV + manifest | Metric computation; comparing engines; deciding skip policy (compiler decided it) |
| **Analyzer** | Read RenderResults (files only); compute metric modules as pure functions over (reference, output, context); write AnalysisResults | Touching engines; re-rendering; mutating results |
| **Reporter** | Aggregate AnalysisResults into BenchmarkRuns, comparison tables, listening indexes | Scoring; ranking beyond presenting measurements |
| **CLI (`pitchlab`)** | Entry points: `compile`, `render`, `analyze`, `report`, `verify`, `listen-index`; registry introspection (e.g. listing engines and their capabilities — read-only) | Business logic (all logic lives in the components) |

### B.2 Ownership matrix (no shared mutable state)

| State | Owner | Mutability | Persistence |
|---|---|---|---|
| Input assets + metadata | CorpusRegistry (read-only) | immutable | `assets/corpus/` (authoritative files) |
| Pitch curve specs | CurveLibrary (read-only) | immutable | `experiments/curves/` (authoritative) |
| Pitch curve signals (compiled) | ExperimentCompiler → Renderer | ephemeral, derived | derivable; never persisted as truth |
| **Engine registry (identity, capabilities, licence/origin metadata)** | **code — compile-time registration point; nobody mutates it at runtime** | **immutable per build** | **in-binary; mirrored into every manifest** |
| Engine instances + internal DSP state | OfflineRenderer, for the duration of one RenderJob | mutable, job-local | never persisted |
| Engine configurations | defined in experiment files (authoritative) | immutable | `experiments/` |
| Random seeds | defined in experiment/TestCase files (authoritative) | immutable | `experiments/` + copied into manifests |
| RenderJob queue | ExperimentCompiler → Renderer (handoff) | append-only during run | job plan manifest (generated) |
| Render buffers | Renderer, per job | ephemeral | none |
| WAV outputs + manifests | Renderer emits; **nobody owns after emission** (immutable artifacts) | immutable | `artifacts/renders/` (generated) |
| Analysis results | Analyzer emits; immutable artifacts | immutable | `artifacts/analysis/` (generated) |
| Experiment definitions | The user (via files) | authoritative | `experiments/` |
| Configuration defaults (parameters only — **never engine identity**, §D.6) | `config/` files | authoritative | `config/` |

**Rule:** generated trees (`artifacts/renders/`, `artifacts/analysis/`, `artifacts/reports/`) are **never** source of truth; deleting them and re-running `pitchlab render/analyze/report` must reconstruct them bit-for-bit for deterministic engines (§L). **[RESOLVED DESIGN]**

### B.3 Project layout (proposed root: `pitch-lab/`)

```text
pitch-lab/
  config/                 # authoritative: harness defaults, metric tolerances,
                          #   per-engine tunable parameter defaults — NOT engine
                          #   identity (identity lives in code, §D.6)
  docs/                   # authoritative: this design doc + research links
  experiments/            # authoritative: experiment definitions (TOML)
    curves/               #   pitch curve specs (TOML) + optional curve data (CSV)
    suites/               #   benchmark suites (the §10 battery as files)
    creative/             #   creative experiments (namespaced)
  assets/
    corpus/               # authoritative: test inputs + per-asset metadata.toml
    reference-notes/      # authoritative: notes on what counts as reference and why
  src/
    core/                 # contract, curve lib, renderer, compiler, harness, CLI,
                          #   shared DSP primitives (incl. resampler core)
    engines/native/       # own engines (one dir per engine)
    engines/external/     # adapters ONLY (linked + subprocess)
    analysis/             # C++ metric modules (SOURCE)
    analysis-py/          # OPTIONAL Python offline analysis layer
                          #   (SOURCE, dev-only, isolated)
  external/               # vendored third-party sources, one dir per library,
                          #   each with LICENSE.txt copied verbatim + ORIGIN.toml
  scripts/                # bootstrap, build, run-suite, verify-reproducibility
  tests/                  # unit/component/renderer/failure test sources
  artifacts/
    renders/              # GENERATED (gitignored; reconstructable)
    analysis/             # GENERATED
    reports/              # GENERATED
```

**[RESOLVED DESIGN — correction item 3]** — Source code lives under `src/` (including `src/analysis/` and `src/analysis-py/`); generated artifacts live under `artifacts/` (including `artifacts/analysis/`). The v1.0 confusable pair `src/analysis/` vs top-level `analysis/` no longer exists; source and generated trees can never be mistaken for each other by name. Exact file formats inside `experiments/` are sketched in §F; schemas are authoritative here, syntax details are **[IMPLEMENTATION DETAIL]**.

---

## C. Data flow (an experiment moves through the system)

1. **Author** writes `experiments/suites/dynamic-basic.toml`: selects inputs (by asset id), curves (by spec id), engines (**by registry id — the registry is in code, §D.6**) or engine groups, parameter sets, sample rates, seed, kind = benchmark.
2. **`pitchlab compile`** → ExperimentCompiler loads corpora/curves/engines; validates everything it can **before** any DSP runs (missing asset → hard error with asset id; **unknown engine id → hard error** "engine `<id>` referenced but not registered"; ratio out of engine range → job skip recorded with reason `ratio-out-of-range` — or, in creative mode, ratio saturation per §F.4; unsupported fs/channels → skip reason); emits `artifacts/renders/<run-id>/jobplan.json` with the full job list + skip log.
3. **`pitchlab render`** (consumes jobplan) → for each job: build engine, configure with parameters + seed, prepare with (fs, channels, harness block size), then block-stream: read input block → advance curve view → `process()` → collect output block → account consumed/produced frames per §D.4 → at end of input, notify the engine and collect the bounded flush → enforce length policy → NaN/Inf scan. Emits `artifacts/renders/<run-id>/<engine>/<input>__<curve>__<fs>__<paramstag>/<...>.wav` + `...manifest.json` (provenance: asset hash, curve spec hash + signal hash — **requested and effective, if saturated** — engine id+version, params, seed, git commit, config hash, host info, length accounting per §D.4).
   - Varispeed reference renders are produced by the same path (the harness's reference engine is just another registered engine) — one per (input, curve, fs) — and tagged `reference` in the manifest.
4. **`pitchlab analyze`** → Analyzer loads each RenderResult + its reference pairing from the run's job plan; computes metric modules; writes `artifacts/analysis/<run-id>/...json` mirroring render paths.
5. **`pitchlab report`** → aggregates to `artifacts/reports/<run-id>/`: comparison tables (engines × metrics × material classes), skip log, listening index (grouped A/B/C render sets with identical input/curve — §G.4).
6. **Human listens / reviews.** The render identity chain (manifest) answers: which algorithm, which parameters, which input, which curve, which sample rate, which seed, which code version. **[RESOLVED DESIGN — requirement §17]**

Failure branches: see §K. Every failure is classified and persisted; nothing is silently dropped.

---

## D. Pitch engine contract (architecture only — C++ sketch, NOT implementation)

```cpp
// engine/PitchEngine.h  — the single contract every engine family implements.
// Architecture-level sketch: exact signatures may be adjusted by the implementer
// within the constraints marked RESOLVED below. [IMPLEMENTATION DETAIL] applies
// to syntax, not semantics. In particular, the process() form shown below is the
// minimal SYMMETRIC block exchange (input count == output count per call) and is
// NOT normative for RateFollowing engines: the final API must express the block
// exchange semantics of §D.4 (input frames consumed, output frames produced,
// end-of-input notification, bounded flush). The semantics are binding; the
// signatures are deliberately not frozen in this document.

enum class UsageClass { Prototype, BenchmarkOnly, ExternalIntegration,
                        ExternalBenchmark, ResearchReference, Rejected, Future };
enum class DurationBehaviour { Preserving, RateFollowing };   // see D.4
enum class ChannelMode { Mono, Stereo, MonoAndStereo, MultiChannel };
enum class Determinism { Deterministic, SeededDeterministic }; // Nondeterministic is FORBIDDEN

struct ControlRateSpec {
    enum Kind { PerSample, FixedBlock, EngineEvent } kind;
    int blockFrames = 0;        // for FixedBlock; engine-declared, harness-verified
    // EngineEvent: engine pulls curve samples at its own cadence (per hop/grain/
    // period) — the harness supplies a sampleable view and LOGS the declared rate.
};

struct BandwidthSpec {
    // Declared honest bandwidth: fraction of Nyquist, or absolute Hz at a given fs.
    double nyquistFraction;     // e.g. 0.45, 0.95
    // Engines that alias above some ratio must say so here (honesty over marketing).
    std::string notes;
};

struct EngineInfo {
    const char* id;             // stable registry id, e.g. "native.pv.phaselocked"
    const char* displayName;
    const char* version;        // engine's own version string
    UsageClass  usageClass;
    const char* origin;         // "own implementation" | adapter target + version
    const char* license;        // recorded status from research (re-verify before impl.)
};

struct Capabilities {
    double      minRatio, maxRatio;      // no global ceiling imposed by the harness
    bool        supportsDynamicRatio;    // false => static-ratio engines still welcome
    ControlRateSpec controlRate;         // §9: per-sample / per-block / per-event
    ChannelMode channelMode;
    int         maxChannels;
    BandwidthSpec bandwidth;
    DurationBehaviour duration;          // §D.4
    Determinism determinism;             // all engines must be reproducible (§14)
    // NOTE: material-suitability flags (voice/polyphonic/noise/transient) are
    // DESCRIPTIVE metadata stored in the registry, NOT part of this contract —
    // see D.5 for the justification (they must not become architectural gate-keeping).
};

struct LatencyInfo { int64_t inputLatencyFrames; int64_t outputLatencyFrames; };

struct PitchCurveView {
    const double* ratio;        // dense ratio signal, indexed on the INPUT timeline
                                // (one ratio value per INPUT audio frame; see §D.4)
    int64_t       frames;       // == input frames
    double        sampleRate;   // of the AUDIO (curve is indexed per audio frame)
    // Engines with EngineEvent control pull arbitrary indices via the view's
    // sampler (linear interpolation at declared boundaries; no engine may
    // extrapolate beyond the view — clamp + report, see §K).
};

struct ProcessContext {
    double sampleRate; int channels; int maxBlockFrames;
    int64_t totalFrames;         // whole render length (offline: known) — INPUT frames
};

struct EngineConfiguration {      // opaque, engine-defined parameter set,
    // serialised verbatim into manifests; harness validates only seed presence.
    // contains: named parameters (strings/numbers), seed (uint64, REQUIRED)
};

class PitchEngine {
public:
    virtual ~PitchEngine() = default;
    virtual EngineInfo     info() const = 0;
    virtual Capabilities   capabilities() const = 0;
    virtual LatencyInfo    latency() const = 0;          // after prepare(); constant per job
    virtual void configure(const EngineConfiguration& cfg) = 0;   // params + seed
    virtual void prepare(const ProcessContext& ctx) = 0;          // allocate, reset state
    virtual void process(const float* const* in, float* const* out,
                         int frames, const PitchCurveView& curve,
                         int64_t frameIndex) = 0;         // planar, channel-major;
                                                          // shown form = symmetric
                                                          // exchange; see §D.4 for
                                                          // the binding block-exchange
                                                          // semantics for both
                                                          // DurationBehaviour classes
    virtual void reset() = 0;                            // clears DSP state, keeps config
    // End-of-input notification and flush: whatever the final call shape is
    // (a finish()/flush() method, a flag on the last process() call, or an
    // equivalent mechanism — [IMPLEMENTATION DETAIL]), the engine MUST be
    // informed that the input stream is complete and MUST then emit its
    // remaining bounded output (see §D.4). The mechanism is not frozen here;
    // the semantics are binding.
};
```

### D.1 Resolved contract semantics

- **Planar float processing.** Buffers are non-interleaved, channel-major. Internal compute precision is the engine's choice, but the I/O bus is `float` (32-bit); research-grade double-precision variants are a per-engine internal decision. **[RESOLVED DESIGN]** — *alternative considered and rejected: double I/O bus (rejected: diverges from future VST reality; precision analysis happens inside engines/analyzers).*
- **Block streaming.** `process()` is called with arbitrary block sizes ≤ `maxBlockFrames`, in order, exactly once per frame range, `frameIndex` monotonically increasing. Engines must tolerate any block boundary. **[RESOLVED DESIGN — VST-donation requirement]**
- **Block-exchange accounting (both duration classes).** Whatever the final signatures, every block exchange must make explicit — per call and cumulatively — **(a) input frames consumed, (b) output frames produced, (c) whether the input stream has ended**. The renderer accumulates these as the render's progress accounting (the output-frames-produced counter *is* the output-timeline progress) and records the totals in the manifest. A per-call equality of input and output counts is required only for `Preserving` engines at steady state (see §D.4). **[RESOLVED DESIGN — correction item 1]**
- **Determinism.** Every engine must be reproducible: given identical (input, config incl. seed, fs, channels, block schedule), output must be bit-identical. RNG must come from the injected seed via a harness-provided deterministic generator (own PCG64 implementation — no `<random>` device entropy). **[RESOLVED DESIGN — §14 mandate]** Block-size independence of *audio content* is a target contract for native engines (see §L, with the caveat that bit-exactness across different block schedules is a **[OPEN DECISION]** — audio-equal within tolerance vs bit-exact; bit-exact is required only for the *same* schedule, which is what reproducibility demands).
- **The engine owns ONLY its processing behaviour.** It must not know: test methodology, metrics, benchmark state, corpus semantics, file paths. `PitchEngine` has no hooks for any of that. **[RESOLVED DESIGN — §2 mandate]**
- **Latency.** Reported once after `prepare()`, in frames. The harness uses it for alignment in analysis and records it; engines do not self-compensate. **[RESOLVED DESIGN]**
- **Duration behaviour** — Preserving vs RateFollowing, including length policy, end-of-render and output-timeline semantics: **defined normatively in §D.4**. **[RESOLVED DESIGN]**
- **Static-ratio engines** (`supportsDynamicRatio == false`) receive the curve like everyone else; they may sample it at their own (e.g. initial) value; the harness's benchmark marks the effective control rate in analysis. No special API. **[RESOLVED DESIGN — methodology equality]**

### D.2 Adapter semantics (external engines)

- **Linked adapters** (permissive licences only): adapter implements `PitchEngine` by calling the external library; adapter owns all glue; external code stays in `external/`, never in `engines/native/`. Adapter declares the external's true capabilities honestly (including "control updates at block rate N").
- **Subprocess adapters** (copyleft engines — Rubber Band, SoundTouch): the adapter writes the input WAV + a control sidecar to a temp dir, invokes the vendor CLI (rendering the *static* or *block-quantised* ratio timeline it supports), reads back the output WAV, and reports honest capabilities (e.g., `FixedBlock` control with the tool's block size, or static-only). **License isolation is the reason** (GPL/LGPL code must never be linked into the Lab binary; the CLI boundary keeps the Lab proprietary-clean). **[RESOLVED DESIGN — inherited from [RESEARCH FINDING] licensing matrix]**
- Adapters record the external version in `EngineInfo` and fail with `external-dependency-unavailable` if the binary/library is missing (§K). **[RESOLVED DESIGN]**
- Subprocess adapters for rate-inconsistent externals must map the external's actual consumption/production behaviour onto the §D.4 accounting honestly (e.g. a duration-preserving CLI library is a `Preserving` engine; a CLI that changes duration is `RateFollowing` even if its ratio semantics are the tool's own — the adapter documents the mapping in the manifest). No adapter may hide a duration change. **[RESOLVED DESIGN — correction item 1 consistency]**
- All adapters are **post-v0.1** scope (§0.5): the adapter architecture is designed here, not implemented in the first phase.

### D.3 Capability model — which fields are architecturally necessary and why

| Field | Necessity |
|---|---|
| `minRatio / maxRatio` | **Necessary.** The compiler must skip out-of-range benchmark jobs *without rendering* (test integrity: rendering a silently-clamped ratio would falsify results) and record the skip; in creative mode the same declaration drives the explicit saturation of §F.4. No *global* range exists — each engine declares its own. |
| `supportsDynamicRatio` + `ControlRateSpec` | **Necessary.** §4/§9 require the harness to test the *difference* between per-sample / per-frame / per-hop / per-grain control. The declared spec is also what the analyzer's *measured* control rate gets compared against. |
| `ChannelMode / maxChannels` | **Necessary.** Corpus contains stereo + mono; jobs must be skippable when an engine cannot process the layout. |
| `BandwidthSpec` | **Necessary.** §6: no fixed 20 kHz ceiling; the benchmark must verify *declared* bandwidth against measured bandwidth (aliasing indicator). Honesty field — an engine may declare "aliases above 0.5 Nyquist at ratios > 2"; declaring a limitation is fine, hiding it is not. |
| `DurationBehaviour` | **Necessary.** Output-length policy differs (§D.4: varispeed reference and all rate-followers). |
| `Determinism` | **Necessary.** §14 reproducibility mandate. |

### D.4 Duration behaviour contract — Preserving vs RateFollowing (normative semantics)

**[RESOLVED DESIGN — correction item 1]** This section defines the *semantic* contract; the exact C++ method signatures are **[IMPLEMENTATION DETAIL]** and are deliberately not frozen in this document. The semantics below are binding.

#### D.4.1 What the contract must represent (both classes)

1. **Input frames consumed** — per block exchange and cumulatively.
2. **Output frames produced** — per block exchange and cumulatively. This counter is the **output-timeline progress** of the render (used for NaN-scan positions, latency accounting, taint positions, and reporting).
3. **End-of-input notification** — the engine is explicitly informed when the input stream is complete (mechanism unspecified; semantics binding).
4. **Bounded flush** — after end-of-input, the engine emits its remaining output, bounded by its declared `outputLatencyFrames`. A Preserving engine that cannot fill its canonical length, or a RateFollowing engine that continues producing past its bounded flush, is a failure (§K).
5. **Input↔output correspondence ("emission map")** — for RateFollowing engines, the mapping between output position and input read position is **deterministically derivable** from the input-indexed curve and the declared latency; the harness computes the *expected* map and length, and the manifest records everything needed to reconstruct it (curve hash, lengths, latency). The harness does not need the engine to report a per-frame map; conformance is checked via the produced length (§D.4.3) and measured by analysis (§H).

#### D.4.2 Curve indexing and rate-follower consumption semantics

The dense `PitchCurveView` is **indexed on the INPUT timeline**: one ratio value per input audio frame; `ratio[i]` is the pitch ratio in force when the source position `i` is being read. Preserving engines map output frame `k` to input frame `k` (output timeline == input timeline). A RateFollowing engine drives its **read position** `r` such that the read advances by `ratio[r]` input frames per output frame (read-speed ratio = pitch ratio; varispeed family). The resulting emission map and total output length are properties of the integrated curve:

- expected output length = the output position `m` at which the read position reaches the input end, **plus** the declared `outputLatencyFrames` flush tail;
- equivalently `m` solves `∫₀^m ratio(r(τ)) dτ = N_in` (constant ratio ⇒ `m = N_in / ratio`).

#### D.4.3 Length policy

- **`Preserving` engines:** canonical total output length = **`N_in + outputLatencyFrames`** (the engine's complete response to the input stream, including the algorithmic tail emitted during flush). *(v1.0 said "output length == input length"; that contradicted the no-self-compensation latency rule — the delayed tail must come out somewhere. Amended here; recorded in the change record.)* Actual length ≠ canonical length ⇒ `length-policy` taint (§K).
- **`RateFollowing` engines:** output length is **not pre-declared**; it is discovered during the render. The harness computes the **expected output length** from the integrated curve + declared output latency, and verifies the actual produced length within a tolerance (**[OPEN DECISION]**: tolerance value — provisional: ±1 block). Mismatch ⇒ `length-policy` taint.
- End-of-render for both classes = **input exhausted + bounded flush complete**. The renderer never terminates a render by silently truncating or padding output.

#### D.4.4 Worked example (semantics, not implementation)

```text
Input: 1000 frames, constant ratio 2.0×, declared outputLatency = 0
Read position advances 2 input frames per output frame (input-indexed curve).
Expected output length: read reaches frame 1000 at output frame 500
  ⇒ ~500 output frames ⇒ approximately half the source duration;
  the output timeline is compressed 2:1 (the "corresponding output timeline").
Render ends when the read position reaches the input end and the flush
(bounded by outputLatencyFrames) is complete.
With a declared outputLatency = L, canonical output length = 500 + L.
```

The example explains semantics only; it does not freeze any implementation (interpolation strategy, block decomposition, or exact flush mechanics are engine/implementation business within these constraints).

#### D.4.5 Rejected alternatives (see also §O)

- Forcing RateFollowing engines into a duration-preserving contract (pad/truncate varispeed output to the input length) — rejected: it falsifies the reference model and hides the defining behaviour of the family (§O.18).
- Freezing the exact rate-following C++ signatures at design time — rejected: this document defines semantics; signatures are implementation detail within them (§O.20).

### D.5 Rejected capability fields (and why)

- `supportsExtremeRatio` — derivable from `minRatio/maxRatio`; redundant.
- `supportsTransientMaterial / supportsNoiseMaterial / supportsVoice...` — **rejected as contract fields.** These are *research findings*, not architecture: an artefact may be a feature (§1); encoding them as gate-keeping would fossilise today's opinions into the structure. They live as **descriptive metadata in the engine registry** (informative, non-enforcing). **[RESOLVED DESIGN]**
- `supportsWideband` — superseded by `BandwidthSpec` (quantitative, honest) rather than a boolean (qualitative, marketing-prone).
- semitone-based parameters anywhere in the contract — **rejected** (§5: ratio is canonical).

### D.6 Engine registry — ONE authoritative source of truth

**[RESOLVED DESIGN — correction item 2]** The engine registry is **defined in code, at one compile-time registration point** (`src/core/` registry; each entry binds a stable engine id to its implementation). It is the **single authoritative representation** of, for each engine:

- stable engine **ID** (referenced by experiments)
- **implementation binding** (the registered constructor/factory — identity and binding cannot diverge)
- **display name**
- engine **version**
- **UsageClass**
- **licence/origin metadata**
- **Capabilities** (§D.3)

Consequences:

- **Experiments reference engines by stable registry id.** An unknown id is a CONFIG ERROR at compile time (§K) — never an implicit default.
- **No second registry.** No config file, script, or document maintains an independent authoritative engine list; no synchronisation mechanism exists because there is nothing to synchronise. Manifests *copy* registry fields (read-only mirrors for provenance), they never become authority.
- **Configuration stays configuration.** `config/*.toml` may hold per-engine *tunable parameter defaults* (keyed by engine id) and harness defaults/tolerances — **never engine identity, capabilities, usage class or licence metadata.** Adding an engine = editing code at the registration point (and its implementation); never editing a config file.
- The registry is compile-time and static in v1 (dynamic loading rejected, §O; revisit condition in §N).

Initial registry contents (mapping §7 tiers → UsageClass; **phase column = implementation phase per §0.5**):

| Registry id | Tier | UsageClass | Phase | Origin / licence status (from research; **re-verify before linking**) |
|---|---|---|---|---|
| `native.varispeed` | A | **Prototype + harness reference role** | **v0.1** | own implementation; no deps |
| `native.vardelay` | A | Prototype | **v0.1** | own; note: naive-delay ratio limits per research §A (documented, engine is a *musical* shifter here) |
| `native.pv.classic` | A | Prototype (benchmark baseline role) | **v0.1** | own |
| `native.pv.phaselocked` | A | Prototype | **v0.1** | own (L-D '99 techniques, patents expired) |
| `native.granular` | A | Prototype (creative-leaning) | **v0.1** | own |
| `native.wsola` | B | Prototype | post-v0.1 | own (from Verhelst & Roelands math) |
| `native.tdpsola` | B | Prototype (voice lane) | post-v0.1 | own (from Moulines & Charpentier 1990) |
| `native.fdpsola` | B | Prototype (low priority) | post-v0.1 | own |
| `native.pv.transient` | B | Prototype | post-v0.1 | own (Röbel '03-style reset) |
| `native.sinusoidal` | B | Prototype (low priority) | post-v0.1 | own (MQ/SMS) |
| `native.sms` | B | Prototype (low priority) | post-v0.1 | own |
| `ext.signalsmith` | C | ExternalIntegration | post-v0.1 | Signalsmith Stretch — MIT **[RESEARCH FINDING: verified LICENSE.txt; commonly misreported as LGPL — re-verify at vendoring time]** |
| `ext.pbshift` | C | ExternalBenchmark (watch) | post-v0.1 | pbshift — MIT, pre-1.0 **[RESEARCH FINDING: maturity risk]** |
| `ext.rubberband` | C | ExternalBenchmark | post-v0.1 | Rubber Band — GPL-2.0+ ⇒ **subprocess adapter only** |
| `ext.soundtouch` | C | ExternalBenchmark | post-v0.1 | SoundTouch — LGPL-2.1 ⇒ **subprocess adapter only** |
| *(future)* `future.hnm`, `future.world`, ... | — | Future / Rejected | future | per research report §B.6 (not built unless re-scoped) |

### D.7 What the contract deliberately does NOT contain

- No bypass/identity shortcut method (would defeat the ratio=1.0 contract test — engines must really process; rejected in §O).
- No quality self-assessment, no "render report" from engines.
- No streaming-time discovery of capabilities (fixed after `prepare()`).
- No engine-side "effective ratio" reporting (creative-mode saturation is harness-side and recorded — §F.4; an engine reporting its own execution result would create a second authority and an engine-side test surface; rejected in §O.19).
- No Doppler/propagation concepts (§0.3).
- No GUI model, no parameter automation model beyond the curve view.

## E. Pitch curve model

### E.1 Canonical representation

- **Canonical domain: linear pitch ratio, double precision.** `ratio = 1.0` is unchanged; 0.5 = −1 octave; 2.0 = +1 octave; 4.0, 8.0 … unrestricted at the architecture level. **[RESOLVED DESIGN — §5 mandate]**
- Semitones exist ONLY at the authoring surface: the curve generator accepts semitone expressions and converts via `ratio = 2^(st/12)` at compile time. The engine contract, harness, manifests and metrics see ratio only. **[RESOLVED DESIGN]**
- **No artificial global range.** Each engine's practical limits come from its `Capabilities`; the compiler skips (benchmark) or explicitly saturates with record (creative) jobs accordingly (§K). The curve system itself must accept and represent any positive finite ratio (e.g. 0.25× and 8× from the extreme battery, and beyond for creative work). **[RESOLVED DESIGN]**

### E.2 Two-layer model: PitchCurveSpec → PitchCurveSignal

**Layer 1 — `PitchCurveSpec` (declarative, authoritative, serialisable):**

```toml
# experiments/curves/fast-reversal.toml  [schema sketch; syntax = IMPLEMENTATION DETAIL]
id = "fast-reversal"
[crossfadeless_reversal]                 # shape spec, one of:
kind   = "reversal"                      # static | ramp_lin | ramp_exp | lfo
                                         # | random | reversal | breakpoints | external
from   = { semitones = +12.0 }
to     = { semitones = -12.0 }
time   = { ms = 100 }
hold   = { ms = 400 }                    # pre/post holds as needed
seed   = 0                               # REQUIRED for any stochastic element
```

Supported spec kinds (§9 mandate): static ratio; static semitone; linear ramp; exponential ramp (per-second semitone rate or ratio-power rate); slow/fast ramp profiles; LFO (sine / triangle / saw / random-walk) with rate, depth, centre, min/max bounds; rapid reversal; breakpoint curve (monotone times, per-point ratio, per-segment interpolation law); user-supplied curve (external CSV/F32 file of (time, ratio) or dense ratio at declared rate). Every stochastic element carries an explicit seed. **[RESOLVED DESIGN]**

**Layer 2 — `PitchCurveSignal` (dense, derived):**

- Compiled by CurveLibrary deterministically from the spec into a **dense audio-rate ratio array — one double per INPUT audio frame** (input-timeline indexing; see §D.4.2) at the job's sample rate.
- This dense signal is the **single source of pitch truth for a job**; the renderer hands engines a `PitchCurveView` over it.
- Engines with `PerSample` control consume it directly; `FixedBlock`/`EngineEvent` engines sample it at their own cadence via the view (the harness logs the declared cadence; the analyzer *measures* the effective control rate independently — see §H). RateFollowing engines evaluate it at their read position (§D.4.2).
- **[RESOLVED DESIGN]** Why dense: it makes per-sample vs per-hop control a *measurable engine property* instead of a harness property, exactly what §9 demands the benchmark demonstrate. Cost is negligible for offline research file sizes.

### E.3 Control-rate semantics (first-class)

| Concept | Owner | Notes |
|---|---|---|
| Curve *definition* rate | Spec (author) | breakpoints/params in time domain |
| Curve *signal* rate | Harness | audio-rate dense (one ratio per input frame, input-timeline indexed) |
| Curve *consumption* rate | Engine (declared in `ControlRateSpec`) | per-sample / fixed-block / engine-event (per hop, per grain, per period…); rate-followers: per output frame at the read position |
| Curve *effective* rate | Analyzer (measured) | pitch-tracking lag + reversal-response latency measured from output |

The harness must **never** quantise the curve globally; block-rate quantisation that some engine performs internally is that engine's measured characteristic. **[RESOLVED DESIGN]**

### E.4 Curve validation (compile-time, hard errors)

- ratios: finite, strictly positive (NaN/≤0 → config error);
- times: finite, strictly monotone;
- duration coverage: curve must cover `[0, totalFrames)` — extend policy: hold-first/hold-last declared in spec (default hold-last);
- discontinuities: allowed only if the spec declares `allow_discontinuity = true` (rapid reversal is a *derivative* discontinuity — always allowed; *value* jumps need the flag, so unintentional clicks are caught at compile time).
**[RESOLVED DESIGN — §21 "invalid pitch curve"]**

### E.5 Requested vs effective curve (creative mode)

The authored curve spec/signal is the **requested** curve and is never mutated. In creative mode, out-of-declared-range jobs are rendered against a harness-derived **effective** curve (saturation to the engine's declared `[minRatio, maxRatio]`, produced deterministically by the ExperimentCompiler); both curves — and the saturation status — are recorded end-to-end (manifest → analysis → reports). See §F.4. The requested curve remains the authoritative authored artifact. **[RESOLVED DESIGN — correction item 4]**

---

## F. Experiment model (entities, lifecycle, ownership)

### F.1 Entity definitions

| Entity | Creation | Owner | Mutable state | Persistence | Lifetime / destruction |
|---|---|---|---|---|---|
| **Experiment** | Author writes TOML in `experiments/` | Author (file) | by editing the file | authoritative file | until deleted by author |
| **TestCase** | Compiler expands matrix | Compiler (ephemeral) | none (immutable value) | listed in jobplan.json (generated) | duration of run |
| **InputAsset** | Author adds file + `metadata.toml` to `assets/corpus/` | CorpusRegistry (read) | immutable | authoritative + hash pinned in manifests | permanent |
| **PitchCurveSpec** | Author writes curve TOML | CurveLibrary (read) | immutable | authoritative file | permanent |
| **PitchCurveSignal (requested)** | Compiled from spec per job | Compiler → Renderer | derived, job-scoped | hash in manifest | job |
| **PitchCurveSignal (effective, creative only)** | Derived by compiler saturation (§E.5) | Compiler → Renderer | derived, job-scoped | hash + derivation note in manifest | job |
| **PitchEngine (instance)** | Renderer constructs per job | Renderer | internal DSP state during job | none | destroyed at job end (state never leaks across jobs) |
| **EngineConfiguration** | Experiment file | Author | immutable | authoritative + manifest copy | permanent (file) |
| **RenderJob** | Compiler | queue handoff | status field until completion | jobplan.json | run |
| **RenderResult** | Renderer emits | nobody (immutable artifact) | none | `artifacts/renders/**.wav` + `...manifest.json` | permanent until `artifacts/renders/` regenerated (generated tree) |
| **AnalysisResult** | Analyzer emits | nobody (immutable artifact) | none | `artifacts/analysis/**.json` | as above |
| **BenchmarkRun** | Reporter aggregates | Reporter | none (immutable view) | `artifacts/reports/<run-id>/` | generated |

**No manager classes** exist beyond the four components in §B.1. **[RESOLVED DESIGN — operating principle: no unnecessary abstraction]**

### F.2 InputAsset metadata (minimum schema)

```toml
# assets/corpus/whip-crack-1m-96k/metadata.toml
id          = "whip-crack-1m-96k"
category    = "whip"                # sine|harmonic|chord|musical|speech|sung|percussion|
                                    # transient|noise-white|noise-pink|whoosh|whip|
                                    # stereo-ambience|diagnostic|wideband
sampleRate  = 96000
channels    = 1
durationSec = 0.8
sourceDescription = "own measurement per research report A.6.3, 1 m, on-axis"
referenceStatus   = "real-world-recording"   # test-fixture | benchmark-reference |
                                              # experimental-source | real-world-recording
bandContentHz     = [20, 40000]     # honest declared content band (wideband §I)
```

**[RESOLVED DESIGN]** — `category` vocabulary is fixed by §8; `referenceStatus` distinguishes the four asset roles (fixture/reference/experimental/real-world). Corpus assets are authored **at their intended sample rates**; the harness never silently resamples (see §I).

### F.3 Run identity and reproducibility

- A **run-id** = `{experiment-id}_{UTC-timestamp}_{configHash8}`. Directory naming under `artifacts/renders/`: `artifacts/renders/<run-id>/<engine-id>/<asset-id>__<curve-id>__<fs>__<paramtag>/`.
- Every manifest records: input asset id + SHA-256; curve spec id + spec hash + **requested signal hash (+ effective signal hash + saturation status, creative only)**; engine id, version, adapter/external version; full EngineConfiguration incl. seed; sample rate; channels; harness block size; **length accounting per §D.4 (input frames consumed, output frames produced, declared latency, expected/canonical output length)**; git commit (or version fallback); config hash; host triplet. **[RESOLVED DESIGN — §14/§17 mandate]**
- Two identical configurations → identical WAV bytes for deterministic engines (verified by `pitchlab verify`, §L). **[RESOLVED DESIGN]**

### F.4 Benchmark vs Creative separation (§12)

- `Experiment.kind ∈ {benchmark, creative}` — a single field set at the top of the experiment file.
- **Benchmark mode:** full validation (§E.4, §K), canonical suites (§10), capability-filtered jobs, results enter aggregation. **Nothing in benchmark mode is relaxable. Out-of-range ratio → job skip** (strict; no saturation, no render).
- **Creative mode: relaxed policies, never silent ones.** Explicitly allowed:
  - **Out-of-declared-range ratios:** the job **may render**. The ExperimentCompiler saturates the requested curve to the engine's declared range, producing the **effective curve**; the engine receives the effective curve. The manifest records **requested curve** (spec id + signal hash), **effective curve** (signal hash + derivation note: "saturated to [min,max] of engine `<id>`"), the requested-vs-effective extrema, and an **execution status** (semantic: range-saturated; exact status vocabulary = **[IMPLEMENTATION DETAIL]**). Example:

    ```text
    requested ratio = 32.0×
    engine declared limit = 8.0×
    effective ratio = 8.0×   (harness saturation, recorded)
    execution status = RANGE_SATURATED
    ```

  - Discontinuous curves without pre-declaration (flagged, not blocked); extreme parameter values; unusual engine combos (a chain is NOT supported in v1 — see §M; "combination" in v1 means *rendering the same material through different engines and layering externally*); NaN/Inf in output → render completes, result **tainted** (flagged in manifest + analysis refuses to score tainted results but preserves them for listening).
- **Semantic rule (binding):** creative mode may deliberately exceed an engine's declared range, but **neither the engine nor the harness may silently pretend the requested value was executed exactly.** Requested, effective and status are preserved end-to-end through manifest, analysis and reports (Operating Principles §79 — no silent information loss at a boundary). Engines never perform their own unrecorded saturation: an engine whose measured behaviour contradicts the (in-range) curve it was given is a capability-honesty taint (§K). **[RESOLVED DESIGN — correction item 4]**
- Creative outputs live in `artifacts/renders/<run-id>/creative/...` namespace and are **excluded from benchmark aggregation** — no metric table ever mixes them in. **[RESOLVED DESIGN — §12 "must never alter the correctness of the benchmark mode"]**
- Creative mode cannot disable: determinism, seeding, manifests, NaN tainting, requested/effective recording. (Determinism is a property of the system, not a quality judgement.) **[RESOLVED DESIGN]**

---

## G. Benchmark architecture (identical tests across engines)

### G.1 The methodology-ownership rule (§2)

The harness (compiler + renderer + analyzer) owns: input selection, sample rate, duration, pitch trajectory, test case definition, output naming, metadata, benchmark execution, measurements. Engines own ONLY processing behaviour and honest capability declaration. There is **no engine-side test API** — an engine cannot influence how it is tested. **[RESOLVED DESIGN — architectural guarantee, not policy document]**

### G.2 Matrix expansion, capability filtering, and the benchmark/creative boundary

ExperimentCompiler expands `inputs × curves × engines × configs × sampleRates`, then:

- filters by `Capabilities` (ratio range vs curve extrema, channel mode, declared fs support);
- **benchmark mode: out-of-range → JOB SKIP** (reason `ratio-out-of-range`; the job is not rendered — no silent clamping, no saturation). Skips are results, not silence;
- **creative mode: out-of-range → may render**, via the recorded saturation semantics of §F.4/§E.5 (requested + effective + status);
- each filtered job becomes a **skip record with reason code** (`ratio-out-of-range`, `layout-unsupported`, `sample-rate-unsupported`, `engine-unavailable`), persisted in the job plan;
- unfiltered jobs proceed identically: same block schedule, same curve signal, same manifest schema.

### G.3 The initial benchmark suite (§10 mandate, as authoritative experiment files)

| Suite | Content |
|---|---|
| `static` | −24, −12, −5, −1, +1, +7, +12, +24 st × full corpus |
| `dynamic` | +1 st/s ramp; +12 st/s ramp; fast exponential glide; rapid reversal (±12 st in 100 ms) × corpus |
| `extreme` | −48 st, +48 st; ratios 0.25×, 0.5×, 1×, 2×, 4×, 8× × corpus |
| `material` | per-class focus: sine, harmonic, polyphonic, voice, percussion, transient, noise, whoosh, whip, stereo, wideband (each class's representative assets, standard curve set) |

**[RESOLVED DESIGN]** — expected: not all engines pass (or even complete) all cells; the suite's purpose is to *reveal behaviour*, including skip patterns and failures. Metrics are per-class (§H); no global score. Sample rates for the suite: each asset's native rate (§I). Note for the suite: rate-following engines produce different-duration outputs than preserving engines for the same (input, curve) — this is **measured, reported behaviour** (realised duration ratio; §H), never normalised away.

### G.4 Listening / human validation support (§17)

- Reporter emits a **listening index**: for each (input × curve × fs) group, the reference + one render per engine, identically named, with a one-line manifest summary (algorithm, params, seed, latency, **duration behaviour + output duration** — listeners must see which renders are rate-following/shorter).
- Renders are plain WAVs → any DAW/media player works; a tiny `pitchlab listen-index` command regenerates the index from manifests (no GUI in scope).
- Identity chain guaranteed by manifests (§F.3) — every question of §17 ("what algorithm/parameters/input/curve/sample rate?") is answered from the manifest alone, without the source experiment file. **[RESOLVED DESIGN]**

---

## H. Analysis architecture (measurements strictly separated from rendering)

```text
Renderer ──► RenderResult (wav + manifest) ──► Analyzer ──► Metric modules (pure functions)
                                                   │              each: (reference, output, context) → {value, status, notes}
                                                   ▼
                                             AnalysisResult (json)
```

- Analyzer **never touches engines**; it reads files only. It cannot re-render or re-interpret test intent. **[RESOLVED DESIGN]**
- Metric modules are independent pure functions; adding a metric never modifies rendering or engines. **[RESOLVED DESIGN]**
- **Comparison axis and alignment (explicit, per correction item 1):** comparisons use the **input timeline as the common axis**. A Preserving engine's output frame `i` corresponds to input frame `i` (after declared-latency alignment); a RateFollowing engine's output corresponds to input positions via its emission map (§D.4.1/§D.4.2, reconstructed from the manifest's curve hash + latency + lengths). The varispeed reference is itself rate-following, so rate-follower-vs-reference comparisons align naturally on the output timeline; cross-class comparisons (preserving engine vs rate-following reference) warp the reference to the engine's timeline via the emission map. Each metric module declares its alignment method; alignment parameters live in `config/metrics.toml` (provisional). No metric silently compares misaligned timelines. **[RESOLVED DESIGN]**

### H.1 Metric set (§15 mandate) and method class

| Metric | Method class | Notes |
|---|---|---|
| Pitch error | tracker (offline pYIN/CREPE via optional Python layer; or C++ tracker later) | compares measured output f₀(t) vs expected f₀(t) curve (analytically derived from input + curve spec; for `RateFollowing` engines the expected map is the curve's emission map — §D.4) **[IMPLEMENTATION DETAIL: expected-map math]** |
| Pitch tracking lag | tracker + cross-correlation of measured vs expected f₀ under ramps/reversals | the direct measurement of control-rate honesty (§E.3) |
| Spectral error | own STFT (C++) log-spectral distance vs varispeed reference | varispeed is artifact-free by construction **[RESEARCH FINDING §B.1.1]**; alignment per §H preamble |
| Transient preservation | attack correlation + onset-time error vs reference | |
| Onset timing | onset detector | includes latency estimation independent of declared latency |
| Phase coherence | harmonic-stack alignment metric | |
| Stereo coherence | inter-channel cross-correlation delta vs input | |
| Latency | declared (manifest) + measured (onset/impulse alignment) — both reported | discrepancy > tolerance ⇒ capability-honesty flag **[OPEN DECISION: tolerance, provisional ±1 ms]** |
| **Realised duration / length conformance** | waveform | expected output length per §D.4 (canonical for Preserving; integrated-curve expectation for RateFollowing); the realised duration ratio of rate-followers is itself a reported measurement |
| CPU cost | renderer-measured real-time factor per block size (32…4096), single thread, pinned | measured on the harness's schedule; provisional normalization documented |
| Peak / RMS / Crest | waveform (C++) | |
| HF energy | spectral (C++) | band relative to fs (no fixed 20 kHz ceiling — §I) |
| Aliasing indicators | spectral: energy above the expected max output frequency given input band + ratio | verifies `BandwidthSpec` honesty |
| Amplitude modulation | envelope spectrum at expected grain/crossfade rates | |
| Potential warble | f₀ flutter variance under static ratio | |

- **No global quality score.** Aggregation presents per-metric, per-material-class tables. Weighted composites are explicitly rejected (§O). **[RESOLVED DESIGN]**
- **All numeric tolerances are PROVISIONAL** and are defined in `config/metrics.toml` (versioned, cited in reports) — flagged as such per §22. **[RESOLVED DESIGN]**

### H.2 Reference model (§16)

- `native.varispeed` is the harness's first reference engine where mathematically appropriate (band-limited Kaiser-sinc resampling, §I) — as **reference**, not as "best". The reference render per (input, curve, fs) is produced once per run and referenced by analysis jobs. The varispeed reference is a **RateFollowing** engine and its output length follows §D.4 — this is precisely why the rate-following contract must not be forced into duration preservation (§D.4.5).
- Other reference engines can be registered later with `reference` role (e.g. an external engine as perceptual high anchor) — architecture already supports this via UsageClass + job plan. **[RESOLVED DESIGN]**
- Varispeed as reference is **not** used for creative-mode judgement (creative mode has no correctness target).

## I. High-sample-rate architecture (44.1 kHz → 192 kHz, wideband material)

### I.1 Principles

1. **The harness supports 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz as first-class rates.** No component assumes 44.1/48. FFT sizes, windows, grain sizes, and metric bands are **specified in seconds (or Hz)** and converted per rate at `prepare()` time — with engines free to clamp to their internal rate ceilings via `BandwidthSpec`. **[RESOLVED DESIGN]**
2. **No arbitrary 20 kHz processing ceiling.** High-frequency metrics and aliasing detection use bands relative to the *actual* Nyquist of the job's fs. **[RESOLVED DESIGN]**
3. **Corpus assets are authored at their target rates** (e.g. a 96 kHz whip recording is a 96 kHz asset). The harness **never silently resamples an asset** — if an experiment wants an asset at a different rate, it must declare `resample = { engine = "native.varispeed", quality = "kaiser-..." }` explicitly; the resampled asset is then a **generated intermediate** (recorded in the manifest with source hash + resampler params, stored under `artifacts/`), never an authoritative corpus file. **[RESOLVED DESIGN]**
4. **A high sample rate does NOT imply an algorithm preserves ultrasonic content.** This is exactly what the benchmark demonstrates: `BandwidthSpec` declares per-engine bandwidth; the aliasing/HF metrics measure the truth; capability-honesty flags arise from the comparison. **[RESOLVED DESIGN — §6 mandate: "the benchmark must demonstrate this"]**

### I.2 Correctness machinery

- **Varispeed reference (band-limited):** windowed-sinc (Kaiser) interpolation with pre-shift anti-alias filtering: for upward shifts (ratio > 1), input is low-passed at Nyquist/ratio before resampling (otherwise content folds down — *correctness*); for downward shifts no AA is needed (spectrum moves down). Creative aliasing is possible via the **creative-mode parameter** `allow_aliasing` on the varispeed *experiment* engine config (not the reference role) — recorded in the manifest. **[RESOLVED DESIGN]**
- **Extreme upward shifts (×4, ×8):** correctness at any fs means most real content exits the audible band — the metrics must report this neutrally (HF-energy distribution, band occupancy), not as "failure". **[RESOLVED DESIGN]**
- **Extreme downward shifts (0.25×):** long read pointers → memory/delay policy is engine-internal; harness imposes nothing beyond the total-frames context. **[RESOLVED DESIGN]**
- **Ultrasonic source material:** assets declare `bandContentHz` (§F.2); wideband experiments pair wideband assets with high fs; the analyzer uses the declared band when computing expected max-frequency / aliasing windows. **[RESOLVED DESIGN]**
- **Internal-rate engines** (e.g. an engine that internally runs a 48 kHz processing core): allowed, but the engine's `BandwidthSpec` must declare the resulting ceiling (e.g. `nyquistFraction = 0.45` at 48k-core → ~21.6 kHz absolute). The architecture treats declared-vs-measured mismatch as a flagged finding. **[RESOLVED DESIGN]**

---

## J. Dependency and licensing matrix

Strategy per §19: implement simple DSP primitives ourselves when that removes dependency/licensing complexity; add a library only with a purpose it cannot honestly replace. **Licensing classifications inherited from the research report's verified table [RESEARCH FINDING] — items marked ⚠ require fresh license verification at vendoring time (licenses change; the research verification is dated 2026-02).** The engine-registry decision (§D.6) introduces **no** dependency: the registry is own code in `src/core/`.

| Dependency | Purpose | Runtime or dev-only | Licence (per research) | Replaceable? | Deployment complexity | Effect on future proprietary VST |
|---|---|---|---|---|---|---|
| **CMake + Ninja** | build | dev-only | permissive (Kitware BSD-style) | yes (any build system) | none | none |
| **own DSP primitives** (WAV I/O, sinc resampler, windows, PCG64 RNG, STFT helpers) | core | runtime (lab) | own code, no licence | n/a | none | **donatable** — written once, reused in VST |
| **pocketfft** ⚠ | FFT for spectral engines | runtime (lab) | BSD-3 **[RESEARCH FINDING]** | yes (KISS FFT / PFFFT / own radix-2) | vendored header | none (permissive; verify at vendoring) |
| **Signalsmith Stretch** ⚠ | external integration engine (Tier C) | runtime (lab, behind adapter) — **post-v0.1** | **MIT** **[RESEARCH FINDING — verified LICENSE.txt; frequently misreported as LGPL]** | replaceable (own spectral engine is the long-term path) | vendored, header-only | **safe** (MIT, keep notices) |
| **pbshift** ⚠ | external watch/benchmark engine (Tier C) — **post-v0.1** | runtime (lab, behind adapter) | **MIT, pre-1.0** **[RESEARCH FINDING: maturity risk; self-reported quality/CPU unverified]** | replaceable | vendored + pffft (BSD-like) | safe if matured; verify then |
| **Rubber Band CLI** | external benchmark renders — **post-v0.1** | dev/benchmark-only subprocess | GPL-2.0+ — **subprocess adapter only** (no linking) | replaceable (benchmark only) | requires system install of the CLI | **none** (never linked) |
| **SoundTouch CLI/library** | external benchmark renders — **post-v0.1** | dev/benchmark-only subprocess | LGPL-2.1 — **subprocess adapter** | replaceable | CLI install | **none** (never linked) |
| **Python 3.11 + numpy/scipy/matplotlib** ⚠ (OPTIONAL) | offline analysis layer (trackers, plots, report tables) | **dev-only, never runtime DSP** | BSD / BSD / matplotlib licence | replaceable (pure-C++ analysis path designed-in, §H) | venv + lockfile | **none** (analysis only; never in DSP path) |
| **librosa (pYIN) / CREPE** ⚠ (OPTIONAL) | offline reference pitch tracking for metrics | dev-only | ISC / MIT **[RESEARCH FINDING]** | replaceable (C++ tracker later; analyzer is pluggable) | Python layer | none (offline benchmarking only, per research) |
| **Catch2 / doctest** ⚠ | unit tests | dev-only | BSL-1.0 | yes | none | none |
| **Rejected: FFTW** | — | — | GPL + paid commercial | — | — | **avoided** **[RESEARCH FINDING]** |
| **Rejected: libsndfile** | audio file I/O | — | LGPL | replaced by **own WAV I/O** (float PCM RIFF/W64 only — trivial, deterministic, dependency-free) | — | none |
| **Rejected: JUCE in Pitch Lab** | — | — | AGPL/commercial | **explicit non-dependency**: the Lab must not couple to JUCE; JUCE enters only with the future VST project | — | keeps donation path clean |

**[RESOLVED DESIGN]** — no hidden global dependencies: everything is either vendored in `external/` (with verbatim LICENSE + ORIGIN.toml recording version + source URL + verification date) or a declared dev tool in the lockfile. A clean-machine bootstrap is `scripts/bootstrap.sh` → toolchain check → vendoring verification (licence hash comparison) → build → `pitchlab verify` self-test. (v0.1 note: with externals deferred per §0.5, the v0.1 bootstrap vendors nothing beyond the build toolchain and test framework.)

---

## K. Failure model

Error taxonomy (three classes, each with detection point, behaviour, persistence):

1. **CONFIG ERROR** — detected at compile time; the experiment (or the offending definition) fails; nothing renders; a human-readable error names the exact file/field.
2. **JOB SKIP** — detected at compile time via capabilities or environment; the job is not rendered; the skip + reason code is persisted in the job plan (visible in reports).
3. **RENDER TAINT / FAILURE** — detected during/after render; the job runs but its result is marked; analysis behaviour defined per case.

| Case (from §21, extended) | Class | Behaviour | Persistence |
|---|---|---|---|
| missing input asset | CONFIG ERROR | experiment fails: "asset `<id>` referenced but not found in corpus" | error log |
| **unknown engine id** | CONFIG ERROR | experiment fails: "engine `<id>` referenced but not registered (registry is in code, §D.6)" | error log |
| unsupported sample rate (engine) | JOB SKIP | reason `sample-rate-unsupported` | job plan |
| invalid ratio in curve (NaN/≤0) | CONFIG ERROR | curve validation §E.4 | error log |
| ratio outside engine range | **benchmark: JOB SKIP (`ratio-out-of-range`)** / **creative: render with harness saturation** — manifest records requested curve, effective curve, extrema, execution status (§F.4) **[RESOLVED — was OPEN in v1.0]** | recorded either way | job plan / manifest |
| invalid pitch curve (times non-monotone etc.) | CONFIG ERROR | §E.4 | error log |
| empty input (0 frames) | CONFIG ERROR | rejected at corpus validation: assets must have ≥1 frame | error log |
| zero-length render requested | CONFIG ERROR | rejected | error log |
| NaN/Inf in **output** | TAINT | render completes; manifest `taints:["nan-inf"]` + count + first frame; analyzer refuses metrics on tainted results (benchmark) or records but warns (creative) | manifest + analysis |
| NaN/Inf in **input** | CONFIG ERROR | corpus validation computes input integrity hash incl. finiteness scan | error log |
| engine initialisation failure (prepare/throw) | JOB FAILURE | job marked failed with error string; run continues with next job | job plan + manifest stub |
| external dependency unavailable | JOB SKIP | adapter reports `engine-unavailable`; **[OPEN DECISION: hard error vs skip]** default: skip + reason | job plan |
| render interruption (Ctrl-C / crash) | INCOMPLETE | partial WAV + manifest marked `incomplete`; incomplete results never enter analysis or reports | manifest |
| **engine fails to reach end-of-render** (does not terminate after input exhaustion + bounded flush, or produces past its bounded flush) | JOB FAILURE | job marked failed (`end-of-render-violation`); run continues | job plan + manifest stub |
| insufficient memory | JOB FAILURE | fail fast with allocation context; run continues if possible | job plan |
| unsupported channel layout | JOB SKIP | reason `layout-unsupported` | job plan |
| declared-vs-measured latency mismatch | TAINT (capability honesty) | flagged; render otherwise valid | analysis |
| engine output length violates §D.4 (Preserving: ≠ `N_in + outputLatency`; RateFollowing: outside tolerance of the integrated-curve expectation) | TAINT | manifest `taints:["length-policy"]`; analysis aligns what it can | manifest |
| engine measured behaviour contradicts the (in-range) curve it was given (undeclared internal saturation or quantisation beyond declaration) | TAINT (capability honesty) | flagged via pitch-error/lag metrics; render otherwise valid | analysis |

**Explicitly not designed** (requirements undefined — do NOT invent): user-facing recovery flows, partial re-run policies, interactive error prompts. All failures are **fail-visible in files**; recovery = fix the authoritative file and re-run. **[RESOLVED DESIGN]**

---

## L. Testing strategy

| Layer | What is tested | Tools / notes |
|---|---|---|
| **Unit** | curve compiler (spec→signal, validation cases from §K); ratio math; WAV I/O round-trip (incl. 192 kHz, multi-channel WAVE_FORMAT_EXTENSIBLE); sinc resampler passband/stopband specs; PCG64 determinism | Catch2; DSP tests with analytic signals (sines, impulses) |
| **Component (per engine contract)** | (1) `ratio = 1.0` → output equivalent to bypass within tolerance **after declared-latency alignment** (engines must actually process — no shortcut path exists, §O); (2) constant ratio → measured pitch matches expected ratio within tolerance; (3) sample-rate change → behaviour preserved (same test at 44.1 & 96 & 192); (4) stereo input → channel relationship within engine-declared expectations; (5) determinism → same schedule twice = bit-identical; (6) block-boundary independence: audio output for block schedules {4096} vs {1024, remainder} equal within tolerance **[OPEN DECISION: bit-exact vs audio-exact tolerance; provisional: −80 dB FS error floor]**; (7) **length policy per §D.4** — Preserving: total output == `N_in + outputLatency` exactly; RateFollowing: within tolerance of the integrated-curve expectation + `outputLatency`; (8) **end-of-render** — render terminates at input exhaustion + bounded flush; no unbounded tail; constant-ratio worked case from §D.4.4 (1000 frames @ 2.0× ⇒ ~500 + L) | every engine runs the SAME contract test suite — the anti-"each algorithm defines its own methodology" guarantee at the unit level |
| **Renderer** | manifest completeness (all §F.3 fields present, incl. length accounting and requested/effective/status on saturated creative jobs); output naming; latency accounting; incomplete/taint marking; skip plan correctness | golden job plans |
| **Reference comparison** | each metric module against hand-computed analytic cases (e.g. known lag for a deliberately block-quantised dummy engine) | dummy engines are allowed in tests only, registry-flagged `test-only` |
| **Regression** | golden metric JSON with headroom bands (values may drift within declared bands across platform changes) | **[OPEN DECISION: band widths; provisional ±20% relative]** |
| **Performance** | RTF per engine per block size per fs; results are *measurements* (no thresholds in v1) | recorded in analysis JSON |
| **Failure** | every §K case triggered by a fixture experiment; assert class + persistence (incl. unknown engine id, end-of-render violation, creative saturation record) | the failure model is itself tested |
| **Reproducibility** | full run re-executed → deterministic engines byte-identical WAVs; seeded stochastic engines byte-identical; manifest diffs = empty | `pitchlab verify` — the recoverability gate |

**All numeric tolerances in tests are PROVISIONAL and live in `config/tolerances.toml`** (versioned; cited in every report) — per §22 they are identified as provisional, not silently chosen. **[RESOLVED DESIGN]**

## M. Future compatibility (without coupling now)

The design must allow five future systems — **PropagationEngine, Doppler control, Shock Events, Spatial Engine, Feedback Engine** — without any of their concepts entering Pitch Lab today.

| Future system | Architectural boundary preserved by this design |
|---|---|
| **PropagationEngine / Doppler** | Pitch Lab's engine contract is explicitly *musical pitch* (duration-preserving or rate-following by ratio curve — §D.4). The future propagation engine is a *different contract* (retarded-time, per-segment, duration-modulating, supersonic event semantics) per the research report §B.3.3. None of those concepts exist here; Pitch Lab's curve model (ratio-only) cannot express them, which is the *point*: no accidental coupling. Integration later happens at a **chain/graph layer above both contracts** (below). |
| **Doppler-driven pitch curves** | Already supported *as curves*: arbitrary time-varying ratio signals (§E). A future Doppler system can emit a ratio curve into Pitch Lab's curve format for A/B comparison — without Pitch Lab knowing where the curve came from (the spec's `external` kind + `sourceDescription` metadata covers provenance). |
| **Shock Events** | Belong to the propagation domain. Pitch Lab's transient *detector* code (shared analysis pool, §B.2) is donatable, but no event-scheduler concept exists here. No coupling. |
| **Spatial Engine** | Channel handling is already multi-capable (`ChannelMode`, planar bus). No spatialisation concepts (no panning, no HRTF) in the contract. Future spatial stages consume the same planar block pattern — but are built in the future project, not here. |
| **Feedback Engine (§13, explicitly out of scope)** | The required boundary is identified as follows: (a) the engine contract is **single-stage** (`process()` maps input-block+curve → output-block; no loop-back semantics); (b) feedback requires a **future ChainRenderer / graph layer** that taps `RenderOutput` buffers, applies feedback gain/delay/safety (saturation, NaN/Inf guard, kill switch, buffer clear) and feeds the chain input. The existing `RenderInput/RenderOutput` buffer semantics are already tap-compatible (planar, framed, owned by the renderer — the exact points a future loop controller needs to intercept); (c) NaN/Inf protection already exists at render validation (§K), which is the *detection* half of the future safety system; (d) nothing in the contract prevents a future engine from being wrapped as a chain stage. **[RESOLVED DESIGN — boundary only, per §13]** |

**VST donation path (quality check Q12):** the Lab's own DSP primitives (resampler, windows, FFT usage, delay machinery, RNG) and the engine implementations are plain C++ with no Lab-specific dependencies; donating them later = extracting the `src/engines/native/<engine>` + `src/core/` DSP trees. The Lab is never a *runtime* dependency of the VST (no linked relationship by construction — the VST project will re-vendor selected code). **[RESOLVED DESIGN]**

---

## N. Open decisions (must be decided by the owner, not silently by implementers)

1. **Language mix confirmation:** C++ core + *optional* Python analysis layer is the recommendation; if the team mandates pure C++, the analysis layer falls back to the designed-in C++ metric path (the architecture supports both; §H, §J).
2. **Authoritative config file format:** TOML recommended for author-facing files, JSON for generated manifests; final choice open (schema is fixed either way). Engine identity is never in config regardless (§D.6).
3. **Harness block size default** (provisional 4096 frames; engine-test schedules use {4096} vs {1024}).
4. **WAV export bit depth:** float32 (listening-friendly) vs float64 (analysis-exact) vs both. Provisional recommendation: float64 for all research renders (disk is cheap, determinism is precious); float32 export flag for listening copies. Open.
5. **Block-boundary independence standard:** bit-exact vs audio-equivalent (−80 dBFS provisional) for native engines (§L).
6. **Rate-following output-length tolerance** (§D.4.3): provisional ±1 block; needs empirical calibration against the varispeed reference before being treated as authoritative.
7. **External adapter build order for Tier C (all post-v0.1, §0.5):** recommended order Signalsmith → Rubber Band (subprocess) → SoundTouch → pbshift (last, pre-1.0). Open.
8. **Engine registry growth:** compile-time static registry is authoritative and sufficient; revisit dynamic loading **only** if the engine count grows past ~20 — recorded to prevent silent scope creep.
9. **Metric tolerance values:** ALL provisional (§H, §L); require empirical calibration on Tier-A engines before being treated as authoritative.
10. **Corpus acquisition:** who records/sources the whoosh/whip/wideband assets (research report §A.6.3 defines the measurement protocol); licensing status of each real-world asset must be recorded in its metadata.
11. **`>2-channel` (5.1 etc.) support depth:** mono+stereo now; multi-channel is contract-capable but untested — decide whether v1 tests it at all.
12. **Reference pitch tracker choice** (pYIN vs CREPE vs C++ own) for the analysis layer — dev-only decision, but affects metric definitions.
13. **Reporting output format** (Markdown tables + CSV + optional HTML) — presentation choice, owner's call.

*(v1.0 items #1 "Operating Principles availability" and #7 "creative-mode defaults for out-of-range ratios" are **resolved** by this correction pass: the principles file is present and reconciled — §P; creative out-of-range behaviour is mandated as render-with-recorded-saturation — §F.4.)*

---

## O. Rejected approaches (considered and rejected, with reasons)

1. **GUI-first or GUI-in-v1** — rejected: §25 non-goal; CLI + files + listening indexes satisfy §17 without GUI risk.
2. **Real-time audio path in v1** — rejected: determinism and reproducibility (§14) come first; real-time brings scheduling nondeterminism the research phase must not absorb.
3. **Single global quality score / weighted composite metric** — rejected: §1/§11 require per-class, per-metric distinction; composites fossilise taste into architecture.
4. **Per-algorithm test methodologies** — rejected: §2 core principle; the harness owns methodology (structurally guaranteed: engines have no test-facing API).
5. **Semitone-based canonical pitch representation** — rejected: §5; ratio is canonical, semitones are an authoring convenience.
6. **Linked integration of GPL/LGPL engines (Rubber Band, SoundTouch)** — rejected: licensing contamination for the future proprietary VST **[RESEARCH FINDING licensing matrix]**; subprocess adapters preserve benchmark access without linking.
7. **Database for results** — rejected: file-based manifests + generated trees are sufficient, diffable, and recoverable; a DB adds state ownership problems (violates operating principles' no-unnecessary-abstraction).
8. **Dynamic plugin loading of engines in v1** — rejected: compile-time registry is simpler and sufficient; revisit only on real growth (§N.8).
9. **Python/Jupyter-first prototyping for the engine contract** — rejected: the engine contract must be C++ for VST donation (§M); Python remains analysis-only (hybrid, §J).
10. **Modeling PitchEngine as duration-modulating propagation (Doppler-style contract)** — rejected: category error inherited from the research report (§B.3.3); Doppler gets its own future contract.
11. **Fixed 20 kHz processing/analysis ceiling** — rejected: §6; per-engine `BandwidthSpec` + Nyquist-relative metrics instead.
12. **Implicit/ambient randomness (time-seeded RNG, `<random_device>`)** — rejected: §14 determinism; PCG64 with explicit per-job seeds.
13. **Engine-provided identity/bypass shortcut in the contract** — rejected: would defeat the ratio=1.0 contract test; engines must actually process.
14. **Material-suitability booleans as contract capabilities** — rejected: §D.5 — descriptive registry metadata only (artefacts may be features).
15. **Silent resampling of corpus assets to match experiments** — rejected: §I.1; resampling is an explicit, manifest-recorded operation.
16. **libsndfile / FFTW / JUCE as Lab dependencies** — rejected: §J matrix (own WAV I/O; permissive FFT; JUCE reserved for the future VST project).
17. **Config-file engine identity registry (the v1.0 `config/engines.toml` "reflected in code" concept)** — rejected: two independently authoritative engine lists (or a sync mechanism between them) is a dual-source-of-truth defect (Operating Principles §2, §80 canonical-path rule). Engine identity lives in code at one registration point (§D.6); config holds parameters/defaults only. *(v1.0 concept, removed in v1.1.)*
18. **Forcing RateFollowing engines into a fixed-length output contract (padding/truncating varispeed output to the input length)** — rejected: falsifies the reference model and hides the defining behaviour of the rate-following family; the contract must express differing input/output amounts (§D.4).
19. **Engine-side silent ratio saturation (engine clamps without reporting)** — rejected: violates information preservation across the engine boundary (Operating Principles §79); creative-mode saturation is harness-side, recorded as requested + effective + status (§F.4); also rejected: engine-side "effective ratio" reporting APIs — they would create a second authority and an engine-side test surface (§D.7).
20. **Freezing the exact rate-following C++ method signatures in this document** — rejected: the design defines the *semantic* contract (consumed/produced/end-of-input/flush/length policies, §D.4); signatures are implementation detail within those semantics — freezing them here would confuse architecture with implementation (Operating Principles §3).

---

## P. Operating Principles v3.0 reconciliation (correction item 5)

**Method:** the governing document (*AI Assisted Software Engineering Operating Principles v3.0*, 130 sections — stored at `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md`) was read **in full** and this design checked against it. The principles govern the engineering process; this document governs Pitch Lab architecture. Conflicts are resolved in favour of the principles by explicit amendment (this section records them); the principles are NOT rewritten into this document.

### P.1 Conflicts found and corrected

| # | Principle violated | v1.0 defect | v1.1 correction |
|---|---|---|---|
| C1 | §2 Source Of Truth / §80 canonical path | Dual engine registry (compile-time C++ + `config/engines.toml` "reflected in code") — two authoritative lists with an implicit sync mechanism | ONE authoritative registry in code (§D.6); config = parameters only; §O.17 |
| C2 | §38 terminology control / §5 data ownership | `src/analysis/` (source) vs top-level `analysis/` (generated) — confusable names for different ownership classes | Source under `src/` (incl. `src/analysis-py/`), generated under `artifacts/` (§B.3) |
| C3 | §79 data propagation / no silent information loss | Creative out-of-range rendering recorded only a flag (`override-ratio-range`); the effective ratio and what actually happened were not represented | Requested/effective/status semantics, preserved end-to-end (§E.5, §F.4) |
| C4 | §10 must not invent missing behaviour | Varispeed output-length, end-of-render and flush semantics undefined — an implementer would have had to invent them; v1.0's Preserving "output == input" also contradicted its own no-self-compensation latency rule | Normative §D.4 (consumed/produced/progress/end-of-render/length policies, worked example); Preserving canonical length amended to `N_in + outputLatency` |
| C5 | §28 failure model audit | Missing failure cases: unknown engine id; end-of-render violation | Added to §K |
| C6 | §37 documentation consistency | v1.0 `DurationBehaviour` comment cross-referenced "D.4", which was the rejected-fields section (dangling ref) | §D.4 is now the duration contract; old D.4–D.6 renumbered to D.5–D.7 |

### P.2 Verified as compliant (no change required)

Source-of-truth separation (authoritative vs generated trees, §B.2); architecture-vs-implementation discipline (DESIGN ONLY, tags, §D sketch header); ownership matrix and entity lifecycle (§B.2, §F.1); assumption control (§0.4, A-table); decision management and rejected-approach records (§O, change record); change discipline (version history table); testing strategy incl. contract tests (§L); reproducibility (§F.3, §L, `pitchlab verify`); scope control (§0, §0.5); dependency discipline (§J — every dependency justified, licence-verified, replaceable); implementation-claim qualification (status chain: everything in this document is **DESIGNED**, nothing claimed implemented — §81); independent review (this correction pass itself is an independent-review cycle per §18: AUDIT → REPORT → DECISION → MODIFY → VERIFY, with remediation explicitly authorised by the task brief).

### P.3 Design preferences explicitly NOT treated as defects (per §90)

File-based manifests vs a results database; TOML vs JSON for authoring; float32 I/O bus; CLI-first workflow. The principles do not mandate alternatives; v1.0's rejections (§O.7 etc.) stand as recorded design decisions.

### P.4 Process obligations transferred to the implementation phase (not satisfiable now — no code exists)

The following principles bind **implementation cycles** and are recorded here as gate conditions for the v0.1 implementation agent: development-start environment audit with fresh sources (§112); canonical development environment + dependency locking (§113, §123); bootstrap/installation validation (§114–§116); worklog as handoff entry point with reading order (§118); per-cycle recoverable package / single-ZIP handoff (§119–§122, §126); post-change checks (§52). This document cannot satisfy them because nothing is implemented; claiming otherwise would violate §81.

### P.5 Pre-implementation gate (§71) — mapping

| §71 gate item | Where defined | Status |
|---|---|---|
| Architecture defined | §A–§D | yes |
| Source of Truth defined | §B.2, §D.6 | yes |
| Scope defined | §0.1, §0.2, §0.5 | yes |
| State defined | §B.2, §F.1 | yes |
| Ownership defined | §B.1, §B.2, §F.1 | yes |
| Lifecycle defined | §F.1 | yes |
| Persistence defined | §B.2, §B.3, §F.3 | yes |
| Failure paths defined | §K | yes |
| Testing strategy defined | §L | yes |
| Versioning defined | §F.3 (runs), doc version header | yes |
| Configuration defined | §B.3 (`config/` role), §D.6 | yes |
| Recovery defined | §K (fix authoritative file + re-run), §L (verify gate) | yes |
| Open decisions identified | §N | yes (13 items, none structural) |
| Known ambiguities identified | §N + [OPEN DECISION] tags inline | yes |
| Protected constraints identified | §0.2, §0.3, §0.5, §G.1, §P.6 | yes |
| Validation strategy defined | §L, §26 | yes |

**Reconciliation result: CONSISTENT.** After corrections C1–C6, no conflict between this design and the Operating Principles v3.0 is known; residual items are open decisions (owner-owned) and implementation-phase process obligations (§P.4).

### P.6 Protected constraints (per §62)

The following are protected — changes require an explicit owner decision, never silent drift: the four-component model (§A/B); the single in-code engine registry (§D.6); the ratio-canonical curve model (§E); benchmark/creative separation with strict benchmark skip semantics (§F.4, §G.2); the §D.4 duration-behaviour contract; the authoritative-vs-generated tree split (§B.2/B.3); the v0.1 scope (§0.5); the non-goals (§0.2); the licensing isolation rules (§J).

---

## §26 Final architectural quality check (self-review)

| # | Question | Answer | Where |
|---|---|---|---|
| 1 | Understandable without this conversation? | **YES** — self-contained: scope, contract, layout, failure model, open decisions all in-document; upstream research findings tagged and referenced by section | whole document |
| 2 | Implementation agent can start without inventing architecture? | **YES** — components, ownership, contract semantics (incl. §D.4 duration/end-of-render), project layout, data flow, error taxonomy, and test strategy are resolved; the genuinely undecidable items are *listed* as open decisions instead of being silently decided | §B–§L, §N |
| 3 | Ownership explicit? | **YES** — ownership matrix, no shared mutable state, registry ownership (in-code, immutable per build), generated vs authoritative trees | §B.2, §B.3, §D.6 |
| 4 | Experiment lifecycle explicit? | **YES** — entity table with creation/owner/persistence/lifetime (incl. effective-curve entity for creative saturation) | §F.1 |
| 5 | Benchmark vs creative explicit? | **YES** — kind field, separated policies and namespaces, aggregation exclusion; creative saturation with requested/effective/status | §F.4, §G.2 |
| 6 | Dynamic pitch first-class? | **YES** — dense audio-rate curve signal + declared consumption rates + measured effective rates | §E |
| 7 | Extreme ratios without global ceiling? | **YES** — ratio domain, per-engine ranges, benchmark skip-not-clamp, creative saturation-with-record | §E.1, §F.4, §K |
| 8 | 192 kHz / wideband without redesign? | **YES** — rate-agnostic parameters, per-engine BandwidthSpec, Nyquist-relative metrics | §I |
| 9 | Third-party engines isolated? | **YES** — adapter-only access, native/external directory split, subprocess boundary for copyleft, verbatim licence files in `external/` | §D.2, §J |
| 10 | Generated outputs separate from authoritative state? | **YES** — generated trees under `artifacts/`, reconstruction gate (`pitchlab verify`) | §B.2, §B.3, §L |
| 11 | Future feedback without restructuring the engine contract? | **YES** — single-stage contract stays; feedback belongs to a future chain layer; tap-compatible buffer semantics; NaN guard already exists | §M |
| 12 | DSP donatable to the VST without the Lab becoming a runtime dependency? | **YES** — plain-C++ engine/DSP trees, no JUCE/GUI coupling, donation = re-vendoring | §M |

### Correction-pass final quality check (the 8 mandated questions)

| # | Question | Answer |
|---|---|---|
| 1 | Can the Varispeed semantics be implemented without inventing output-length behaviour? | **YES** — §D.4 normatively defines consumed/produced accounting, end-of-input + bounded flush, both length policies, expected-length computation and the worked example; only signatures and numeric tolerance (§N.6) remain open |
| 2 | Is there exactly one authoritative engine registry? | **YES** — in-code compile-time registration point (§D.6); config never holds engine identity; manifests are read-only mirrors; the v1.0 `engines.toml` identity concept is rejected (§O.17) |
| 3 | Are source and generated analysis paths clearly separated? | **YES** — `src/analysis/` + `src/analysis-py/` (source) vs `artifacts/analysis/` (generated); no confusable pair remains (§B.3) |
| 4 | Can creative mode exceed engine range without silently changing the requested value? | **YES** — requested curve is never mutated; effective curve + status recorded end-to-end; engines never silently saturate (§E.5, §F.4, §K) |
| 5 | Does the design comply with the governing Operating Principles? | **YES** — reconciliation performed (§P): conflicts C1–C6 corrected; result CONSISTENT |
| 6 | Is v0.1 implementation scope explicit? | **YES** — §0.5 binding list (resampling primitives + 5 engines + harness); do-not-implement list; post-v0.1 phases in §D.6 |
| 7 | Are no new unnecessary architectural components introduced? | **YES** — no new components, managers, layers, databases, servers, plugin frameworks, scanning or loading; all fixes operate on existing components and files (change record) |
| 8 | Are all affected sections internally consistent? | **YES** — consistency sweep performed over: RateFollowing/Varispeed, engine registry, `engines.toml`, `analysis/` paths, creative mode, clamping, Operating Principles references, v0.1 scope; stale references corrected (details in the worklog of this pass) |

---

## Closing statement

**This design is not implemented.** No code exists, no build exists, no corpus exists. Its status chain (per Operating Principles §81) is **DESIGNED** — and nothing more. The document is now **implementation-ready for the v0.1 scope** (§0.5): an implementation agent can build the harness + the six listed engine items from this document without inventing architecture, provided it (a) treats §N open decisions as owner questions, never silent defaults; (b) follows the implementation-phase process obligations recorded in §P.4; (c) uses §L as the acceptance gate. The next phase, when ordered, is: resolve the §N items owned by the product owner → implement v0.1 per the §B.3 layout → close each cycle per the Operating Principles lifecycle. Nothing in this document silently decides product behaviour beyond the [RESOLVED DESIGN] markers, and every such marker carries its justification inline.

**Primary upstream source:** `research/doppler-whip-pitch-research-report.md` — sections referenced: §A (Doppler physics), §B.1 (per-algorithm findings), §B.3 (architecture separation), §B.4 (licensing), §B.5–B.6 (benchmark and recommendations).

**Governing process document:** `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` (reconciled in §P; not reproduced here).
