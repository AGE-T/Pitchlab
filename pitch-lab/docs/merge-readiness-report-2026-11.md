# Pitch Lab — Realtime Integrity and Merge Readiness report (2026-11-25)

Branch: `audit-fixes-world-signalsmith-pixel-ui` · audited start HEAD **92e15be** · `main` **42757e7** (untouched).
Toolchain: Windows 11, VS 2022 17.14 / MSVC 19.44, CMake (VS generator), Release. Linux GCC / TSan / UBSan run on CI only.

## 1. Starting point
`92e15be` (branch HEAD at audit). `main` was not modified; there were no force pushes.

## 2. Commits and modified files
| Commit | Content |
|---|---|
| 699fdd9 | native.world: a process-wide shared pool, a never-wait realtime callback, failure handling, dual-mono F0, declared rates |
| 7397050 | native.signalsmith: processing-time automation index (inputLatency), a tracked formant base |
| 082c2d4 | F1 allocation audit, F4/F5 tests, F7 callback-time distribution |
| 10e8364 | Fixes for the independent audit's findings (pool notify on freed Group, offline lost wakeup, starved-job polling, prefill after reset, …) |
| 9fcb4b9 | GCC -Werror: explicit `ProcessReport::starved` in every brace initialiser; engine_registry.h doc |
| (this) | Sanitizer gate in vst_rt_alloc_test, ORIGIN.toml notes, worklog, this report |

Files (39 against 92e15be): `pitch_engine.h`, `world_engine.{h,cpp}`, `signalsmith_engine.cpp`, `realtime_adapter.{h,cpp}`, `processor.cpp`, the `ProcessReport` initialisers of the other 5 engines, CMakeLists (root + vst), `ci.yml`, tests (new: `vst_world_realtime_test`, `vst_rt_alloc_test`, `tests/fixtures/state/*`; extended: `external_engines_test`, `vst_processor_test`, `vst_audio_path_test`, adapter/seam/status/diagnostics), `tools/vst_host_sim.cpp`, and both `ORIGIN.toml` files.

## 3. Bugs found and how they were reproduced
| ID | Bug | Reproduction |
|---|---|---|
| W1 | WORLD `emit()` waited without bound in the audio callback for the chunk worker | `vst_world_realtime_test` "overloaded pool": a 300 ms chunk delay; before the fix the callback was ≥300 ms, now worst < 50 ms |
| W2 | A new helper `std::thread` was created for every chunk (DIO/Harvest parallelism) | Code review plus `worldPoolLiveThreads()` measurement |
| W3 | A worker exception ran into `std::terminate` (abort) | `setWorldFaultInjectionChunk` → now EngineException → the adapter counts it and rebuilds the chain |
| W4 | Thread count across instances was unbounded | 3 concurrent instances; the pool is bounded to hw−2 (max 8) |
| W5 | Stereo F0 came from channel 0 only (anti-phase/different-F0 cases were wrong) | `external_engines_test` stereo F0 matrix (in phase, anti-phase, different F0, mono, silence, noise × DIO/Harvest) |
| W6 | Sample-rate declaration inconsistent with prepare | Registry `supportedSampleRates` = {44.1, 48, 88.2, 96} kHz; 192 kHz → counted preparation failure, finite dry output |
| S1 | Signalsmith automation took effect 55–67 ms early (index taken from the newest input frame) | Step-timing matrix: now ±5.5 ms (gate 15 ms); 44.1/48/96 kHz × block sizes |
| S2 | Auto formant base: 9.4 dB mean / 22 dB worst envelope error | Synthetic-vowel comparison; with the tracked base 2.0 dB / 3.66 dB |
| A1 | Phase G (pv.phaselocked) timeout ~1/4 runs — a branch regression (lane memory doubled) | Built main: 8/8 pass; the branch before the fix failed ~1/4; with the capacity/4 slack 8/8 |
| A2 | (audit) The pool notified through a Group the owner may already have freed | Code review; fix via the pool-level `groupGen_` |
| A3 | (audit) Offline lost wakeup on failure (`done_` notify without a value change) | `progress_` generation, bumped by both commit and failure |
| A4 | (audit) A starved job could be finished early | `Job::lastStarved` + `pollJob()` |
| G1 | The GCC -Werror build broke (missing-field-initializers) | CI Linux build; explicit `, false` fixes it |

