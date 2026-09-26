# Pitch Lab

**Local DSP research laboratory for sound-design-oriented pitch processing.**
Product: knowledge and tested DSP components — not shippable audio software.

> Current status: **IMPLEMENTATION FREEZE** — specification, repository
> reconciliation and CI infrastructure complete. No DSP engine is
> implemented; none is faked (the engine registry is deliberately empty and
> CI asserts it). The v0.1 build can start without hidden design decisions.

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
test → evidence upload, zero third-party dependencies at freeze. CI proves
the environment and the infrastructure (toolchain, FP determinism guards,
engine-registry freeze state); it does **not** prove any DSP behaviour.

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
