# OCTAVOX R31 — TELEMETRY & LATENCY-TRUTH DEMO VST3 (built from the audit findings)
**Eventide Octavox VST3 — the R26-R30 static-audit findings APPLIED: a new
PitchLab Harmonizer demo build (PitchLab_Lagrange6_Balanced.vst3, Linux
x86-64) carrying the m02-style pitch-telemetry surface, the R30 poller/
dirty-bit fanout, the R29 vendor-interface pattern and the U-2 latency-truth
notification. Clean-room: our own code; the original binary was never
executed.**
Round: R31. Date: 2026-11-27. Builder: PitchLab RE agent (R31 session).

## 0. Mandate, target, method, rules honoured
- Mandate (user): "Tudnál buildeltetni egy új demó vst-t az itt feltárt
  infók használatával?" — build a NEW demo VST3 that USES the findings of
  the r26-28-static-audit line (R26-R30).
- Clean-room status: UNCHANGED. The new code is our own design; it consumes
  the PUBLISHED BEHAVIOURAL SPECIFICATION our static audits established
  (formulas + architecture), not target code. The original binary
  (`upload/Ox.vst3`, SHA-256 7bb260f6…) was not executed at any point and
  was not re-analysed this round (no new RE claims — this round APPLIES
  the existing record).
- Claim grammar: the R26-R30 documents stay byte-intact; every applied
  finding is cited to its round/finding id below.

## 1. What the audit contributed (finding -> shipped feature)
| audit finding | shipped in the R31 build |
|---|---|
| **R28 F1.6** — m02 fully decoded: ids 7..13 = cents-from-C4 (1200/ln2, 1/261.6256, −65535 sentinel), frequency ratio, Q23-quantised ratio n/(n+8), sqrt(frac·(1−2⁻²³)), confidence combination, gated ∓cents correction twins | `include/octavox/telemetry.hpp` + `src/telemetry.cpp`: the same id space 7..13 served from PitchLab's own engine state (tracked/detector pitch, per-voice ratio, grain phase, CMNDF periodicity, control state) |
| **R30 F2.3-F2.6** — the delivery architecture: a per-voice BLOCK-RATE poller reads the engines once per block, change-detects every result and fans changes out as ATOMIC DIRTY BITS (`lock or [bits], 1<<shift`) | `TelemetryPoller::poll()` called once per process() block; exact change detection; `fetch_or` dirty mask (bit = voice*8 + (id−7); 56 live bits); `consumeDirtyBits()` = the UI-side read-and-clear |
| **R29 F1.x** — the original serves custom functionality through a VENDOR interface answered by queryInterface (the "TideOctW…" IID at [this+0x48]), not through extra parameters | `IPitchTelemetry` (custom IID D1A60F31-8C4B-4E39-9A67-31D1-8E4F5A21) answered FIRST by the processor's queryInterface; the 38-parameter automation surface and the state blob v2 stay byte-frozen (21/21 wrapper tests unchanged) |
| **R30 F1.4-F1.8 (U-2)** — the host latency is a LIVE value: set by the mode-aware prepare (`flag ? 8 : round(0.04·sr)+8` in the original) and must reach the host whenever it changes | `notifyLatencyIfChanged()` — on setupProcessing/setActive/setState the wrapper re-reports via `IComponentHandler::restartComponent(kLatencyChanged)` exactly when the value changed (first activation, and an active state restore that changes the sample-domain config); unchanged latency never re-notifies |
| **R30 F2.1** — the engines are PER-VOICE (voice+0x430) | acknowledged in the telemetry design: per-voice frames polled from the per-voice engine state (`voiceStats(0, v)`), channel 0 = the reference engine (documented) |

Deliberately NOT applied (documented decisions): the R27 pan law and tanh
softclip (PitchLab has no pan parameter and does not clip — float
headroom; adding them would change the frozen surface and the reference
hashes), the R28 Q23 deglitch (PitchLab's R19 crossfaded base-delay changes
already cover the click-free requirement with a tested mechanism).

## 2. Engine additions (pure observers — bit-neutrality gated)
- `dsp.hpp` R31 block + `dsp.cpp`: `lastEstimateCmndfMin()` (the detector's
  CMNDF minimum, the periodicity ingredient), `lastControlState()` (the R23
  control layer's state), `trackedHz()` (the post-gate tracked value) — on
  `AlignedDelayLine` and delegated to channel 0 on `MultichannelShifter`
  (+ `lastEstimateHz()`/`lastEstimateSpacingAccepted()` shifter delegates).
- **Output neutrality PROVEN**: the 115-case suite passes 115/115 with all
  four default-neutrality reference hashes bit-exact (the accessors read
  state the engine already maintains; no arithmetic touched).

## 3. Verification (all gates green)
| gate | result |
|---|---|
| core suite (Release) | **115/115**, 4/4 reference hashes bit-exact |
| core suite (ASan/UBSan) | **115/115, zero sanitizer reports**, 4/4 hashes bit-exact |
| VST3 wrapper tests | **21/21** (unchanged — the frozen surface untouched) |
| module integration (the compiled bundle through the SDK loader) | **16/16** = the previous 12 + 4 new R31 cases |
| Steinberg VST3 validator | **47/47** on the fresh bundle |
| new: telemetry_vendor_interface | queryInterface serves IPitchTelemetry; id mapping 7..13; bounds rejects |
| new: telemetry_values_sentinel | silence → id 7 = −65535; 220 Hz sine → id 7 = **−300.009 cents** (0.009 cent tracker precision); Q23 identity exact; ids 10/11 in range; unity → twins 0 |
| new: telemetry_dirty_bits | id-7 bit quiet during steady silence; fires on the tone; consume clears |
| new: latency_notification | 1 restart on first activation; unchanged → no re-notify; grain-2048 state restore → re-notify + latency 2304→1280 |

## 4. The demo package
`public/PitchLab_R31_Telemetry_Demo_VST3.zip` — the built
`PitchLab_Lagrange6_Balanced.vst3` (Linux x86-64) + README (install +
the IPitchTelemetry spec for host/tool authors) + the verification logs +
this report. SHA-256 alongside (`.sha256`). A Windows cross-build was NOT
produced this round (the llvm-mingw toolchain was lost with the sandbox
resets and was not re-acquired — the MinGW CMake paths remain authored;
same status as R24-W).

## 5. Honest limits
- The demo is a Linux x86-64 VST3. Windows remains source-compatible (the
  wrapper's MSVC/MinGW paths are authored and were green at R24-W) but is
  not binary-delivered this round.
- The telemetry is host-/tool-visible (a vendor interface), not
  pixel-visible: the wrapper deliberately ships without a GUI; a DAW-side
  or standalone tool queries `IPitchTelemetry` via queryInterface.
- id 11 (confidence) is a LABELLED PitchLab mapping (periodicity × state
  weight); the original's exact combination is engine-internal. With the
  default configuration (tracking gate off) it honestly reads 0 — the
  documented no-validated-lock behaviour.
- No DAW was involved; the validator + the SDK hosting loader are the
  compatibility evidence (the project's standing scope note).