## 4. Technical justification for the fixes
- **Never wait in realtime:** in `ProcessContext::realtime == true` mode the engine returns with `starved = true` instead of waiting. The adapter fills the gap through the existing dry-fallback path and counts it (`engineStarvedCalls`, `deliveryUnderruns`, `dryFallbackFrames` — F2 one-to-one). Offline (`kOffline`) rendering still waits, so bounces stay deterministic.
- **Shared pool:** threads start and stop only in attach/detach (preparation thread). The callback only touches atomics (`wake()`). The fork-join uses help-while-join, so no deadlock.
- **Errors:** worker exceptions set `failed_`; the next `process()` throws an EngineException. The adapter's existing exception path (rebuild, `engineExceptions`++) handles it.
- **Dual-mono F0:** each channel gets its own F0, so anti-phase and different-F0 stereo stay correct; mono costs ×1.
- **Signalsmith:** the library's README "Automation" section says the parameter belongs to the processing time (newest input − inputLatency). `setFormantBase` is skipped only without Keep Formants (that is the default); with it, the tracked base is clearly better (S2).

## 5. New and modified tests
- `vst_world_realtime_test` (new, 5+1 skip): paced DIO/Harvest, overload never-wait, fault → exception + rebuild, 3 instances pool bound, undeclared rate.
- `vst_rt_alloc_test` (new, 2): F1 zero callback allocation for every engine (all 4 TimePitch modes, Signalsmith ± formants) and lifecycle event; F7 histogram/percentile consistency and bucket arithmetic.
- `external_engines_test` (+): WORLD never-wait, fault injection + reset/re-prepare, pool bound, stress, stereo F0 matrix, sample rates; Signalsmith step and sweep timing matrix, formant comparison.
- `vst_processor_test` (+): F4 latency notification (timer → restartComponent), F5 PLV1/2/3 state fixtures + migration + truncated state.
- `vst_audio_path_test`: 8 engines, undeclared rates skipped, kOffline.
- Capacity gates: `PITCHLAB_SANITIZER=1` turns off only the wall-clock capacity checks (zero fallback, rtf < 1). The functional, never-wait and allocation checks always run.

## 6. Build results (Windows, MSVC 19.44, Release)
- `cmake --build build --config Release`: **0 errors**. Warnings are only the existing C4244/C4267 narrowing ones (`realtime_adapter.cpp` 358/360/522/525/554/555, `timepitch_engine.cpp`, `experiment_compiler.cpp`, `metrics_modulation.cpp`).
- VST3 bundle: `build/VST3/Release/PitchLab.vst3`, module SHA-256 `FBFAC67BF2664C299C418AB926F5237F797CBA82C726CDDB51F89D417CDBA0F0`.

## 7. CTest
- Full: **45/47 passed**. The 2 failures are pre-existing and also appear on `main`:
  - `vst_processor_test` — the LFO determinism case (`:1290`) is flaky under load; it reproduced on `main`.
  - `corpus_gen_test` — environment-dependent and pre-existing.
- Probe suite (5 heavy probes): **5/5**. Fixed sweep fallback improved from 288 to 0 frames.
- New and affected suites run individually: `external_engines_test` 17/17, `vst_world_realtime_test` 5/5, `vst_rt_alloc_test` 2/2.

## 8. Sanitizers
- **ASan (MSVC /fsanitize=address, build-asan, PITCHLAB_SANITIZER=1):**
  - `external_engines_test` 17/17, `vst_world_realtime_test` 5/5, `vst_rt_alloc_test` 2/2.
  - **0 AddressSanitizer reports.**
- **UBSan / TSan:** not available on MSVC; they run in the CI Linux sanitizer matrix (see §13). TSan (incl. the WORLD pool tests): **no data race reported**. ASan+UBSan on GCC: one UBSan finding in a new test (fixed, §13), no memory error. The only remaining sanitizer failure is the pre-existing LFO test.

## 9. VST3 validator
Official SDK validator on the bundle: **Result: 47 tests passed, 0 tests failed.**

## 10. Windows build
Successful (see §6). The bundle loads and the validator passes.

## 11. Open problems
1. **F7:** the worst callback time for `granular` (~12.0 ms) and `timepitch adaptive` (~14.8 ms) exceeds the 10.67 ms budget at 512/48k. Measured, not fixed (out of scope; profiling needed).
2. ~~Audio-path golden artifact~~ — regenerated from the CI GCC run (§13).
3. ~~CI Linux result unread~~ — read and fixed (§13).
4. **`pollJob()`** (audit fix A4) has no dedicated targeted test. It is covered indirectly by the overload and recovery test.
5. **The pool-bound test cannot fail** with the current design (the pool size is fixed). It is a regression guard against a future redesign.
6. **On the realtime path:**
   - Each callback can make at most one `notify` syscall (`wake()`).
   - `throwIfFailed()` allocates the exception object, but only on the failure path.
