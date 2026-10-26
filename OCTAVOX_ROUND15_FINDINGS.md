# OCTAVOX ROUND 15 FINDINGS — Complete the Eight Voice Harmonizer API and Close Remaining Static DSP Questions

**Status: DELIVERED (with two documented environment incidents and full recovery).**
Native library `octavox-native 15.0.0`: **85/85 acceptance cases** (release + ASan/UBSan, zero
sanitizer reports), bit-neutral default path (4/4 reference hashes vs 12.0.0), full per-voice
harmonizer parameter surface. Static side: **Q-A, Q-B, Q-C CLOSED** with instruction-level
evidence; **Q-D recorded OPEN** (not attempted — see §2). The target binary was **never
executed**; all runtime questions stay UNK.

> **Environment incidents (full disclosure).** This round ran through TWO sandbox resets:
> #1 at 2026-10-26 ~14:32 UTC (destroyed the rounds 1–14 working tree — the previously
> delivered rounds 13/14 ZIPs and all their artifacts; recovery from the round-12 delivery
> ZIP, worklog R15-R0) and #2 at ~20:54 UTC mid-round (destroyed the rebuilt tree including
> the first 15.0.0 implementation and the raw static scan outputs; recovery in
> `/home/z/my-project/octavox-work/` — now inside the persistent project directory —
> worklog R15-R1/REB-1/REB-2). Every number below is either (a) measured on the current
> verified tree, or (b) explicitly marked as a **reconstructed record** from the preserved
> worklog. No number was fabricated; no gate was widened to pass.

## 1. Verdict table

| Item | Verdict |
|---|---|
| P1 parameter surface (per-voice ratio/gain/mute/base-delay, wet/dry, bypass) | **DELIVERED** — full API + documented semantics; default path bit-identical to 12.0.0 (4/4 FNV reference hashes) |
| P2 acceptance gates | **DELIVERED** — 85 cases: 51 frozen round-12 + 19 R13-recovery + 7 P1 + 7 r15acc; machine-readable JSON |
| P3 static questions | Q-A **CLOSED** (dead field) · Q-B **CLOSED** (cadence selector {2,4,8}) · Q-C **CLOSED** (second kernel consumer = output-stage fractional delay) · Q-D **OPEN** (not attempted, plan recorded) |
| P4 docs/version coordination | **DELIVERED** — canonical docs updated (Q-16/Q-17, R-71…R-74, S3 correction); version history 12.0.0 → 14.0.0 (recovery) → 15.0.0 documented |
| P5 performance/quality regression | **DELIVERED** — default-path cost ≈ 0, surface ≈ 0–2 %, 96 kHz rows new (2.0–2.1× RT), 0 allocations, vs the captured 14.0.0 baseline |
| Per-voice feedback routing | **DEFERRED by design** — documented as a future extension (see §3.8) |

## 2. Static closures (P3)

