# R15-P2 SESSION NOTE -- second environment reset mid-task (2026-10-26, ~20:54 UTC)

## What happened

- The round-15 P2 session started from the verified state: tree at
  `/home/z/octavox/work/Octavox_RE_evidence/native`, version 15.0.0,
  **78/78 tests green** (release, 28 s — actually re-run by this agent at
  session start; ASan greenness was recorded by P1), P1 parameter surface
  implemented and behaviour-neutral by default.
- The full normative header (`include/octavox/dsp.hpp`), the complete
  implementation (`src/dsp.cpp`, 1097 lines), the relevant parts of
  `tests/test_main.cpp`, `bench/bench_main.cpp` and `CMakeLists.txt` were
  read into the session BEFORE any edit (per the task's "read it first").
- While the first (and only) source edit was being applied, the sandbox
  environment was reset: `/home/z/octavox` was wiped to a snapshot state
  (the same failure mode as the round-13/14 loss documented as
  "R15-R0 / R15-E1" in the worklog — this is the SECOND occurrence).
  The build/ tree, the 78-test suite, all round-14/15 evidence files under
  `evidence/` (except what is re-created here) and the P1 capture tool
  `r15p1_refcapture/` were lost. What survived: `/home/z/my-project`
  (worklog + the round-8/10/11/12 evidence packages in `download/`).

## What was salvaged / re-created (this session, after the reset)

Recovered tree at `/home/z/octavox/work/Octavox_RE_evidence/`:

- `native/include/octavox/dsp.hpp` — **restored VERBATIM** from the
  session's read of the intact 15.0.0 file (the normative ROUND-15 contract
  included; only a dated salvage note added).
- `native/src/obs_kernel_table.h`, `native/tests/alloc_counter.cpp`,
  `native/tests/wav_io.{cpp,hpp}`, `native/cli/octavox_cli.cpp` —
  byte-identical carry-over files extracted from the preserved
  `download/Octavox_RE_evidence_round12.zip` (these predate the lost rounds
  unchanged: the OBS kernel table is frozen observed data; the harness
  utilities' interfaces are what the 15.0.0 sources compile against).
- `native/CMakeLists.txt` — restored 15.0.0 form minus the lost
  `octavox_r14probe` target (source lost), with a salvage note.
- `native/bench/bench_main.cpp` — the intact 15.0.0 benchmark **with the
  R15-P2 (Task B) extensions already applied**: `sample_rate` + `surface`
  parameters on `runCase`, the 96 kHz variants (`mono_1v_b128_96k`,
  `mono_8v_b128_96k`) and the active-surface case
  (`mono_8v_b128_surface`: per-voice ratios != default, voices 1+6 muted,
  wet 0.5, three mid-run `setVoiceGain` retargets issued INSIDE the timed
  loop), JSON rows extended with `sample_rate`/`surface`.
- `native/tests/r15_acceptance_cases.cpp.inc` — the **fully designed
  13-category completion of the acceptance suite** (the 7 `r15acc_*`
  cases, gates fixed before running, ~6 s extra suite runtime), ready to
  merge into a recovered `tests/test_main.cpp` (merge instructions in the
  file header). This was the edit in flight when the tree vanished.
- `evidence/analysis_round14_recovery/r14_baseline_bench_stdout.txt` —
  restored verbatim (the P5 comparison baseline, re-typed from the
  session's read of the intact file; contents identical line-for-line).

## What is MISSING (must be recovered before the battery can run)

- `native/src/dsp.cpp` — the 15.0.0 implementation is LOST. ~87% of it was
  read into this session (all of the R15-P1 surface, process(), all
  setters, prepare/reset, MultichannelShifter, config validation) but the
  interpolator-table constructor and most of the `PitchDetector` class
  (lines ~52-179, incl. the R13-b sample-rate-relative window law) were
  never read. The round-12 zip carries the round-12 `dsp.cpp`
  (pre-R13-b/R13-c/R14/R15-P1). Recovery path (same as round-14): rebuild
  the R13-b/R13-c/R14/R15-P1 deltas test-first against the round-12 base;
  the 15.0.0 API contract is fully specified by the restored header, and
  the default_neutrality hashes (in the worklog / test file:
  0xc7179361b0a1ca4f / 0xa894e57671084f9f / 0x638ce760ed3cd10a over the
  three recorded configs) verify bit-exactness of any rebuild.
- `native/tests/test_main.cpp` — the 78-case suite is LOST (only
  ~55% was read into the session: the framework, gen/meas helpers, the
  corrected `meas::pitchFFT`, `casesR15P1` (all 7 P1 cases verbatim), the
  JSON report and main()). The round-12 zip has the 51-case round-12
  version. The round-13/14 sections (cold_start_*, detector_96khz_*,
  rt_*, tracker-parity) must be rebuilt as in round-14 recovery, then the
  `r15acc_*` battery merged from the .inc file.
- `native/README.md`, `native/ARCHITECTURE.md`, `native/cli/r14recovery_probe.cpp`,
  the P1 `r15p1_refcapture/` tool, `evidence/analysis_round{13,14_recovery,15}`
  contents (except this directory and the restored baseline stdout).

## Honest task status (R15-P2P5)

- Task A (13-category acceptance suite): the 7 new cases are DESIGNED and
  gate-fixed (salvaged in `r15_acceptance_cases.cpp.inc`) but **NOT RUN**
  — no runnable engine exists. `r15_acceptance_results.json` was NOT
  produced.
- Task B (P5 bench): the extended benchmark binary source is ready
  (`bench/bench_main.cpp` with 96 kHz + surface cases) but **NOT RUN**;
  `r15_bench_15.json` / `r15_bench_comparison.md` were NOT produced. The
  comparison baseline is preserved (restored stdout above). The P1 session
  had already re-measured 15.0.0 vs the baseline: unchanged (1v 15.47x RT,
  8v 13.73x, ctrl512 50.66x, 0 allocs, RSS +2 MiB/engine for the dry ring)
  — recorded in the worklog R15-P1 entry.
- Task C (`native/VST3_READINESS.md`): WRITTEN (see the file) — it is an
  API/contract assessment and cites only recorded measurements.
