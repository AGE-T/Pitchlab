# RC-2b — TD-PSOLA exact-octave null: measurement record and candidate evaluation

**Task 35 continuation · diagnostic phase · NO production code changed · status after this work: TIMEPITCH NOT COMPLETE**

Baseline of this investigation: the current authoritative source = upstream `main` HEAD `254f7a9`
(the task brief names baseline `9616e43`; see the baseline note in the worklog — that commit does
not exist upstream or in this environment; `254f7a9` is the newest verifiable state and the
`native.timepitch` TD-PSOLA core under investigation is identical in its relevant parts).
Instrument: `tests/rc2b_octave_null_probe.cpp` (registered `rc2b_octave_null_probe`, CTest green),
artifacts: `rc2b_null_map.json`, `rc2b_candidates.json`, six listening WAVs (this directory).

---

## 1. The analytic model (derived from the frozen source, engine lines 1271–1404)

The frozen Pitch-Synced placement law, verbatim constants from the code:

- analysis marks: pitch-synchronous, spacing `P` (`tMark_ += max(8, round(P))`), ZC-refined;
- grain: length `gLen = round(2P)`, **periodic Hann** `w[i] = 0.5(1−cos(2πi/gLen))`, centred on the mark;
- schedule: input advance `u = P`, output advance `s = P/β` (voiced; D.4 RateFollowing);
- accumulation: **raw OLA**, per-grain scale `s/P = 1/β`, integer placement rounding (`llround(tNext_)`).

For stationary period-`P` content with pitch-synchronous marks, every grain carries the **same
content phase relative to its mark**; at output time the k-th grain's fundamental has phase offset

```
ψ_k = ω_1·(c_k − t_k) = ω_1·k·(P − P/β)  →  per-grain step  Δψ = 2π(β−1)/β  (mod 2π)
```

At `β = 2`: `Δψ = π` — consecutive grains carry **opposite sign** of the content. The output lattice
is two interleaved sub-lattices (even/odd grains), each at hop `P = gLen/2` — a **50 %-overlap
periodic-Hann lattice, exactly COLA-complete (sum = 1 each, shift-invariant)**. The two sub-lattices
carry opposite content sign, so

```
y(t) ∝ S_even(t) − S_odd(t) = 1 − 1 = 0   for ALL t (interior)  → the fundamental is EXACTLY nulled.
```

Corollaries (each verified numerically below):

1. **Every integer β ≥ 2 is an exact fundamental null** (`Δψ = 2π(β−1)/β`; the β phase classes
   `e^{−i2π(β−1)k/β}` sum to zero over the β-class COLA lattices).
2. **Harmonic material does not null**: harmonic `h` has step `h·Δψ`; at β=2 the **even** source
   harmonics are coherent (they pass) and form a complete harmonic series at `2f0` — the correct
   target pitch. Odd source harmonics are nulled (the measured "thin but pitched" character).
3. **The null is shift-invariant**: sliding the window anchor along the grain does not change either
   sub-lattice's COLA sum — family C5 provably has ZERO effect.
4. **The pitch-up IS the slip.** The output is pitched at `β·f0` only because the content is placed
   with the controlled phase slip; a grain content that is output-phase-coherent (any member of the
   "synthesis phase policy" family) makes the output `content-period × COLA-envelope` = a signal at
   the **input** pitch. Phase-coherence corrections do not "fix" the null — they cancel the pitch
   shift itself.

## 2. Instrument validation

| check | result |
|---|---|
| simulator β=1 identity (integer P) | max\|y−x\| = 1.67e-16 (exact COLA transparency) |
| simulator β=2 null (integer P = 128) | RMS = **1.09e-13** (the exact analytic zero) |
| engine β=2 sine 440 Hz | RMS = **8.98e-03** (owner reported ≈0.0086 — reproduced on the current source; the residual is tracker/ZC/rounding jitter on top of the exact zero) |
| engine vs simulator, rational landscape | 0.5→0.433/0.433, 2/3→0.372/0.371, 3/4→0.348/0.346, 4/3→0.263/0.264, 3/2→0.179/0.177 (RMS, engine/simulator — agreement to 3 decimals) |
| frozen γ=1 parity gate | pitch_formant γ=1 **bit-identical** to pitch_synced at β ∈ {2.0, 1.5, 1.99} (asserted) |
| render determinism | A/B full-render bit-identical; interior [1.0 s, 2.9 s] bit-identical across block 64/512/1024/373 (asserted) |

## 3. THE NULL MAP — single mathematical zero or wider unstable region?

**Engine, mode=pitch_synced, pure sine 440 Hz, the mandated neighbourhood** (reference: RMS 0.3532
at 0 st; full tables in the JSON):

