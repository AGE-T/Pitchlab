# Task 28 candidate research artifacts

**Scope:** research-only evidence for the five re-evaluated candidates
(OLA / WSOLA / TD-PSOLA / FD-PSOLA / Transient-aware PV) — see
`research/task28-candidate-re-evaluation.md` (the task-28 report) for interpretation.

**Contents (per candidate):**
* `<id>/candidate-report.json` — the full 12-material × 10-transform matrix at the default
  configuration + the parameter-effect matrix (canonical JSON, sorted keys, %.17g doubles).
  Deterministic: same binary + same inputs => byte-identical.
* `<id>/rtprobe.json` — the realtime feasibility probe (per-block timing, RTF, allocation
  audit, block-size sweep, block-split invariance). **Timing fields are local evidence
  (machine-dependent); allocation counts and invariance results are deterministic.**
* `<id>/renders/*.wav` — curated float32 listening renders (drum/vocal/metallic × ±12 st).
* `baseline/existing-engines.json` — the five frozen v0.1 production engines (default
  configurations) driven through their real PitchEngine contract on the same corpus and
  measurement layer (identity/+12/−12/ramp-fast) — the differentiation baseline. Read-only
  reuse; the production registry is untouched.

**Regeneration (same-binary determinism):**
```
build/prototypes/task28_ola       --out pitch-lab/results/research/task28-candidates
build/prototypes/task28_ola       --rtprobe --out pitch-lab/results/research/task28-candidates
build/prototypes/task28_baseline  --out pitch-lab/results/research/task28-candidates
# (per candidate; rtprobe timing fields are local evidence, not byte-stable across machines)
```

**ctest entries:** `task28_<id>_selftest` (fast invariant suites: identity exactness,
duration, finiteness, determinism, allocation audit, block-split invariance — no source-tree
writes).

**Boundary:** research-only; nothing here is reachable from the VST3 product layer; the
frozen v0.1 engine sources are never modified (baseline reuse is read-only through the real
contract).
