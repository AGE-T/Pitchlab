# TASK C — TimePitch continuity differential baseline (canonical `e551ab9`)

**Instrument:** `tests/timepitch_differential_harness.cpp` (registered `timepitch_differential_harness`)
**Baseline record:** `results/vst3/task35/differential_baseline.md` (this tree, canonical remote HEAD content)
**Discipline:** diagnostic-first. The OPEN RC classes are REPORTED rows, never asserted away and
never tuned around. The only hard assertions are the frozen invariants (render determinism for
identical static drives; the static zero-miss class), which HOLD on the canonical tree
(15/15 assertions green).

## 1. What the baseline shows on the canonical tree

### 1.1 The frozen invariants hold (asserted)
- static (+7 st) zero-miss at 48k/512 for fixed / pitch_synced / pitch_formant:
  `dryFallbackFrames = deliveryUnderruns = jobStalls = preparationFailures = 0`.
- render determinism: the two identical static drives are interior-bit-identical for all three
  modes.
- static rows across the FULL format matrix (44.1/48/96 kHz × blocks 64..1024, pitch_synced):
  zero misses everywhere; wetSpan ≈ inSpan (the duration accounting closes); zero clicks.

### 1.2 RC-1 REPRODUCED in the baseline (the automation-only class)
- `psync sweep auto` (48k/512): undr=2400, fbTel(dryFallbackFrames)=2400, detector-dry
  167528 frames in 10 runs (maxRun 46264), 45 re-prepares.
- `pform sweep auto`: undr=2526, fbTel=2526, detector-dry 169646 frames.
- `fixed sweep auto`: undr=0 fbTel=0 but detector-dry 288 frames (the pitch-0 bit-identity
  class — the wet OLA identity equals the dry lane near 0 st; NOT a miss; the adapter's own
  telemetry is the authoritative fallback metric, exactly as the Task-34 probe recorded).
- RC-1 spans the whole format matrix (the FORMAT table): every fs × block sweep row misses
  (44.1k: ~1.7k underruns / 158–164k dry frames; 48k: ~1.5–2.3k / 172–178k; 96k: ~4.1–4.7k /
  343–350k) while the matching STATIC rows are all zero — the defect is schedule-dependent,
  not format-dependent.
- The snapshot-flood host model suppresses the underruns (`sweep flood`: undr=0, fbTel=0) —
  consistent with the recorded RC-1 mechanism (the flood keeps the geometry fresh; the
  automation-only cadence lets the envelope-scoped geometry go stale).

### 1.3 Observations flagged for the re-derivation tasks (recorded, not acted on)
1. **44.1 kHz click class**: the 44.1k sweep rows carry ~516–552 clicks (maxΔ ~0.6–1.0) vs
   ~12–28 at 48k/96k — an fs-dependent seam/click behaviour on the automation-only sweeps
   (relevant to TASK G / TASK D; possibly the 44.1k pMax/tracker-window quantisation).
2. **Telemetry-vs-detector divergence on the flood rows**: `psync sweep flood` reports
   fbTel=0 while the detector sees ONE 129198-frame bit-equal stretch; `pform sweep flood`
   the same shape (130734). The declared latency on those rows is 409 ms (vs 185 ms on the
   auto rows) with 47 re-prepares — the flood churn re-scopes the geometry. Either a new
   detector false-positive class (wet bit-equal to the delayed input under churn) or
   uncovered-range dry serving that dryFallbackFrames (covered-range only) does not count.
   → TASK D must resolve which; until then the flood-row detector numbers are RECORDED
   SUSPECT, the undr/fbTel numbers are authoritative.
3. The sweep-auto rows' `domHz` (Goertzel over the final 16384 frames, NOT latency-aligned)
   lands on the latency-shifted content tail — recorded as-is; a latency-aligned spectrum is a
   TASK D refinement if needed.

## 2. Differential usage (the re-derivation checkpoints)

After each implementation checkpoint (RC-1 epoch handoff — TASK D; RC-2a beta<1 — TASK E;
RC-3 seam — TASK G), re-run:

```
ctest -R timepitch_differential_harness   (or the binary directly)
git diff results/vst3/task35/differential_baseline.md
```

Expected signatures of the TASK D fix: the sweep-auto `undr`/`fbTel` columns collapse toward
zero WITHOUT any change to the static rows (the frozen invariants must remain untouched) and
without the flood-row churn signature growing. Any change in the static rows or in the
determinism assertions is a REGRESSION by definition (frozen semantics).