| β | RMS | \|X\| at target | AM depth | AM rate Hz |
|---|---|---|---|---|
| 1.90 | 1.90e-02 | 1.9e+02 | 4.5 | 1.5 |
| 1.95 | 1.08e-02 | 8.2e+01 | 10.0 | 1.5 |
| 1.98 | 8.5e-03 | 3.2e+01 | 18.7 | 83.7 |
| 1.99 | 8.5e-03 | 1.7e+01 | 17.6 | 1.4 |
| 1.995 | 8.8e-03 | 9.9e+00 | 15.1 | 10.2 |
| 1.999 | 9.0e-03 | 4.9e+00 | 14.1 | 24.8 |
| **2.000** | **9.0e-03** | **3.6e+00** | **14.5** | — |
| 2.001 | 9.1e-03 | 1.9e+00 | 13.2 | 8.7 |
| 2.005 | 9.0e-03 | 3.0e+00 | 15.2 | 71.5 |
| 2.01 | 8.5e-03 | 9.2e+00 | 16.7 | 80.4 |
| 2.02 | 7.8e-03 | 2.1e+01 | 25.4 | 80.8 |
| 2.05 | 8.5e-03 | 5.1e+01 | 11.5 | 1.5 |
| 2.10 | 9.9e-03 | 8.5e+01 | 8.2 | 1.6 |

- At the exact integer the simulator shows the **mathematical zero** (1e-13; engine residual 9e-3
  from tracker/refinement/integer-placement jitter).
- **The neighbourhood is a WIDE NEAR-NULL PLATEAU, not a single zero**: over the entire
  [1.90, 2.10] span the sine output stays at 0.5–5 % of the 0-st reference with AM depth 4–25
  (perceptually: fading/garbling, the "effect comes and goes" experience) and the spectral peak
  wanders in the sidebands (f0peak 455–923 Hz instead of the target).
- The **rational map** (validated against the engine to 3 decimals) widens the class further:
  **every pitch-up integer ≥ 2 is a null** (2: 1.1e-13; 5/2: 2.4e-03; 3: 3.3e-03; 7/2: 2.5e-03;
  4: 2.2e-14; 6: 5.1e-14) and the pitch-up landscape descends toward them
  (6/5: 0.32 → 5/4: 0.30 → 4/3: 0.26 → 3/2: 0.18 → 5/3: 0.09 → 2: 0).
  Pitch-DOWN rates are loud everywhere (0.34–0.43) — the null class is **pitch-up-integer-centred**.
- **Harmonically structured material is immune at β=2**: stack_220 RMS 0.078 / saw_220 0.084 /
  voice_120 0.064, measured pitch 438–440 Hz (correct octave) — the even source harmonics carry the
  octave-up series exactly as the model predicts.

**Classification: MUSically relevant FAILURE.** The exact zero sits inside a wide (≥ ±100 cents,
and structurally every pitch-up integer) near-null region on fundamental-dominated material; real
melodies/vibrato/slider drags sweep through it. This is not an isolated mathematical curiosity.

## 4. Candidate correction families — measured verdicts

Simulator = the frozen placement law (validated above); candidates differ ONLY in their hook.
Full matrix: `rc2b_candidates.json` (212 records; materials sine-integer-P / sine-440 / stack / saw
× β ∈ {1.9, 1.99, 1.999, 2.0, 2.001, 2.01, 2.1, 1.5}).

