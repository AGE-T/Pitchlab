# R16-P1 — Round-15 status reconciliation log (2026-10-28)

Task-book Priority 1: "Reconcile all Round 15 status contradictions. Audit
the current package against the final recovered source and test results."

Method: the delivered round-15 package (SHA-256 990b1d48cff8aa69186cd6855d15
12abacbfe829a2924a0d6a7934affc7208ec) and its living source tree
(`octavox-work/Octavox_RE_evidence/`, identical content) were audited against
what the CURRENT source actually contains and does. Pre-edit verification
(R16-R0, 2026-10-28): release build clean; `octavox_tests` 85/85 in 41.2 s;
`octavox_r15refcap` reproduces the four reference hashes exactly;
`octavox_bench` runs **12** cases (not 11 as several docs claimed).

Policy applied (per the task book): no historical evidence deleted; every
superseded statement kept in place and marked with a dated correction tag
`[R16-P1 CORRECTION/RECONCILIATION, 2026-10-28: …]`; this file is the
correction history.

## Corrections applied

| # | File | Superseded statement (round-15 text) | Reconciled statement (R16-P1) |
|---|---|---|---|
| 1 | `native/CMakeLists.txt` (salvage note) | "src/dsp.cpp is MISSING from this restored tree and must be recovered before anything builds." | Kept as the historical salvage note; marked SUPERSEDED — dsp.cpp re-implemented by R15-REB1, verified bit-neutral (4/4 reference hashes), tree builds, 85/85. |
| 2 | `native/CMakeLists.txt` (title comment) | "OCTAVOX round 12 -- native DSP core build" | "OCTAVOX native DSP core build (15.0.0)" — matches `project(... VERSION 15.0.0)`. |
| 3 | `native/README.md` (title + intro) | "OCTAVOX Native DSP Core (Round 12)"; intro described only the round-11/12 design | Title → 15.0.0; intro now describes the current API incl. the per-voice parameter surface, wet/dry, bypass, block-boundary semantics; round-12 history preserved in the version-history section. |
| 4 | `native/README.md` (layout) | "tests/test_main.cpp … (51 cases)"; missing entries for the round-15 files | 85 cases; layout extended with `r15_acceptance_cases.cpp.inc`, `cli/r15ref_capture.cpp`, `r15_reference_hashes.txt`, `VST3_READINESS.md`, `ARCHITECTURE.md`; bench described as 12 cases. |
| 5 | `native/README.md` (recorded results) | "51/51 pass"; round-12-only benchmark description | 85/85 release + ASan/UBSan; benchmark section updated with the 96 kHz (2.0–2.1× RT) and active-surface rows; neutrality-hash line added; the round-12 cross-language record kept and labelled as such. |
| 6 | `native/VST3_READINESS.md` (context caveat) | "the implementation was being recovered at the time of writing" (implying uncertainty about the shipped state) | Caveat marked HISTORICAL (mid-incident); R16-P1 reconciliation block added: dsp.cpp re-implemented + verified, 85/85 delivered, JSON + bench files shipped. |
| 7 | `native/VST3_READINESS.md` (§8) | "96 kHz and active-surface rows … pending re-measurement after the tree recovery" | Measured and delivered: 96k 4998.30/5211.17 ns/frame = 2.08×/2.00× RT, surface 1520.40 = 13.70×, 0 allocs (`r15_bench_15.txt`). |
| 8 | `native/VST3_READINESS.md` (overall verdict) | "the battery's 7 new r15acc_* cases are designed and salvaged …, not yet run" | Reconciliation block: the cases were wired by R15-REB2 and the delivered suite is 85/85 (release + ASan/UBSan) with the machine-readable result file shipped; the wrapper milestone moves to the round-16 task book. |
| 9 | `native/r15_reference_hashes.txt` (NEXT AGENT footer) | "the 7 r15acc_* acceptance cases … are designed but NOT wired" | Kept; marked SUPERSEDED — wired (R15-REB2), 85/85 delivered; the regression re-run instruction STANDS; re-verified 2026-10-28 (all four hashes match). |
| 10 | `evidence/analysis_round15/NOTES_R15P2.md` | Document ended with the mid-incident "Honest task status": Tasks A/B NOT RUN, no runnable engine | Original text preserved unchanged; a "FINAL STATUS — R16-P1 reconciliation addendum" section appended: what was lost at the intermediate stage, what was subsequently restored and verified (R15-R1/REB1/REB2/D + R16-R0 re-verification). |
| 11 | `native/include/octavox/dsp.hpp` (title) | "OCTAVOX rounds 13/14 recovery -- … (public API)" | Title → "OCTAVOX independent native DSP core (public API) -- version 15.0.0" (the version-history block already records the recovery lineage). Comment-only change; the contract text is untouched. |
| 12 | `native/include/octavox/dsp.hpp` (salvage note) | Undifferentiated R15-P2 salvage note ("restored VERBATIM … after the second environment reset") | Reframed as the HISTORICAL SALVAGE NOTE (incident record, kept) + an R16-P1 note stating the final implementation status (header text = original 15.0.0 contract; implementation re-built, verified bit-neutral, 85/85). |
| 13 | `DELIVERY_ROUND15.md`, `docs/OCTAVOX_ROUND15_FINDINGS.md`, `README.md` (reproduction blocks) | "./build/octavox_bench # 11 cases, 0 allocations" | "12 cases" — the delivered bench runs 12 rows (9 round-12 + 2× 96 kHz + 1 surface; `r15_bench_15.txt` has 12 data rows). The "11" was an off-by-one carried from the worklog's "all 11 cases" phrasing. |
| 14 | `docs/OCTAVOX_ROUND15_FINDINGS.md` §7 | "octavox_tests # 85/85 (≈30 s)" | Actual measured duration 41.2 s (release, R16-R0, this environment) — the ≈30 s figure was an estimate extrapolated from the pre-reset 78-case/28 s run, never a measured 85-case duration. |
| 15 | `docs/OCTAVOX_ROUND15_FINDINGS.md` §7, `DELIVERY_ROUND15.md`, `README.md` | `export PATH=/home/z/.local/bin:$PATH` (hard-coded) | Environment-drift note added: cmake is pip-installed per environment (round-15 sessions: `/home/z/.local/bin`; round-16 environment: `/home/z/.venv/bin`) — put the directory containing cmake on PATH. |
| 16 | `native/ARCHITECTURE.md` (§Build and test) | "Release tests: 51/51; ASan+UBSan build: 51/51" | 85/85 release + ASan/UBSan; benchmark 12 cases; results in `evidence/analysis_round15/` (found by the audit though not listed in the task book — same class of stale count). |

## Consistency verification (after the corrections)

- Version references: every current-status statement now says 15.0.0 /
  85 cases / 12 bench rows / 4 reference hashes; the 12.0.0→14.0.0→15.0.0
  lineage stays in the version-history records only.
- Provenance statements (two environment resets, reconstructed records,
  originals preserved) were already consistent across DELIVERY_ROUND15.md,
  the findings document, README.md, NOTES_R15P2.md and the worklog — no
  change needed beyond the addendum in NOTES_R15P2.md.
- File counts: the package ships 297 content files + manifest = 298 zip
  entries (unchanged by P1; new round-16 files are added only in the
  round-16 delivery, which regenerates the manifest).
- The corrections are comment/markup-only in every source or build file —
  the compiled library, the test suite and the four neutrality hashes are
  unaffected (re-verified after the edits: build clean, 85/85, 4/4 hashes —
  see the R16-P2 full-suite run).

## What was NOT changed (deliberately)

- No test gate, no recorded measurement, no static-analysis verdict and no
  incident record was deleted or silently rewritten.
- The Python reference implementations (`poc/`) and all pre-round-15
  evidence directories are untouched.
- The superseded statements remain readable in place, tagged with the
  correction date and pointer to this log.