All evidence: `evidence/analysis_round15/NOTES.md` (reconstructed verbatim from the record;
provenance note inside — the raw scan scripts/outputs were lost in reset #2).

* **Q-A — record ring `base+2` (round-9 `i4`): CLOSED — DEAD FIELD.** Written at `0x1800E028E`
  (record writer `FUN_1800E0190`, slot `(widx−2)&0xFFF`, value `float(int(P[0]))` from the
  T+0x6058 source) and **never read anywhere in the image**. Exhaustive coverage: engine-relative
  indexed disp 0x98/0x9C/0xA0/0xA4 all forms → 68 functions / 159 hits classified (42 stack, 33
  vtable calls, 79 other-object, **5 engine sites** = 2 write-path leas + the 3 prepare reads
  +0/+1/+3); T-based/wrapper-based/odd-byte encodings → 0 sites; the **pointer form** T+0x5FD8
  (= engine+0xA4F0) is loaded only inside the writer itself; ring machinery touched only by
  ctor/writer/prepare. Honest bound: hypothetical non-standard anchor registers are not
  enumerable — none materialised image-wide.
* **Q-B — tracker `+0x5F14`: CLOSED — cadence reload value ∈ {2,4,8}** (the sample-rate class
  selector). Sole writer: the dsp_t setter `FUN_1800E10B0` ("m01", vtable slot 1), case
  paramId 5: thresholds 50000.0/100000.0 on the rate at T+0x600C → `[engine+0xA42C]=2/4/8`
  (+ per-class decimation taps and LP coefficients at T+0x5EB0..). Readers (exactly 2):
  `0x18013BDF7` reloads the countdown; `0x18013D09F` (state 6) applies multiplier
  **cadence/2 ∈ {1,2,4}** to the decimated-domain period estimate → hold override / gate
  publish / 2048·50 clamp / live-period store / record-ring write. Matches the round-6 tracker
  field table (T3). Runtime pre-setParam value UNK.
* **Q-C — interpolation kernel second consumer: CLOSED.** The observed 256×6 kernel table at
  `0x1803B6170` has **exactly two consumers**, proven by four exhaustive scans (RIP-window
  scan over all 13 116 .pdata functions = 2 exact hits; whole-image absolute-VA byte scan = 0;
  disp-0x40F0 scan = engine-only reads; ×6-idiom scan = exactly 2 functions). The second
  consumer is a **maxdelay-configured fractional-delay reader class** — the modules
  **"just for the filter" / "just for the filter2"** (add_fn thunks 0x1801374A0/0x1801374D0 →
  shared ctor 0x1800DC7A0 ["maxdelay"] → binder `FUN_1800DBEF0` storing the table ptr at
  obj+0xD0; process `FUN_1800DC430..1800DC761`: 6-tap kernel rows, row = int(frac·256),
  masked ring, mode-dependent phase) — instantiated **twice in the stereo output chain**
  (L `chain+0x1D2B8`, R `chain+0x1E240`, between `delay_xf*` and `highcut*`, feeding
  `LWetDryMix`/`RWetDryMix`). DSP role: a kernel-interpolated variable fractional delay in
  the output stage **sharing the pitch engine's interpolation kernel**. Bounded residual: the
  parametric meaning of the mode-1 four-float phase trajectory.
* **Q-D — host candidate table / scale-key: OPEN (not attempted).** Two environment resets
  consumed the round's analysis budget; the investigation plan is recorded in NOTES.md
  (search .rdata interval/mode candidate arrays; trace the wrapper entry lists via the
  round-9 modamt pattern; determine producer = host callback vs wrapper metadata vs
  .rdata constant).
* **Correction applied:** `OCTAVOX_DSP_ARCHITECTURE.md` S3's stale
  `rate = page[0]·ring[(idx+2)]` → `ring[(idx+3)]` (rounds 9/12; the idx+2 read is now
  PROVEN absent — R-71/R-74).

## 3. P1 — the per-voice harmonizer parameter surface (15.0.0)

The normative contract is the "ROUND-15 PARAMETER SURFACE" block in
`native/include/octavox/dsp.hpp`; mechanisms in `native/ARCHITECTURE.md`.

1. **Per-voice pitch ratios** — `setVoiceRatio(v, r)`: absolute per-voice target; each voice
   has its own geometric glide (rate limiter); `setRatio`/`setSpread`/`prepare` re-derive all
   voices and clear overrides (the documented simple rule). Proven: 8 voices at
   2^(k/12), k ∈ {−12,−9,−6,−3,0,+3,+6,+12}, each measured by muting the others — worst
   error 0.019 cents (gate ≤ 1.0).
2. **Per-voice gain + mute with ramping** — `setVoiceGain(v, g)` (linear-amplitude ramp over
   `Config::gain_ramp_seconds`), `setVoiceMute(v, on)` (same ramp; DSP state keeps running
   while muted). Gain multiplies after normalisation; `gain==1` is bit-neutral. Gates: 0.5
   gain ±0.1 dB; ramp steps bounded by the implementation-independent formulation
   (2×(natural step + ramp slope·amplitude), literal values recorded — a 220 Hz tone's
   natural inter-sample slope exceeds the naive ramp bound; the analysis is documented
   in-test); post-ramp output bit-identical to an unmuted reference.
3. **Per-voice base delay isolation** — `setVoiceBaseDelay(v, d)`: applied at the next block
   boundary via a voice-v-only read-position re-align (parked voices re-park). Gate: the other
   voices' output is **bit-exact identical** full-stream; the changed voice stays bounded.
4. **Wet/dry mix** — `setWetDry(w)`, in-core mixing; **the dry path is the input delayed by
   `latencySamples()`** (the ring read(d)=d−1 convention), so dry and wet are time-aligned at
   every mix value — the documented latency handling (a host mixing externally must delay the
   dry path by the same amount; in-core mixing avoids that). Gates: wet=1 → bit-identical to
   the legacy output; wet=0 → bit-exact delayed copy; wet=0.5 → linear law ≤ 1e-6 max abs.
   Feedback taps the **pre-mix wet signal** (wet-path only → the wet state is invariant under
   mix/bypass).
5. **Click-free bypass** — `setBypass(on)`: crossfades the output between the normal signal
   and the dry delayed signal over `Config::bypass_seconds`, both directions; the engine keeps
   processing while bypassed (state transparency). Gates: fully-bypassed output = bit-exact
   delayed copy; enter+exit cycle → bit-identical to a never-bypassed reference outside the
   ramp regions; transition steps bounded (implementation-independent formulation).
6. **Parameter transition semantics** — all setters are audio-thread-safe, allocation-free,
   lock-free; a change is picked up at the **start of the next process() call**
   (block-boundary application, not sample-accurate); simultaneous multi-parameter updates
   apply atomically at the boundary; large ratio changes ride the per-voice glide. Gates:
   64- vs 150-sample block splittings with the same stream-position change → outputs identical
   (max|Δ| = 0); 20-step gradual vs jumped gain → identical settled state.
7. **Default neutrality** — the primary gate of the re-implementation: four configs
   (default unity+LCG noise; +700¢ 8-voice spread+feedback; −400¢ 4-voice chirp; stereo
   unity→non-unity) produce **bit-identical output hashes vs the 12.0.0 core**
   (`native/r15_reference_hashes.txt`, tool `octavox_r15refcap`; verified on release and ASan
   builds). The mechanism: the `wet==1 && !bypass` output stage is the bit-exact legacy path.
8. **Per-voice feedback routing: DEFERRED** — the existing global `feedback` stays; per-voice
   routing would couple the voices through the shared ring and change the stability guarantees
   the acceptance battery pins (bit-exact isolation gates). Documented as a future extension
   requiring its own acceptance design; not implemented (the task book's "secondary feature
   only if it fits without breaking core stability" — it does not fit without that work).
9. **No modulation features were added** (the task book's explicit constraint: the goal is a
   practical standalone harmonizer, not an imitation of the original's random-walk modulator).

## 4. P2 — acceptance gates (85 cases, machine-readable)

`evidence/analysis_round15/r15_acceptance_results.json` (85/85 with per-case notes + measured
values). Composition: 51 frozen round-12 cases (unchanged — the bit-exact baseline),
19 R13-recovery cases (cold-start impulse alignment with independent analytic k0
re-derivation at block sizes 1/64/512 × 1/8 voices × 3 ratios; detector batteries at 96 kHz
[110/220/440/880 Hz, grain 8192/base 512 — sample-domain by design, documented] and 44.1 kHz;
estimate tracking ±5 cents at 3 rates; zero-allocation re-prepare/reset/setters), 7 P1 cases
(§3), 7 r15acc acceptance cases (ratio-change isolation — other voices bit-exact; level
accuracy ±0.1 dB over gains {0.1,0.25,0.5,1.0}; block-boundary semantics; 4-way
reset/re-prepare/restart determinism with the full surface scripted; stereo broadcast parity +
exact channel isolation under the full surface; robustness battery — silence/−60 dBFS/tone/
chord/noise/impulse-train with finite/bounded/deterministic gates; invalid-argument matrix
with bit-identical no-ops). ASan+UBSan: 85/85, zero reports.
**Measurement discipline held:** every gate fixed before running; the corrected Hann-exact
3-bin pitch estimator everywhere (self-test bias 4.29e-11 cents; windows never read before the
supplied buffer — the historical routine defect is fixed in the harness); undecidable literal
bounds reformulated implementation-independently with the literal values recorded in-test.

## 5. P5 — performance & quality regression (vs the 14.0.0 baseline)

`evidence/analysis_round15/r15_bench_comparison.md` + `r15_bench_15.txt`; baseline
`evidence/analysis_round14_recovery/r14_baseline_bench_stdout.txt` (14.0.0: mono 1v
1341–1358 ns/frame = 15.5× RT, 8v 1566–1571 = 13.3×, stereo 2716/3112, ctrl512 408 ns =
51×; 0 allocations).

* **Default-path cost ≈ 0**: 15.0.0 mono 1v 1308–1338 ns/frame, 8v 1533–1557 — all deltas
  within run noise (the legacy output path is cost-neutral).
* **Per-voice control overhead ≈ 0–2 %**: the surface case (per-voice overrides, two mutes,
  wet 0.5, mid-run gain retargets inside the timed loop) runs 1520 vs 1557 ns/frame.
* **96 kHz rows (new)**: 4998/5211 ns/frame = 2.08×/2.00× realtime margin. The per-sample
  cost scales ≈3.8× vs 48 kHz because the R13-b detector window law
  `win = max(2048, ceil(2.5·τmax))` grows with τmax = sr/fmin — the price of fixing the
  round-12 core's 96 kHz blindness (it reported 0.000000 Hz on a 110 Hz tone; 15.0.0 tracks
  110.00003 Hz). Still comfortably realtime.
* **Memory**: +2–4 MB RSS (dry ring per engine + the detector window); **0 allocations**
  steady-state in every case.
* **Quality vs the round-14 baseline** (reconstructed record,
  `evidence/analysis_round14_recovery/r14recovery_reconstructed.md`): tracker/rate-limiter
  parity vs the Python reference — estimate trajectories agree to 6.2e-12 cents, glide
  convergence identical (17344 samples, analytic 17317), unity bit-exact 0.000000000 dB vs
  the reference's −2.4181 dB comb; four-mode interpolation comparison — **Lagrange6 stays the
  default** (best near-Nyquist −79.2 dB and below-band −46.5 dB residuals; ObsKernel6's raw
  OOB lead trades away 7.3 dB below-band and 17.8 dB near-Nyquist — not a net win; opt-in
  research only).

## 6. VST3 readiness (P4)

`native/VST3_READINESS.md`: **READY** — process-cycle contract (planar, block-size agnostic),
buses (structural channel isolation), rate/block-size changes (zero-alloc re-prepare fast
path), allocations (counter-verified zero steady-state), threading (RT contract), realtime
margin (13–16× mono, 6–8× stereo at 48 kHz). **PARTIAL** — parameter surface (block-boundary
application vs VST3 sample-accurate parameter segments: host-side ramp today, or a future core
extension) and state persistence (deterministic reset/re-prepare, but no serialization API —
a wrapper must snapshot Config + the live per-voice surface). **No wrapper is built** — by
design, a later milestone only after this acceptance battery is green (per the task book).

## 7. Artifacts & reproduction (working tree: `octavox-work/Octavox_RE_evidence/`)

```bash
export PATH=/home/z/.local/bin:$PATH
cd native
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2
./build/octavox_tests                 # 85/85 (≈30 s)
./build/octavox_tests --json ../evidence/analysis_round15/r15_acceptance_results.json
./build/octavox_bench                 # 11 cases, 0 allocations
./build/octavox_r15refcap             # the four neutrality hashes vs r15_reference_hashes.txt
# ASan+UBSan:
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON && cmake --build build-asan -j2
./build-asan/octavox_tests            # 85/85, zero sanitizer reports
```
Static evidence: `evidence/analysis_round15/NOTES.md` (Q-A/Q-B/Q-C closures, reconstructed
record + provenance). Recovery evidence: `evidence/analysis_round14_recovery/`
(`r14recovery_reconstructed.md`, `r14_baseline_bench_stdout.txt`). Delivery:
`DELIVERY_ROUND15.md` + regenerated `checksums.txt` manifest.

## 8. UNK residue & known limitations

* Runtime behaviour of the target: **UNK throughout** (never executed — unchanged since
  round 1). All static closures are OBS/DER at the instruction level.
* Q-D (host candidate table / scale-key) — OPEN with a recorded plan (§2).
* Bounded residuals: the mode-1 four-float phase trajectory of the output-stage delay
  modules (Q-C residual); the record-ring first-field scaling (round-12 residual);
  T+0x5F14's pre-setParam value; the exact design formula of the observed kernel.
* The 96 kHz realtime margin (2.0–2.1×) is the tightest measured figure; a ctrl-512 variant
  was not run (noted in the comparison md).
* Provenance caveat: the parity/interpolation numbers and the static scan raw outputs are
  reconstructed records from the preserved worklog (reset #2) — the verdicts and values are
  the record; the raw scripts were lost and are NOT re-shipped. Everything in §3–§6 was
  re-measured on the current tree.
* The historical rounds-13/14 ZIPs referenced by the original task book do not exist in this
  environment (destroyed by reset #1); this round rebuilt their scope (v14.0.0 recovery
  record) and delivered the round-15 scope on top (v15.0.0). Version references were
  coordinated accordingly (12.0.0 → 14.0.0 → 15.0.0) — see `native/README.md`.
