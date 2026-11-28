# OCTAVOX / PitchLab — R34: The Chain-End Stereo Width Stage (energy-preserving)

**Round:** R34 · **Core:** octavox-native 16.4.0 (audio-path addition; the
default path stays BIT-NEUTRAL — the r15acc determinism hash
`0x35fe0d51a4372ac8` is IDENTICAL to the R31/R32/R33 records) ·
**Original binary: never executed** (no new RE claims — this round is OWN
DESIGN on the PitchLab wrapper line, like R31/R33).

**User mandate:** "itt a chain végére szeretnék kérni egy új funkciót.
Szeretném, ha a stereo kép szélességét lehetne állítani akár teljesen
monóra összehúzhatóvá kellene tenni. A twist itt az lenne, hogy közben a
hang energiáját meg kellene tudni tartani. Kérlek első körben menj utána
annak, hogy milyen lehetőségek vannak erre. Majd implementáljunk egyet."
(A new feature at the END of the chain: adjustable stereo image width,
collapsible all the way to mono, with the sound's energy maintained.
Research the options first, then implement one.)

**Part A — the options research** lives in
`OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md` (the option space A–F with full
derivations, the decision record for Option C — the correlation-adaptive
gain — and the honest R34 correction note). This report is Part B: the
implementation and its verification.

---

## 1. What was built

A **stereo width stage at the very end of the DSP chain** — inside
`MultichannelShifter::process()`, after every per-channel engine, before
the buffers return to the host. The mono `AlignedDelayLine` engines are
NOT touched (channel isolation stays structural; the four r15 reference
hashes are bit-exact by construction, re-gated this round).

One new host parameter:

* **"Stereo Width"** (id 38, appended after the per-voice block so every
  pre-R34 id keeps its slot — DAW automation compatibility), domain
  [0,1] continuous, default **1.0 = the original image, bit-neutral**,
  0.0 = **full mono collapse**. Distinct from the Harmony Map (id 3),
  which is the musical per-voice interval switch, NOT stereo widening
  (the R21-P0 rename anticipated exactly this confusion; the new
  parameter's help text states the distinction).

The stage (`include/octavox/stereo_width.hpp` + `src/stereo_width.cpp`,
~200 lines including the full contract comments):

* **The law (mid-only-gain structure):** the orthonormal M/S decomposition
  with `S -> w*S` and a mid gain `k` gives
  `E_out = k^2*E[M^2] + w^2*E[S^2]`, so the unique energy-preserving mid
  gain for the measured correlation `rho = 2*E[LR]/(E[L^2]+E[R^2])` is
  ```
  k(w, rho) = sqrt( (2 - w^2*(1-rho)) / (1+rho) )
  ```
  applied as the symmetric matrix `L' = aL+bR, R' = bL+aR` with
  `a=(k+w)/2, b=(k-w)/2` — which carries two exact structural
  identities: `L'-R' = w*(L-R)` (the side scaling is exact; **w=0
  collapses to mono BITWISE: L'==R'**) and `L'+R' = k*(L+R)`.
* **Sanity pins:** w=1 → k=1 for any rho; w=0, rho=+1 → k=1 exactly
  (dual-mono content collapses at UNITY — the property that rejects the
  static equal-power law, which would be +3 dB on THIS plugin's own
  default dual-mono output); w=0, rho=0 → sqrt(2) (+3.01 dB, the classic
  uncorrelated-sum law); w=0.5, rho=0 → sqrt(1.75) (+2.46 dB).
* **Measurement:** feed-forward on the stage's INPUT (never the output —
  no feedback loop): three running means E[L^2], E[R^2], E[LR] with an
  exact per-sample one-pole Euler smoother (tau = 200 ms nominal; NO
  transcendental in the block path — sqrt/division are the only
  non-basic ops and both are IEEE-exact, so the ACTIVE stage is
  deterministic and reproducible bit-for-bit on any IEEE-754 platform).
