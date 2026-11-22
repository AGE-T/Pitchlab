# PITCHLAB R23 — Implementation Specification (Gate 8)

**The complete, exact list of source changes shipped in R23** (everything
else in the R22 baseline tree is byte-identical; the 100/100 suite and the
12/12 module tests gate every change).

---

## 1. Engine (src/dsp.cpp, include/octavox/dsp.hpp)

1. **Non-finite input guard** (process(), per sample):
   `const float inK = std::isfinite(in[k]) ? in[k] : 0.0f;` feeding the
   detector accumulator, the wet ring and the dry ring. Fixes the
   reproduced NaN/Inf propagation defect (artifact analysis §8.3).
   Bit-exact for finite input. A recorded engine delta per the brief §27
   ("separately reproduced engine defect requires a specific source
   change").
2. **CMNDF telemetry** (`PitchDetector::lastCmndfMin()`): records the
   selected dip's CMNDF value at estimate time in all three modes
   (FullRate/Decimated/Hybrid — the hybrid coarse stage's value). Pure
   observer: zero arithmetic change to the estimate path.
3. **`Config::tracking_gate`** (default **false**): routes estimates
   through the Pitch Control Layer (Strategy A) before the engine's
   accept/history path. OFF = the R12–R22 law bit-exactly. Engine-internal
   (not a host parameter, not serialized — the f0_min precedent).
4. The control layer is configured on both prepare paths (fresh +
   in-place) from (sample_rate, detection_interval, f0_min, fmax).

## 2. New module (include/octavox/pitch_control.hpp, src/pitch_control.cpp)

`octavox::PitchControlLayer` — see TRACKING_CONTROL_SPEC.md for the full
policy contract. No engine dependency; no allocation after configure; no
locks; deterministic; ~200 lines of state-machine code.

## 3. New instruments (cli/, build commands in §5)

| Tool | Purpose | Result |
|---|---|---|
| `r23_minpitch_v2.cpp` | the §27-repaired 360-cell battery | verdicts 141/18/31/170, 0 deltas vs legacy, 58 artifacts removed |
| `r23_yin_core.h` + `r23_yin_reference.cpp` | canonical YIN reference (variants A–E) + vectors + boundary suite | 12/12 |
| `r23_detector_compare.cpp` | engine modes vs canonical/FFT on 15 signals × 4 rates | complete matrix |
| `r23_control_battery.cpp` | the control layer's acceptance battery | 290/290 |
| `r23_artifacts.cpp` | the four-symptom reproduction battery | 2 reproduced (1 fixed) |
| `r23_integration_check.cpp` | Gate-8 engine-level verification | 5/5 |

## 4. Compatibility statement

* VST3 parameters: **38 ids 0..37 unchanged**; state blob v2 layout
  unchanged (module test `state_save_restore_second_instance`: 456 bytes,
  restored replay bit-exact); `tracking_gate` and the telemetry are not
  serialized (restored sessions keep legacy behaviour).
* Default-config DSP output: **bit-exact vs R22** (100/100 suite; the
  guard is value-transparent for finite input; the telemetry is an
  observer; the gate is default-off).
* The real VST3 module (Lagrange6/Decimated variant) rebuilt and re-run
  through the SDK module loader: 12/12.

## 5. Reproduction (this sandbox)

```
cd octavox-work/r23/source
g++ -std=c++17 -O2 -Iinclude -Isrc -o build_r23/octavox_tests \
    tests/test_main.cpp tests/wav_io.cpp tests/alloc_counter.cpp \
    src/dsp.cpp src/pitch_control.cpp -DOCTAVOX_BUILD_TYPE='"Release"'
./build_r23/octavox_tests                      # 100/100
g++ -std=c++17 -O2 -Iinclude -o build_r23/octavox_r23_control_battery \
    cli/r23_control_battery.cpp src/pitch_control.cpp
./build_r23/octavox_r23_control_battery        # 290/290
# (each instrument likewise; the VST3 module build's full source list is
#  recorded in evidence/analysis_round23/logs/r23_vst3_module_integration.json's
#  companion build note -- the g++ invocation with the SDK base/hosting
#  sources, SDK v3.7.12_build_20 at octavox-work/vst3sdk)
```

## 6. Deliberately NOT changed

The detector's threshold/window/CMNDF/parabolic laws (legacy baseline per
the brief); the spacing-commit/median machinery (Strategy A gates its
input); the interpolation kernels; the VST3 wrapper and parameter tables;
the state blob. Every rejected alternative with its reason:
DECISION_RECORD.md §5.
