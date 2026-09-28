# Pitch Lab

**Local DSP research laboratory for sound-design-oriented pitch processing.**
Product: knowledge and tested DSP components — not shippable audio software.

> Current status: **IMPLEMENTATION PHASE — cycles 1-5 complete; the v0.1
> engine registry AND analysis layer are COMPLETE** (2026-09-26/27, implementation specification
> §17 steps 1-4): cycle 1 = core types, deterministic RNG, WAV I/O and the
> shared resampling primitive; cycle 2 = the curve library (own TOML-subset
> parser, spec validation, deterministic compilation to the dense per-frame
> ratio signal — §4.4.3.1), the 21-file curve battery, and the deterministic
> synthetic corpus (17 items committed under `pitch-lab/assets/corpus/`,
> regeneration byte-identity gated by T-C1); cycle 3 = the first real
> engine — `native.varispeed`, the rate-following reference (§6.1/§6.1.1) —
> plus the harness (ExperimentCompiler → OfflineRenderer → canonical-JSON
> manifests with SHA-256 provenance, CLI compile/render); **cycle 4 = ALL
> FOUR remaining engines, each frozen (§6.x.1) BEFORE coding and run through
> the SAME contract suite T-E1..T-E13 — `native.vardelay` (§6.2),
> `native.granular` (§6.5), `native.pv.classic` (§6.3, pocketfft vendored
> BSD-3 per the documented plan) and `native.pv.phaselocked` (§6.4, the
> L-D'99 peak-shift phase-locked vocoder)**. 18 CTest tests, CI per §13.3;
> the registry contains EXACTLY the five implemented v0.1 engines — the §14
> end state, asserted by T-E19 (nothing more is registered; nothing is
> faked). The T-LEN-CAL length-calibration evidence for OD-6 is produced
> (worst |Δ| = 8 frames over the full battery × 6 rates, §13.4 —
> ratification pending). Resampler acceptance evidence and the open decision
> it produced (OD-18) are recorded in the implementation specification
> §7.7.1. **Cycle 5 (2026-09-27, §17 step 5) = the ANALYSIS LAYER — complete**: the 15-metric analysis
> stack per the frozen §10.4 (metric registry as the single in-code identity authority; pure-function metric
> modules; the Analyzer reading render manifests + SHA-256-verified masters + hash-cross-checked curve
> recompilations; the `pitchlab analyze` CLI; deterministic `pitchlab.analysis.v1` artifacts under
> `artifacts/analysis/` — byte-identical on delete + re-run). Analytic goldens landed for every implemented
> metric (seven T-M suites; the varispeed self-reference spectral error ≡ 0 exactly; the 8-onset
> percussive-recipe golden; bin-centred leakage-floor classes); tracker-dependent metrics are honestly gated
> `tracker-unavailable` (OD-12 clean-room pYIN route — no substitute estimator) and cpu-cost gated
> `not-applicable`; NO global quality score exists anywhere. 25 CTest tests, CI per §13.3; the 60-job
> cross-engine metric matrix evidence in §13.6 (60/60 analysed, zero analysis errors). OD-9/OD-6/OD-18
> remain OPEN. **Cycle 6 (2026-09-28, OD-12) = the CLEAN-ROOM C++ pYIN REFERENCE PITCH TRACKER —
> complete** (per the frozen implementation specification §10.5, recorded BEFORE coding: the Mauch &
> Dixon 2014 algorithm + librosa PUBLIC API documentation parameter values, NO external pYIN source
> imported/read-for-derivation/linked; YIN CMND + 100-threshold Beta(2,18) prior + parabolic
> refinement + the 2M-state HMM + Viterbi + forward-backward posteriors; the corpus [f0] recipe
> metadata extension with byte-identical signals; pitch-error/pitch-lag/warble-instability now
> MEASURED with analytic goldens — evidence §13.7: sub-cent identity medians, the delayed-curve lag
> golden == 4 frames EXACTLY, the control-rate lag discriminants separating the engine families;
> CTest 25 → 27, ASAN+UBSAN clean, the 60-job matrix re-run byte-deterministic with zero errors).
> **Cycle 7 (2026-09-28) = §17.6 THE FINAL GATE — PASS, and the RESULT PRODUCT** (owner
> final-gate directive): the repository-level verification & reproducibility gate closed
> with existing evidence reconciled + the genuinely missing checks executed (clean-checkout
> CI runs 36407493452 @ 1eee3a3 and 36408075811 @ 99d4c56 re-verified via the GitHub API;
> full generated-tree regeneration 180/180 files byte-identical; corpus `--verify` 17/17;
> hidden-dependency scan clean; vendored-dependency provenance sha256 4/4; source/build
> separation and documentation/SoT consistency verified — spec §17 step 6). Then, per the
> same directive, the first tangible v0.1 RESULT PRODUCT: `pitch-lab/results/v0.1/` — the
> committed comparative five-engine battery (30 jobs: 3 materials × 2 curves × 5 engines)
> with float32 listening renders, analysis JSONs and the human-readable report
> (`results/v0.1/README.md`; evidence §13.8). `report|verify|listen-index` CLI
> subcommands stay unimplemented by owner decision (§4.8.1 item 10). OD-6/OD-9/OD-18
> remain OPEN. **v0.1 is COMPLETE: no further verification gate exists (§17.6 is the last
> gate); the roadmap continues at the owner's direction.**

This repository hosts **two systems with a hard boundary**:

| | Agent / Web Workbench | Pitch Lab DSP research system |
|---|---|---|
| Where | repo root (`src/`, Next.js app) | `pitch-lab/` (C++20) |
| What | observability workbench for the owner (docs browser, artifacts browser, GitHub sync) | offline DSP laboratory (architecture's four components) |
| Rule | never links, calls or configures DSP code; only observes files | never depends on the workbench |

## Documentation index (authoritative docs, `research/`)

| Document | Status |
|---|---|
| **V0.1 Implementation Specification** — frozen C++ contracts, buffer ownership, frame accounting, 5 engine sheets, resampling, WAV/analysis/corpus/test specs, TOML schemas, open decisions | `research/pitch-lab-v0.1-implementation-specification.md` |
| **Build & CI Environment Specification** — canonical pinned GitHub Actions environment, dependency policy, exact commands | `research/pitch-lab-build-and-ci-environment.md` |
| **Architecture & Design Document v1.1** | `research/pitch-lab-architecture-design.md` |
| **Unified Technical Research Report** | `research/doppler-whip-pitch-research-report.md` |
| **Operating Principles v3.0** (governing engineering process) | `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` |
| Architecture v1.0 (archived evidence) | `research/archive/pitch-lab-architecture-design-v1.0.md` |
| **Worklog** (current-state block at top + full history) | `worklog.md` |

## CI (canonical build/test target)

`.github/workflows/ci.yml` — pinned `ubuntu-24.04`, GCC 13.3.0 (empirically
re-pinned 2026-09-26 from live CI evidence), CMake 3.31.6
(sha256-verified tarball), Ninja, CTest. Clean checkout → configure → build →
test → evidence upload. Dependencies: vendored doctest 2.4.12 (MIT,
`pitch-lab/external/` — dev/test only; the DSP core is zero-dependency). CI
proves the environment, the infrastructure (toolchain, FP determinism
guards, engine-registry freeze state) and — since implementation cycles 1-2
(2026-09-26, spec §17 steps 1-2) — the unit-level correctness of the
foundational components (types, RNG, WAV I/O, resampler, curve compiler,
corpus generator); it does **not**
prove any engine behaviour (no engine exists — the registry test asserts
emptiness).

**Activation status (2026-09-26): ACTIVATED AND PROVEN.** The workflow is
tracked in git and executing on GitHub Actions. Proof: run
[36237247935](https://github.com/AGE-T/Pitchlab/actions/runs/36237247935)
(head 593a2d5) — every step green, CTest 3/3 passed, and the `ci-evidence`
artefact (configure.log, build.log, test.log, JUnit test-results.xml) was
downloaded and content-verified. Activation took two honestly-recorded
empirical fixes (GCC re-pin 13.2.0→13.3.0 per live evidence; `mkdir -p`
for the runner's missing `~/.local`) — full chronology in
`research/pitch-lab-build-and-ci-environment.md` §12.1 (OD-17, resolved).

## Artifacts

Generated DSP outputs live in `pitch-lab/artifacts/{renders,analysis,reports}`
(gitignored, reconstructable bit-for-bit for deterministic engines — never
source of truth). The web workbench observes that tree. Whether selected
generated artifacts are ever published to GitHub is owner decision OD-15.

## Keeping GitHub in sync

The token lives in the **gitignored** `.env` (`GITHUB_TOKEN`, `GITHUB_REPO`):

```bash
bash scripts/push-to-github.sh              # commit + push everything tracked
bash scripts/push-to-github.sh "my message" # custom commit message
```

## v0.1 scope (binding — architecture §0.5 / implementation spec §1.1)

**Included:** shared resampling primitives, `native.varispeed`,
`native.vardelay`, `native.pv.classic`, `native.pv.phaselocked`,
`native.granular`, the harness (compiler/renderer/analyzer/reporter/CLI),
in-code engine registry, curve library, C++ metric modules, deterministic
synthetic corpus generator.

**Excluded:** WSOLA, PSOLA variants, SMS, WORLD, HNM, neural, external
adapters, Doppler/propagation/shock/spatial/feedback, VST wrapper, GUI.

## Source layout

```
pitch-lab/   # C++20 DSP research system (config, experiments, assets, src, tests, tools, external)
research/    # authoritative documents (single location)
src/         # Next.js workbench (observability only)
scripts/     # workbench GitHub-sync tooling
worklog.md   # task history / current state
```

---

*Owner: AGE-T · Assistant: Z.ai · Freeze date: 2026-09-25*