| family | hook | measured effect at β=2 | verdict |
|---|---|---|---|
| **C2 — synthesis phase policy** (π-rotation of odd grains) | `signFlip` | sine: RMS 0.354 but spectral peak = **375.00 = the INPUT f0** (target 750) — pitch collapse; stack: peak 219.94 (target 440) — collapse; saw: peak 219.94 — collapse. At β=1.5: peak 281/165 (target 562/330) — collapse everywhere. | **REJECT — destroys the mode's pitch semantics.** The pitch-up IS the content-lattice slip; forcing coherence outputs the input pitch. Also breaks the frozen γ=1 TD≡FD landscape (FD inherits the placement). |
| **C2' — phase policy via content** (odd grains read the +P/2 input position) | `halfShift` | numerically identical to C2-sign (375.00 / 219.94 / 219.94) — for period-locked content the half-period shift IS the π rotation; for general material it additionally corrupts the waveform (mid-period segments, non-pitch-synchronous). | **REJECT — same proof, plus waveform corruption.** |
| **C1 — mark placement adjustment** (alternating output spacing, mean P/β preserved) | `altEta = P/8` | sine: peak collapses to the input f0 (375.00 / 439.93); AM depth worsens at other rates (0.234 vs 0.180 at 3/2). Any pitch-synchronous lattice at rate βf0 on f0-periodic content carries the same slip topology; the escape schedules change pitch or duration. | **REJECT — pitch-destructive exactly where it was meant to help.** |
| **C3 — window/lattice relationship** (grain length detuned 5 %) | `gLenScale=1.05` | sine: RMS 5.4e-03 (integer-P) / 8.2e-03 (440) — **the null persists** (the class sums become rippled; the output ∝ ripple-difference ≈ 0 for small detune); the whole mode's timbre/AM character degrades at every rate. | **REJECT — quantitatively ineffective and contract-damaging** (gLen = round(2P) is frozen §6.6.1 item 6 semantics). |
| **C5 — alternate valid window alignment** (anchor slid +P/4) | `anchorShift=P/4` | integer-P sine at β=2: RMS **1.089e-13 vs baseline 1.094e-13** — bit-level identical; 440 Hz: 6.216e-03 vs 6.206e-03. Zero effect, as the shift-invariance theorem requires. | **REJECT — provably a no-op** (both sub-lattice sums are anchor-independent). |
| **C4 — exact-octave special-case scheduling** | any schedule preserving u=P, s=P/2, gLen=2P | subsumed by C1/C2/C5: the slip is fully determined by (u, s, gLen, content periodicity); every escape route measured above either collapses the pitch, leaves the null, or damages the mode globally. | **REJECT — no schedule freedom exists inside the frozen semantics.** |
| **C6 — non-destructive gain / normalization** | — | the null is a zero of the accumulation itself (the two COLA sub-lattices cancel); no output-domain gain can restore a structurally absent signal. Explicitly forbidden by the brief anyway. | **REJECT — structurally inapplicable.** |
| γ≠1 (the known mitigation, for the record — NOT a candidate per the brief) | engine pitch_formant γ=2, β=2 | sine440: RMS **0.289**, peak **879.19 Hz** — loud and correctly pitched (the formant shift moves the content into the coherent classes). Re-timbres ALL material; a mitigation only, as the brief itself states. | **Documented as mitigation only.** |

**Conclusion: no candidate correction preserves the frozen Pitch-Synced contract.** The null is not
a defect *of* the mechanism that could be patched — it is a degenerate state **of** the mechanism:
the pitch-up in TD-PSOLA is realized precisely by the content-lattice phase slip, and at pitch-up
integer ratios the slip aligns opposite-phase grains onto COLA-complete periodic-Hann sub-lattices,
exactly cancelling the fundamental. Removing the cancellation removes the pitch shift (measured:
every phase/placement candidate outputs the input pitch); keeping the slip keeps the null.

## 5. What WOULD change in the contract (for the owner decision — NOT implemented)

Any effective correction is a **spec-level amendment** of the Pitch-Synced modes. The honest options:

1. **Accept as documented intrinsic character** (recommended): record the null map (this document)
   in the SoT (`docs/vst3-product.md` mode-scoped character table + the pitch-range honesty
   section): "Pitch Synced / Pitch + Formant: fundamental-dominated material at pitch-up integer
   ratios (and a ≥ ±100-cent neighbourhood) produces a structurally nulled wet; harmonically rich
   material is unaffected (even-harmonic series carries the target pitch)." Product guidance:
   fundamental-dominated material + exact pitch-up intervals → Fixed/Adaptive; γ≠1 documented as
   the mitigation knob. No contract change; the frozen semantics stand with their measured
   boundary spelled out.
2. **A hybrid TD+FD rescue path** (e.g., per-grain fundamental-continuation using the FD plan
   ladder inside the Pitch-Synced modes at/near pitch-up integers): changes the frozen
   "TD = time-domain-only, the two accumulation policies NEVER merged" semantics and the γ=1
   parity landscape; a new mode semantic requiring its own spec section, parity gates, and
   checkpoint series.
3. Keep open / alternative owner proposal.

Per the brief ("If no valid correction exists, STOP and report"): implementation has NOT begun and
the specification is NOT amended; the decision is the owner's.

## 6. Recorded observations discovered en route (no scope creep — documented only)

1. **Offline feed-schedule output capacity**: the direct-render harness must offer ≥ ~(1/β)·block
   output per call for β<1; capacity-limited emission stalls the placement schedule and overflows
   the engine's input window (the continuity probe's recorded "input window overflow" sensitivity,
   reproduced and explained — harness-side, not an engine defect).
2. **Streaming-tail handoff cadence sensitivity**: in the offline compacted render the full-render
   output LENGTH differs with the input block cadence (block 1024: +1029 frames ≈ 21 ms at the
   drain tail; block 64/373/512 bit-identical). The analysis interior is bit-identical across all
   schedules (asserted). Root-cause hypothesis: the tail marks' voicing/decode release at the
   streaming→finish boundary. Narrowly scoped, separate from RC-2b; a candidate next-iteration item.
3. The γ=1 parity gate and the render-determinism contract hold EXACTLY on the current source at
   the probed settings (asserted in the probe).

*Probe: `tests/rc2b_octave_null_probe.cpp` · machine records: `rc2b_null_map.json`,
`rc2b_candidates.json` · listening evidence: the six WAVs in this directory.*