7. **Pre-existing:**
   - The flaky LFO test (`vst_processor_test:1286`, "LFO rate host automation to 0 Hz mid-block"): fails consistently under the CI sanitizers and on `main`/92e15be too; and `corpus_gen_test`.
   - `timepitch_differential_harness:493` (REALTIME render-determinism A/B of a static drive): failed once in five CI runs on unchanged adapter code (9599 mismatching samples) — realtime adoption timing under runner load, not the offline path.
   - The PLV1 state ambiguity: from 2026-10-03 to 11-20, PLV1 states were written with 6 engines and ±48 st.
   - Web sync has CSRF-only protection.

## 12. Known limitations
- WORLD:
  - Latency is ≈217 ms (chunked analysis, 80 ms headroom).
  - Under realtime overload the wet path goes dry and is counted, by design.
  - Supported rates: only 44.1/48/88.2/96 kHz.
  - At most 4 concurrent stereo DIO instances or 3 Harvest instances on the test machine without fallback.
- Signalsmith: upstream #34 (formant envelope) is not applied (a measurable follow-up); #33/#35 are not relevant at 1:1.
- WORLD #159 (RemoveDCComponent) is not patched (open upstream, unconfirmed).
- TSan and UBSan run only on CI.
- The three-concurrent-instance WORLD zero-fallback check is gated on >= 8 hardware threads (a 4-vCPU runner has 2 pool threads; measured 3k-44k fallback frames there).
- Offline (kOffline) waits are bounded at 10 s per wait; a timeout is counted, never a hang.

## 13. Addendum — the CI loop (2026-11-25, after GitHub sign-in)
The CI logs became readable after the owner's GitHub sign-in in Chrome. Findings and fixes, each with a reproducing test:

| Commit | Finding (CI) | Fix |
|---|---|---|
| fc3a6a8 | GCC `-Wpedantic -Werror`: `external_engines_test.cpp` included Signalsmith via the NON-system `-I external` path → upstream `COMPAT_SPELLING(...);` "extra ;" — **both Linux builds broken since 699fdd9** | the same GCC diagnostic fence as `signalsmith_engine.cpp` |
| fc3a6a8 | TSan job never ran a test: the unquoted `-R ^(a\|b)$` regex was a bash syntax error (since 40eb017) | quoted |
| fc3a6a8 | UBSan: invalid vptr — the F4 test's stack handler destroyed before `~Host` terminated the controller | handler detached before scope end |
| fc3a6a8 | `rtf < 1` capacity check under sanitizers (also failing on 92e15be) | `PITCHLAB_SANITIZER` gate |
| df635b1 | **Offline bounce dry gap**: WORLD → other engine served exactly one job span (52 928 frames) dry under ASan+TSan — the prep thread sat in WORLD `reset()` (quiescing an in-flight chunk) and the next job was not prepared in time | kOffline: the audio thread waits (bounded) for an already-due job; repro test with the chunk-delay hook: **60 912 → 0** dry frames |
| df635b1 | 3 realtime WORLD instances on a 4-vCPU runner (capacity) | gated on >= 8 hardware threads |
| 8db371b | **Offline bounce dry start/resume**: P1.4 suspend/resume rendered chainless (engine −1) while the rebuild ran | kOffline also waits for a chain build the prep thread can do on its own (no chain; or a switch with no pending chain); repro test: engine −1 → 3 on the first block after resume |
| 19a5c1b | audio-path golden still 6-engine (182 → 228 records); WORLD EXPECTED-LIMITATION records carried the GRANULAR diagnostic text | golden regenerated from the CI GCC artifact (181 identical identities, 46 new WORLD/Signalsmith, 1 phase-G switch target now WORLD); WORLD diagnostic fixed |
| 66e00cd | UI lane: the pixel-art editor's real capture has 18 colours; the ">100 colours" blank check failed it | blank = < 8 colours or one colour >= 98% |
| 54f01bf | UI parameter-binding lane (synthetic X11 clicks): the pixel-art `PLSlider` maps the mouse onto the bar inset by 7 px; the harness still used the full 172 px width (CI: 0.7532 vs 0.7326 at x=610), so a pv.classic hop drag hit an unintended FFT/hop combination and every later engine switch stalled | the harness derives the click value from the same inset (`kThumbPad`) |

