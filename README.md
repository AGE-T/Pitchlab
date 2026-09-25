# Pitch Lab

**Local DSP research laboratory for sound-design-oriented pitch processing.**
Product: knowledge and tested DSP components — not shippable audio software.

> Current status: **DESIGN ONLY.** Research and architecture are complete;
> implementation (v0.1 scope) has not started. Everything here is documented,
> classified, and traceable per the governing Operating Principles.

---

## Documentation index (authoritative docs)

| Document | Path | Status |
|---|---|---|
| **Architecture & Design Document v1.1** — components, PitchEngine contract, pitch-curve model, benchmark/analysis architecture, licensing matrix, failure model, testing strategy | `research/pitch-lab-architecture-design.md` | DESIGNED (938 lines) |
| **Unified Technical Research Report** — whip physics, acoustics/Doppler, time-domain & spectral pitch shifting, libraries & licensing (5 research tracks, claim-tagged) | `research/doppler-whip-pitch-research-report.md` | RESEARCH COMPLETE |
| **AI Assisted Software Engineering Operating Principles v3.0** — governing engineering-process document (130 sections) | `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` | GOVERNING DOCUMENT |
| **Architecture Design v1.0 (archive)** — superseded by v1.1, kept as historical evidence | `research/archive/pitch-lab-architecture-design-v1.0.md` | ARCHIVED |
| **Worklog** — full history of every task executed on this project | `worklog.md` | LIVING |

## Artifacts

Generated outputs land in the `artifacts/` tree (never in source trees, per
design §B.2 / Operating Principles §38):

```
artifacts/
  renders/    # rendered audio from experiments (offline renderer output)
  analysis/   # metric modules' computed results (per render / per suite)
  reports/    # generated reports (listening index, benchmark summaries)
```

When the v0.1 implementation starts, every artifact produced by a run will be
committed and pushed here together with the docs.

## Keeping GitHub in sync

The repository is synced with the local project via a script. The token is
stored in the **gitignored** `.env` (never committed):

```bash
# one-time setup (.env):
#   GITHUB_TOKEN=github_pat_...
#   GITHUB_REPO=AGE-T/Pitchlab

bash scripts/push-to-github.sh              # commit + push docs & artifacts
bash scripts/push-to-github.sh "my message" # custom commit message
```

## Scope snapshot (v0.1, binding — design §0.5)

**Included:** resampling primitives, Varispeed, Variable Delay, Classic PV,
Phase-Locked PV, Granular, and the experiment harness.

**Excluded (do NOT implement):** WSOLA, TD/FD-PSOLA, SMS, WORLD, HNM, neural
engines, external adapters, Doppler/propagation/shock/spatial/feedback systems,
VST wrapper, GUI.

## Source layout (this repo also contains the research-workbench web app)

```
research/     # authoritative documents (see index above)
artifacts/    # generated outputs (see above)
scripts/      # GitHub sync helper
src/          # Next.js docs & artifacts workbench (browse the docs in-app)
prisma/       # database schema for the workbench
worklog.md    # task history / evidence trail
```

---

*Date: 2026-09-25 · Owner: AGE-T · Assistant: Z.ai*