* **Activation seed:** the first block after (re)activation seeds the
  smoother with that block's aggregate means (correct compensation from
  the first post-activation sample; a documented block-boundary semantic
  of the same class as the engine's parameter law). A settled
  deactivation re-arms the seed (no stale rho).
* **(a,b) slew-rate limited** (10 ms full-scale span): width drags,
  bypass toggles and activation are click-free (bounded per-sample
  matrix motion; the DC-input gate proves the exact bound).
* **kMaxGain clamp = 2.0 (+6.02 dB):** the honest rho -> -1 bound
  (out-of-phase content has zero mid; it cannot survive a mono collapse
  at any finite gain — documented, clamped, tested as the honest digital
  zero).
* **k >= 1 always** (for w <= 1): compensated narrowing never attenuates.
* **BIT-NEUTRAL DEFAULT:** at width 1.0 with the matrix settled at the
  exact identity, `process()` returns without touching a single byte
  (the same discipline as the R31 observers; gated by the whole 115-case
  pre-R34 suite passing unchanged + the r15acc hash identity).
* **Bypass gating:** while the wrapper's bypass is engaged the stage
  targets the exact identity (bypass = the plugin does nothing), slewing
  out/in click-free, then goes inert.
* channels < 2: structural no-op. channels >= 2: the first two channels
  (L/R); further channels pass through (the wrapper exposes a stereo
  bus). RT-safe: allocation-free, lock-free (gated).

**State blob v3:** the 39th plain double (Stereo Width) appended; v1/v2
blobs remain READABLE with the width defaulting to the bit-neutral 1.0
(gated by the new `state_v3_width_backcompat` case with a hand-crafted
v2 blob). Total v3 size: 464 bytes.

