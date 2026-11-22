# PITCHLAB R24-v2 — SIMD PRODUCTION DEFAULT DECISION RECORD (Correction D)

**Round:** 24-v2 (targeted delivery corrections) · **Date:** 2026-11-08
**Subject:** the production value of `Config::simd` for newly instantiated
plugins (engine-internal; never a host parameter, never serialized in the
VST3 state blob).

---

## 1. The decision

**The production default changes `Scalar` → `Auto`.**

`Auto` resolves to the **bit-exact AVX2 level** when the processor (and
build) supports it, and to `Scalar` otherwise. FMA remains an explicit
opt-in (`Config::simd = AVX2FMA`); Auto never selects it — unchanged from
the measured R24 decision.

This supersedes Decision 3 of the R24 measured decision record
(`PITCHLAB_R24_SIMD_BENCHMARK_RESULTS.md` §4), which shipped Scalar as the
default with the AVX2 levels opt-in. That decision was conservative and
correct *for its round*: the acceleration had just landed and the default
neutrality was gated by construction + the battery, but the round had not
yet demonstrated the accelerated default against the **full historical hash
corpus**. The R24-v2 correction completes exactly that demonstration.

## 2. Why the change is now justified (the evidence chain)

1. **The AVX2 level is bit-exact by construction and by gate.** The
   production lag-axis kernel executes the reference op sequence per lane
   (sub → mul → sequential ascending-j add); hard exact-equality gates run
   over a randomized 21-shape corpus (`simd_kernel_avx2_lag_bitexact`,
   `simd_kernel_peak_bitexact`) — passing again in this round's runs.
2. **Engine bit-identity at the AVX2 level** is gated across 10 engine
   configurations (3 detector modes × rates × interpolations, including
   tracking-gate paths) by `simd_engine_avx2_bitidentity` — passing.
3. **The full historical hash record was re-run under the new default in
   this correction round — all eight pins bit-exact on the AVX2 machine:**
   * the four **legacy 15.0.0** pins (`octavox_r15refcap --legacy`):
     `0x74318b06c182aacb`, `0x46a091e98f2a413a`, `0x9502cf8827e8782c`,
     `0x6f7a2ce3ca310c25` — 4/4 exact;
   * the four **16.0.0-default record** pins (suite `default_neutrality` /
     `default_neutrality_r19`, re-run at the new default):
     `0x74318b06c182aacb`, `0x9b7217813e519209`, `0xb66549c1dca9d6ae`,
     `0x38f625fec56dba45` — 4/4 exact.
   These gates executed **through the Auto→AVX2 path** on this machine
   (resolved level recorded = AVX2 in the run JSONs), so the accelerated
   default is proven bit-neutral against the entire recorded corpus, not
   merely against the battery's own matrix.
4. **The runtime fallback is proven.** `resolve(Auto)` returns Scalar when
   `isaUnitsCompiled()` is false or CPUID reports no AVX2 (rule re-gated by
   `simd_detect_resolve_rules`); a forced AVX2/AVX2FMA on incapable
   hardware is loudly rejected at `prepare()` (`simd_prepare_mode_validation`).
   Non-AVX2 machines therefore keep executing the historical Scalar path —
   the same binaries are safe everywhere the module loads.
5. **Saved-state compatibility is structurally preserved.** `Config::simd`
   is engine-internal and NOT serialized (the 'OVR1' v2 blob layout is
   frozen); a restored session adopts the *current* default, which renders
   bit-identically through the accelerated path (point 3). No host-facing
   parameter was added (the 38-id surface is byte-identical).
6. **Allocation-free accelerated processing** re-verified
   (`simd_rt_allocfree_accelerated`: 0 allocations).
7. **Tests updated with the decision, not silently re-baselined:** the
   asserting cases were renamed and re-scoped (`simd_default_auto_neutrality`
   now gates: default IS Auto; default-prepared engine resolves the Auto
   level; **default (Auto→AVX2) output is bit-identical to explicit-Scalar**
   — the neutrality that matters; default == explicit-Auto bit-identical),
   and `simd_reprepare_level_switch` gates the initial Auto-resolved level.
8. **The VST3 chain re-validated under the new default on the built
   artifacts:** wrapper 22/22, module integration 12/12, SDK validator 47/47
   — on all nine variant builds (the wrapper/integration references use the
   same default-config path as production).
9. **Performance at the new default (13-case bench, `--simd auto`, this
   machine):** mono_1v_b128 ≈ 145.7 ns/frame and stereo_1v ≈ 269 ns/frame
   vs ≈ 371/743 ns/frame Scalar (≈ 2.8x on the detector-dominated 1-voice
   shapes, ≈ 1.7x at 8 voices) — consistent with the recorded R24 AVX2
   column. FullRate 96 kHz 8-voice stereo drops 1144 → ~368 ms per audio
   second (the R19 headline mitigation).

## 3. What is explicitly NOT decided here

* **FMA stays opt-in.** The R24 measured record stands: the FMA lag kernel
  measured ≥ the AVX2 one nowhere on this machine and carries the fused
  per-term rounding deviation. No new measurement justifies changing it, and
  none was taken as part of this correction (no scope creep).
* The sample-axis (reassociating) kernels stay benchmark-only.
* No SIMD work beyond the YIN kernels (voice_loop / interp_reads remain the
  documented next milestone — NOT started here).

## 4. Fallback statement (honest limitation)

The Auto→Scalar fallback branch is **proven by rule and by gate** on this
AVX2-capable machine (the resolver unit test exercises the no-capability
branches of the rule table). Execution on genuinely non-AVX2 hardware was
not performed in this environment (the only available machine has AVX2+FMA);
the fallback's correctness additionally rests on the dispatch design: the
AVX2 entry points are only reachable through the CPUID-cleared table, so a
non-AVX2 host cannot even form a call to them.

## 5. Where the evidence lives

* Suite (Release, new default): `evidence/regression/b2_tests_release.json`
  (110/110; `resolved_level = 1` under `simd_default_auto_neutrality`)
* Suite (ASan/UBSan): `evidence/sanitizers/b4_tests_asan.json` (110/110,
  zero reports)
* Legacy hash pins: `evidence/regression/b2_refcap_legacy_check.txt` (4/4)
* Default-record pins: inside the suite JSONs above (4/4) and
  `evidence/regression/b2_refcap_check.txt` (4/4)
* Bench at default: `evidence/benchmarks/b3_bench13_auto.json`
* Environment (CPU/compiler/build): `evidence/corrections_v2/logs/b0_baseline.txt`