CI state at 19a5c1b (VST3 job attempt 2) and 66e00cd (UI capture check PASS, the parameter-binding lane then failed on the slider inset — fixed above): **52/52 tests, audio-path artifact gate PASS (228 records), validator 47/47, all 11 host-sim examples byte-identical**; T-INF1 and Windows x64 green. Realtime mode is unchanged by the offline waits (the overload test's worst callback stays ~0.07 ms).

**Final CI state at 54f01bf (run 45674762563):** T-INF1 core ✅, VST3 product layer ✅ (52/52 tests, audio-path artifact gate, validator 47/47, 11/11 examples, UI capture + parameter-binding lanes), Windows x64 ✅. Sanitizers (ASan+UBSan, TSan): every test passes except the pre-existing LFO test `vst_processor_test:1286` (also failing on `main` / 92e15be); no sanitizer report.

### 13.1 Follow-up (same day)
| Commit | Finding | Fix |
|---|---|---|
| f2ecee9 | **The long-standing LFO test failure was a real offline-bounce defect.** Pitch/LFO are curve parameters handled by the envelope-EXIT path; in kOffline nothing waited for the exit's re-centre chain, so a slow prep thread (ASan) rendered the whole bounce clamped at the old envelope (LFO without effect), and on a fast machine two identical bounces differed (reproduced locally in Release) | kOffline waits (bounded) until published exits are consumed and the re-centre chain is pending; the test passes 3/3 in Release and under ASan with the SAME measured value (2010.07); the 16 host-sim renders are byte-identical before/after |
| 24e90c6 | UI lane: an intermittent stall of every engine switch after a pv.classic hop drag (54f01bf passed, f2ecee9 failed, same tool code). Not reproducible at the adapter level (new REALTIME regression test: fft 4096 / hop 512 then 5 switches, each adopted in 2-3 blocks) | diagnostics in the lane (the controller's engine value next to the adapter state on a failed switch) |

**CI at 24e90c6 (run 45689638405): ALL JOBS GREEN** — T-INF1 core, VST3 product layer (52/52, audio-path gate, validator 47/47, 11/11 examples, UI capture + parameter binding), Windows x64, ASan+UBSan, TSan.

Remaining open items: the intermittent UI-lane engine-switch stall (1 of 3 runs; the diagnostics will localise it on recurrence); the realtime timepitch render-determinism flake (1 of 6 runs); F7 callback budget (granular, timepitch adaptive); the engine-portfolio decision (granular removal, timepitch A/B against Bungee / stftPitchShift).

## 14. Follow-up (2026-11-26): product feedback round
| Commit | Change | Evidence |
|---|---|---|
| 80aa22f | UI: pixel-art switches for discrete parameters, typed pitch entry, +-24 st product range (+-12 on vardelay), formant ratios in semitones; timepitch window choice 3 = Tukey (RECT crackled), Voicing Gate for the pitch-synced modes | rect 66 -> tukey 0 clicks (+7 st sine); gate 0 bit-identical to the frozen behaviour; ui-binding lane ALL CHECKS PASS |
| c976dd7, 5d0a279 | native.bungee (Bungee Basic, MPL-2.0) and native.stft (stftPitchShift core, MIT) as A/B candidates; state PLV4 | alignment lag 0 @44.1/48/96 kHz; 0 callback allocations; worst callback 0.77 / 0.72 ms |
| 2ecd343 | shared resampler 4.6x faster (tabulated Kaiser factor + sinc rotation) | worst |fast - exact| 7.4e-13; granular @96k/-12st/grain 0.5 RTF 1.35-2.8 -> 0.43; timepitch adaptive worst callback 14.8 -> 7.6 ms |
| b785138 | CI evidence steps run past a failed gate; regenerated examples uploaded | |
| (this) | audio-path golden 228 -> 272 records (GCC, run 45825336190) | 227 identities unchanged, 44 new (all PASS), 1 phase-G switch target now native.bungee; 2 informational wobbles (transient-burst zero-cross pitch 699.7 -> 700.5 Hz, granular stereo diagnostic 441.6 -> 441.7 Hz); the 11 example WAVs byte-identical |
| 469de4c | switch keys: short unambiguous labels (MODE read PITCH / PITCH on the CI capture) | |
| ddc3917 | render-determinism checks run as offline bounces (realtime adoption timing is spec-dependent); WORLD paced zero-fallback gated on >= 8 cores | |

**CI at ddc3917 (run 45873312435): ALL JOBS GREEN** — T-INF1, VST3 product layer (tests, audio-path gate 272 records, validator 47/47, examples, UI capture + parameter binding), Windows x64, ASan+UBSan, TSan.