**The UI (R33's minimal editor extended):** a fourth global slider
**WIDTH** on row B (x 576..726, percent readout), in the hit-map, the
drag protocol, and the double-click default; the row-B identity line
moved to the footer ("R34 - FIXED 784x416"). The golden full-frame hash
was **re-pinned DELIBERATELY** for the new layout: FNV-1a
`0x929df78511dc9220` (from the R33 pin `0x0f80c7f43a8358ad`).

## 2. The R34 correction (recorded honestly; caught by the gate)

The first derivation of the energy law (in the research record, section 2)
folded the compensation gain into BOTH paths — the correct law for a
GLOBAL-gain structure, but NOT for the mid-only structure implemented
here. The two laws agree only at w=0 and w=1; the
`r34_energy_uncorrelated` gate at **w=0.5 measured -0.343 dB**, matching
the wrong law's prediction (0.925 energy ratio) to the digit. The
corrected law above is what the implementation and the tests now pin; the
research record carries the full correction note. This is exactly the
class of error the verification plan exists to catch — and did.

## 3. Verification (all green; logs in octavox-work/r34/evidence/)

| Gate | Result |
|---|---|
| Core suite, Release (fresh build_r34) | **126/126** (115 pre-R34 unchanged + 11 new `r34` cases) |
| r15acc determinism hash | `0x35fe0d51a4372ac8` — IDENTICAL to R31/R32/R33 (default bit-neutrality) |
| Core suite, ASan/UBSan/LSan | **126/126, zero reports, zero leaks** |
| Wrapper tests (Release + ASan) | **22/22** (39-param surface, state v3 + v2 back-compat, full-surface restore incl. width) |
| UI tests (Release + ASan) | **11/11** (hit-map 39/39, WIDTH drag + default, X11 pixel identity, the re-pinned golden) |
| Module integration (Release + ASan/LSan) | **18/18** (the new `stereo_width_module` case drives the SHIPPED module: real queue -> bitwise mono, **+0.013 dB** steady-state energy delta, bypass gate bit-exact) |
| Steinberg validator | **47/47** on the fresh bundle |
| Windows cross-build (zig 0.13.0) | machine-code + structural record: ymm=275 vfmadd=26 cpuid=3 xgetbv=1 (identical to R32-W/R33-W; `stereo_width.obj` baseline-ISA), zero X11 refs, same 12 system imports, moduleinfo byte-identical to the twin |

The 11 new core cases (group `r34`): default bit-exactness vs hand-driven
engines; the bitwise mono collapse; the dual-mono exact-unity identity
(zero tolerance — the full derivation chain rho=+1 → k=1 → (L+R)/2 exact);
uncorrelated energy preservation at w=0 and w=0.5 (±0.1 dB); the
partial-correlation gain law at rho=±0.5 (2%); the anti-correlated clamp;
the DC-input exact slew bound through activation + re-target; bypass
gating (bit-equal to the never-touched reference); automation determinism
(bit-identical double run); RT allocation-freedom; silence stability
(exact-zero outputs, frozen measurement, no NaN).

**The ASan gate's own find:** the first ASan run of the NEW ui-test code
exposed a stack-use-after-scope (the mock component handler's scope vs
the processor's late release) — fixed at the source (the explicit detach
in the test), the suite re-run green. Product code was not affected.

## 4. Key measured values

* uncorrelated, w=0: energy delta **-0.0016 dB** (k = 1.4185 ≈ sqrt(2)
  for the engine's residual rho ≈ -0.006)
* uncorrelated, w=0.5: energy delta **-0.0012 dB**, k = 1.3263
* rho=+0.5: k = 1.1572 vs law 1.1547 (0.22%, the EMA window vs the
  population value); rho=-0.5: k clamped at 2.0
* dual-mono, w=0: **per-sample EXACT unity** (bit-equal to the reference)
* width step 1→0: worst per-sample output step 0.00333336 vs the
  documented slew bound 0.00333343
* module-level (the shipped .so): non-mono samples after settle = 0;
  steady-state energy delta **+0.013 dB**; bypass gate bit-exact

## 5. Deliverables

* `OCTAVOX_R34_STEREO_WIDTH_RESEARCH.md` (root; Part A — the options
  research + the decision + the correction note) + this report (Part B)
* `octavox-work/r34/evidence/` (8 records: the suite, ASan, wrapper, UI,
  module, validator logs + the Windows machine-code record + the build
  environment)
* `public/PitchLab_R34_Stereo_Width_VST3.zip` (the Linux AND Windows
  bundles + PDB + README + report + research record + logs + the two
  real-render screenshots; SHA-256 in the sidecar)
* The web page R34 section (ROUND 34 badge, delivery cards incl. the
  editor screenshot with the WIDTH slider at 60% in the demo frame)
* The core: `include/octavox/stereo_width.hpp` + `src/stereo_width.cpp`
  + the `MultichannelShifter` wiring + the 39th parameter + state v3 +
  the UI WIDTH slider

## 6. Honest limits

* Compensated narrowing preserves ENERGY, not PEAKS: +3 dB RMS on
  uncorrelated content can raise sample peaks (the float pipeline has no
  internal headroom issue; a host's final integer conversion is the
  host's domain). Documented, not limited — a limiter is a different
  round.
* The compensation is exact for STATIONARY content; image changes faster
  than the ~200 ms smoother transiently deviate by the measurement's lag
  (the deliberate quasi-static trade-off over AGC pumping).
* Anti-correlated content cannot be energy-preserved through a mono
  collapse by ANY gain (zero mid); the clamp is the honest behaviour.
* The exposed range is [0,1] (mono..original): widening beyond the
  original image is deliberately not exposed (the law extends trivially;
  documented as future work). Frequency-dependent width (the
  Blumlein/Gerzon shuffler) is deferred (Option E in the research
  record).
* macOS: unchanged from R33 (not implemented). Windows: machine-code
  verified only (no PE execution in this sandbox; the identical stage
  logic is executed end-to-end on Linux).
* Not a DAW compatibility claim.
