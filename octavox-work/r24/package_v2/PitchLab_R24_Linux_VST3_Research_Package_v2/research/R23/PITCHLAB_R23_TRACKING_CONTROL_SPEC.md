# PITCHLAB R23 — Tracking & Control Layer Specification (Gate 6)

**Component:** `octavox::PitchControlLayer` (`include/octavox/pitch_control.hpp`,
`src/pitch_control.cpp`) — the Input Pitch/Period Tracking & Spacing-Control
Gate required by R23 brief §6, corrected per §16.2 to fit the ACTUAL engine:
it gates the engine's period-history path, it does NOT emit per-voice pitch
ratios and never touches grain/delay/interpolation state.

**Independence:** pure double-in/double-out, no engine dependency; the whole
acceptance battery (`cli/r23_control_battery.cpp`,
`evidence/analysis_round23/r23_control_battery.json`) runs WITHOUT
instantiating the pitch shifter — 290/290 checks.

---

## 1. Position in the five-signal architecture (audit §6)

```
detector f0 (+cmndf_min telemetry)  ──►  PitchControlLayer  ──►  accept/reject
                                        (confidence, outliers,     │
                                         octaves, lock/hold)      ▼
                              accepted frames only ──► engine period history (8-median)
                                                              │
                                                              ▼
                                                    S_target ──► commit hysteresis ──► grain spacing S
(per-voice ratio_t: NEVER touched by this layer)
```

## 2. Policies and measured constants (defaults; battery-measured)

| Policy | Default | Measured |
|---|---|---|
| Lock acquisition | 3 consecutive consistent frames | 3 frames = 4.0 ms @48k/64 |
| Confidence gate | periodicity ≥ 0.80 (= cmndf_min ≤ 0.20, the engine detector's own threshold law) | gate test in battery case 8 |
| Outlier rejection | \|Δcents\| > 200 (non-octave) | +300c single frame rejected, tracked held |
| Octave-jump acceptance | near ±1200c ± 90c, 3 consecutive frames | accepted at frame 3; single flips stay `kOctavePending` and never poison the history |
| Slew limit | 900 cents/s | glissando 2 oct/1.5 s: max following error 47–48 c |
| Temporal smoothing | EMA τ = 30 ms (OFF by default in the engine integration; see §4) | jitter 4.29c → 0.17c (×0.04) |
| Loss-of-lock hold | 100 ms | held exactly 75 frames then RELEASED — no indefinite freeze |
| Recovery | consistency rule | re-lock 1 frame after a short dropout |
| Invalid frames | never overwrite state | NaN/Inf/0/out-of-range all rejected (battery case 8, 291 sub-checks) |

**Confidence semantics:** the decision's `confidence` is a documented
heuristic persistence score in [0,1] (1.0 acquisition, 0.9 accepted,
0.75 octave-accepted, 0.5 pending, 0.4 outlier, 0.2 hold) — explicitly NOT a
calibrated probability (brief §17.4/§23; `1 − cmndf_min` is an aperiodicity
measure, name kept as `periodicity`).

## 3. Integration Strategy A (selected; the three strategies compared)

* **A (selected — gating before the existing median):** accepted frames
  enter the R22 8-median unchanged; rejected frames are withheld. The
  median stays the ONLY smoother → **no double smoothing**, zero added
  latency (the layer adds no buffering — it is a per-cadence gate).
  Implemented in `src/dsp.cpp` behind `Config::tracking_gate` (default
  false = bit-exact legacy).
* **B (rejected for now):** replacing the median with the layer's tracker
  output — would change the engine's commit-timing law and all reference
  hashes; measured benefit over A on our corpora is within noise; deferred.
* **C (rejected for now):** lock state machine + median in series — the
  layer already contains the state machine; stacking its EMA on top of the
  median would double-smooth (brief §23's warning) unless the EMA is off
  (which is exactly Strategy A with the layer's slew engaged).

## 4. Engine integration contract (Gate 8)

* `Config::tracking_gate` (default **false**): engine-internal, not a host
  parameter, not serialized (the f0_min precedent). OFF = the R12–R22 accept
  law bit-exactly (100/100 suite green after integration).
* Telemetry: `PitchDetector::lastCmndfMin()` (observer side-channel; the
  estimate arithmetic untouched) supplies `periodicity = 1 − cmndf_min`.
* Route: `updateSpacingTarget(cd.update_history ? last_f0 : 0.0)` — a
  rejected/holding frame is indistinguishable to the engine from an invalid
  estimate (history holds), so the legacy hold semantics apply during hold;
  the layer's bounded release bounds the *telemetry* freeze; full
  engine-side history reset on release is a documented Strategy-B item.
* Integration check: `cli/r23_integration_check.cpp` (5/5) — NaN guard
  fixed-output, gating ON ≤ legacy commits on the transient corpus, both
  paths lock the steady tone's spacing, deterministic bit-identical re-run.

## 5. Linear-Hz vs cents smoothing (brief §6 comparison)

Measured in the battery (both spaces): jitter attenuation identical
(×0.04); glissando following 47.3 c (linear) vs 48.0 c (cents). The default
smoothing space is **cents (log)** — equal measured performance, and the
representation matches the musical domain (constant-cent slew = constant
musical rate). Documented, not assumed.

## 6. Test surface

* Unit/acceptance: `r23_control_battery` — 290 checks, all policies above.
* Engine-level: `r23_integration_check` (5 checks).
* Regression: the 100/100 core suite with the layer linked (default OFF).
* Future (recorded): DAW-level PDC/latency audit when a Windows host test
  is available; Strategy-B history-reset semantics.
