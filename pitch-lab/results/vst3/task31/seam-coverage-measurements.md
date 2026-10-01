# Task 31 — Retiring-Chain Seam Coverage: Root Cause, Fix, and Measured Matrix

**Date:** 2026-10-01 · **Baseline:** Task 30 (8ea9ef0) · **Scope:** the realtime
adapter's retiring-chain seam coverage for extreme SAME-engine latency growth
(+ the reverse direction, found defective by the required matrix).

---

## 1. The defect (measured, then proven)

Scenario: `native.granular`, grain `0.1 s → 0.5 s` mid-stream (48 kHz, block
512, host-paced). The chain signature changes → a genuine rebuild → the new
chain (Λ 15638 → 34838; ΔΛ = 19200 = the grain growth exactly) is adopted at
`t_a = 48640`.

**Pre-fix measurement (temporary instrumentation, since removed):**

```
underrun q ∈ [13802, 27359]  — 13558 frames, all deliveryUnderruns
q_start   = t_a − Λnew = 48640 − 34838 = 13802          (EXACT)
q_end+1   = s_oldest_alive + seamX = 26640 + 720        (EXACT)
```

The task-30 record (14406 frames) is the same mechanism at a different
adoption grid phase (the burst size = ΔΛ − wetLen + gridPhase + seamX, phase
∈ [0, advance)).

## 2. Root cause (Task B — the 9-candidate determination)

**A combination of #3 and #8 (#9):** at adoption the Λ_eff monotonic policy
moves the emission read position BACKWARD by ΔΛ (re-covering already-emitted
frames — the frozen spec §4.1 item 5's own declared semantics). The retiring
chain is the sole wet source for `q < act->base`, but:

* the jobs covering the re-read region are **dead** (the old-timeline
  emission passed their `wetEnd`) and **recycled** — the prep thread
  re-stamps a dead job's slot for job `k+kJobSlots`, wiping its lane — so
  `readWet` fails the `job->index != k` check and the emission fell back to
  DRY for the whole uncovered span;
* the lanes were SIZED for the worst-case history (spec §4.1 item 5) but the
  job-recycling data lifecycle destroyed the content long before the
  retention horizon mattered.

Not causes (each refuted by measurement): scheduling distance (#1/#2 — the
retiring production runs ~Λ ahead), dry retention (#4 — 0 dry misses),
adoption timing (#5/#6 — the burst size is phase-dependent but never zero for
ΔΛ > wetLen), the blend timeline (#7 — the +seamX extension is readWet's
correct blend semantics).

## 3. The reverse direction (found by the required "test both directions")

`grain 0.5 → 0.1` (latency DECREASE): the emission timeline is unchanged
(Λ_eff never decreases), but the retiring chain — still the sole wet source
for `q < act->base` — had its job grid **frozen at adoption**: `serveJobNeeds`
served only active+pending. Its grid ceiling could not reach `act->base`
(pre-adoption the next jobs were victim-starved: deaths lag the emission by
Λ; post-adoption nobody schedules). Measured: **15680 frames** =
`[s_{next-job}, actBase)` of ungenerated wet → dry fallback.

## 4. The fix (adapter-level; the engines, the parameter architecture, the
Task-30 clamp policy are untouched)

1. **The retained wet history** (`Chain::history`, a LaneWindow per chain,
   allocated at build): at each job DEATH the produced wet beyond the current
   history top is copied BEFORE the dead flag lets the prep thread recycle
   the slot — the raw region by memcpy, the final `seamX` frames through the
   SAME sin/cos blend `readWet` uses (the dying job faded out, the successor
   faded in; both lanes are alive at that moment). The retained content is
   bit-identical to what `readWet` returns while the lanes live. The
   retention horizon is the same worst-case constant the lanes/dry lane are
   sized from. This implements the retention spec §4.1 item 5 already
   declares ("the job lanes ... retain (and are sized for) the
   parameter-range worst-case history so a mid-stream Λ_eff growth never
   outruns the retained wet/dry").
2. **The retiring emission fallback**: when `readWet` misses inside the
   covered range, the history serves the frame — counted in the new
   `seamRecoveries` diagnostic (a cadence figure like `chainsAdopted`, NOT a
   fault; the aggregate `faults` composition unchanged). Genuinely-missing
   positions keep the honest underrun + dry-fallback path.
3. **The retiring grid continuation** (the reverse-direction fix): the
   retiring chain is published to the prep thread (`retiringPub`, clear-
   before-push ordering; the retire stack is freed only on the prep thread
   itself) and `serveJobNeeds` continues its scheduling, bounded by
   `retiringServeLimit` (the last wet position the emission reads from it =
   `act->base + seamX`): the grid advances exactly as far as it is read,
   victims free as the emission passes their wet, and jobs past the bound
   are never scheduled (no wasted engine work).
4. **The stall-detector `everFed` guard**: jobs deliberately never fed (the
   retiring grid past the blend end — a documented no-op after fix 3's bound)
   are not stalls; the detector's documented semantics ("fully-fed but
   unfinished") is what the code now checks.

## 5. The measured matrix (vst_seam_test, 10 cases / 152 assertions)

| case | drive | result |
|---|---|---|
| T-S1 | grain 0.1→0.5 @48k/512/paced | faults 0, seamRecoveries 13558/ΔΛ 19200, **replay 19200/19200 bit-identical**, RMS 0.326/0.346/0.341 |
| T-S2 | grain 0.1→0.2 (moderate) | faults 0, replay exact (serve count phase-dependent 0..6) |
| T-S3 | grain 0.5→0.1 (reverse) | faults 0 (was 15680), seamRecoveries 0, Λ_eff stays 34838 |
| T-S4 | pv.classic fft 2048→4096+hop (long-window control) | faults 0, replay exact |
| T-S5 | blocks 128/256/512/1024/2048 + irregular {97,512,397,1024,311,2048,128} | faults 0, replay 19200/19200 every schedule; seamRecoveries 12358–14662 (grid phase) — **the 512 row serves exactly the task's 14406 frames** |
| T-S6 | 44.1/48/96 kHz | faults 0, replay exact (17640/19200/38400 = ΔΛ at each rate) |
| T-S7 | paced vs back-to-back | faults 0 both, replay exact both (the seam invariant is cadence-independent) |
| T-S8 | determinism (two runs) | the invariant set deterministic; the per-run replay exact (mid-stream adoption positions are async by design — the artifact's phase-G adoption-robust rule) |
| T-S9 | extreme jump at −12 st + LFO 2 | clamps 0, re-prepares 2 — the Task-30 policy intact under the jump |
| T-S10 | varispeed→granular (cross-engine) | faults 0, replay exact |

Plus the real VST path (vst_processor_test, "Task 31: same-engine extreme
configuration change mid-audio"): instantiate → activate → process → grain
0.1→0.5 mid-audio → rebuild → adoption → seam → continued processing →
suspend/resume → state restore: **faults 0, seamRecoveries > 0, re-prepares
exactly 2, latency 15638 → 34838 reported coherently, the configuration
round-trips through state.**

## 6. The audio result, classified (Task I)

The extreme jump's re-coverage window is now a **bit-identical repeat** of
the previously-emitted wet (19200/19200 frames at every schedule/rate) — the
Λ_eff re-coverage policy's declared cost ("re-covering already-emitted
frames"), rendered correctly instead of as a ~300 ms dry burst. RMS/peak
windows around the seam are continuous (the same signal content); the sample
step at the boundary is the repeat's phase jump (bounded by the signal
slope). Classification: **intentional configuration transition** (the
moderate jump: **seamless**; the long-window engines: **seamless**).

## 7. Regression evidence

* CTest **40/40** (39 + the seam suite); the audio-path artifact
  **BYTE-IDENTICAL** (git-clean — the fix changes output only where the
  defect was: the previously-faulting re-coverage frames);
* ASAN+UBSAN+LeakSanitizer clean on 6 suites (seam 10/10, adapter 34/34,
  parameters 10/10, engine-params 13/13, diagnostics 5/5, processor 22/22);
* the real X11/VSTGUI lane: the granular panel renders, switch-stress 11
  survived, ui-binding **ALL CHECKS PASS** (the 20/20 engine-switch matrix
  fault-free — heavy retiring-path exercise);
* the SDK validator **47/47**; VST3 artifact
  `build/VST3/Release/PitchLab.vst3` (module SHA-256
  `7401384e5a41e556a9bd9430a2820dc4d167a64fb520ca8bac670ee96819d136`).

## 8. Remaining limitations

* The re-coverage repeat itself (ΔΛ frames replayed) is the frozen Λ_eff
  policy's inherent cost — documented, not a defect (the alternative, a
  ramping latency timeline, would be a semantic redesign outside this task);
* the 96 kHz granular RTF (the windowed-splice CPU cost) — unchanged, the
  known primary cause from Task 30;
* the retained-history seam-tail blend fallback (raw copy when the successor
  lane is not yet produced) is unreachable for the splice engines by the
  structural pipeline margin and unreachable for the long-window engines
  (their tail reads precede any recycle) — defensive only, documented;
* the previously recorded granular realtime downshift limitation and the
  pv.phaselocked single-partial amplitude characteristic — intact, untouched.
