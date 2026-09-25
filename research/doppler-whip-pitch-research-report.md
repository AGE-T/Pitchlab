# Physical Doppler Crack & Multi-Algorithm Pitch Engine
## Unified Technical Research Report

**Status:** Research only — no GUI design, no implementation. This report determines technical viability, unsuitable approaches, and what should be prototyped.
**Date compiled:** 2026-02-14
**Method:** Web-verified primary sources (papers, license files, official documentation), cross-checked against engineering reasoning. Five parallel research tracks were executed (whip physics, acoustics/Doppler, time-domain pitch, spectral pitch, libraries/licensing).

---

## Claim classification tags used throughout

| Tag | Meaning |
|---|---|
| **[VERIFIED-PAPER]** | Claim found in/quoted from a specific peer-reviewed paper or its abstract (source URL captured during research) |
| **[VERIFIED-SOURCE]** | Claim from official documentation, vendor site, or standard textbook material (URL captured) |
| **[VERIFIED-LICENSE-FILE]** | Licensing claim verified against the actual license text/file, not a repository description |
| **[INFERENCE]** | Clearly identified engineering inference (derived from verified equations/principles, but not directly measured or published for our exact use case) |
| **[UNPROVEN]** | Plausible but not verified — flagged for the prototype phase |

**Core research principle applied:** an existing implementation that produces a *Doppler-like effect* is not automatically physically correct. Each claim above is tagged so that "works in practice" and "is physically motivated" are never conflated.

---

## Table of contents

- **Part I — Section A: Physical Doppler and Whip Crack**
  - A.1 Established scientific knowledge
  - A.2 Published algorithms and physical models
  - A.3 Existing open-source implementations
  - A.4 Engineering inference: model selection for our engine
  - A.5 Unproven hypotheses
  - A.6 Recommended experiments (incl. the Model A vs Model B critical experiment)
  - A.7 Known failure modes and possible dead ends
  - A.8 Classification summary
  - A.9 Primary references
- **Part II — Section B: Multi-Algorithm Pitch Engine**
  - B.1 Algorithm catalogue (21 algorithms, full attribute grid)
  - B.2 Dynamic pitch-curve test battery (special focus)
  - B.3 Architecture research (PitchProcessor interface, shared components, Doppler bypass)
  - B.4 Licensing classification
  - B.5 Benchmark strategy (corpus, metrics, listening tests)
  - B.6 Final recommendations (minimum prototype set → architecture proposal)
  - B.7 References

---

# PART I — SECTION A: PHYSICAL DOPPLER AND WHIP CRACK

## A.1 Established scientific knowledge

### A.1.1 Whip mechanics: the physical mechanism of the crack

**Mechanism (established):**

1. The whip is a **tapered flexible rod**: mass per unit length decreases by orders of magnitude from handle to tip (a full bullwhip weighs on the order of 0.5–1 kg; the terminal cracker is a fraction of a gram). **[VERIFIED-PAPER]** — taper framing appears in Goriely & McMillen 2002.
2. A **loop (travelling kink)** is launched from the handle and propagates down the whip. As it travels into ever-lighter material, the energy concentrated in the loop is delivered to progressively less mass, amplifying velocity. **[VERIFIED-PAPER]** — energy-conservation analysis in Goriely & McMillen 2002.
3. Near the tip, the taper is so extreme that the **tip (and the loop's turning region) exceeds the speed of sound in air (~343 m/s at 20 °C)**, radiating a shock wave. **[VERIFIED-PAPER]** — Goriely & McMillen 2002; Bernstein, Hall & Trent 1958.
4. The crack is therefore a **miniature sonic boom**, **not** a "slap" of whip against itself or air. Bernstein, Hall & Trent explicitly reject the slap hypothesis after high-speed photography. **[VERIFIED-PAPER]**

**Key subtlety (established, often misquoted):** the *loop itself* does not simply accelerate without bound as 1/√m — Goriely & McMillen's analysis shows the loop speed stays roughly constant while the *tip/tail that turns around the loop* is what accelerates past Mach 1. **[INFERENCE from paper abstracts + secondary sources]** — the precise loop-velocity result should be confirmed from the free PDF of the PRL (goriely.com) during prototyping; this is a known nuance of the paper, but our search captured only the abstract.

**Historical note:** the supersonic-tip explanation was first speculated by Lummer (1905); the first modern experimental support came from Carrière (1927) using chronophotography. **[VERIFIED-SOURCE — via Krehl's historical survey]**

### A.1.2 Experimental validation data

| Study | Method | Key quantitative findings |
|---|---|---|
| **Carrière (1927)**, *J. Phys. et le Radium* 8(9):365–384 | Chronophotography of the whip/cracker motion | First quantitative claim that the crack coincides with the cracker exceeding the speed of sound. Numbers known mainly via secondary analysis. **[VERIFIED-PAPER]** (citation; specifics via secondary source) |
| **Bernstein, Hall & Trent (1958)**, "On the Dynamics of a Bull Whip", *JASA* 30(12):1112–1115 | High-speed (spark) photography | Crack is produced by the tip **exceeding the speed of sound and radiating shock waves**, "rather than by the whip slapping." **[VERIFIED-PAPER]** Tip Mach numbers approaching ≈ 2 are commonly attributed to this study; the exact peak number should be confirmed from the JASA PDF **[UNPROVEN — number not in retrieved snippet]** |
| **Krehl, Engemann & Schwenkel (1998)**, "The puzzle of whip cracking — uncovered by a correlation of whip-tip kinematics with shock wave emission", *Shock Waves* 8(1):1–12 | Correlated high-speed kinematics + shock visualization | Tip is supersonic for **≈ 1.2 ms**; emits a **head wave (bow shock) with parabolic geometry** attached to the supersonic region; also documents the travelling loop. **[VERIFIED-PAPER]** |
| **Goriely & McMillen (2002)**, "Shape of a Cracking Whip", *Phys. Rev. Lett.* 88(24):244301 | Tapered-rod model + energy-conservation analysis + numerical simulation | "The crack of a whip is produced by a shock wave created by the supersonic motion of the tip of the whip in the air." Simulation reproduces the observed loop/turning-point geometry; tip exceeds Mach 1 with realistic parameters. **[VERIFIED-PAPER]** |
| **McMillen & Goriely (2003)**, "Whip waves", *Physica D* 184:192–225 | Full numerical simulation of the nonlinear equations of a tapered rod (finite differences) | Crack = "mini-sonic boom created by a supersonic motion of the end of the whip". Establishes the required simulation machinery for ground-truth offline reference. **[VERIFIED-PAPER]** |

**Answers to the specific research questions:**

- **Tip velocity:** exceeds Mach 1 (established); reported/attributed peak values up to ~Mach 2 (Bernstein et al. attribution; Goriely-McMillen simulation) **[UNPROVEN exact value — confirm from PDFs]**.
- **Tip acceleration:** no number was retrievable from abstracts/snippets. Values on the order of 10⁴–10⁵ g circulate informally. **[UNPROVEN]** — measure or confirm from PRL figures during prototyping.
- **Duration of supersonic motion:** **≈ 1.2 ms** (Krehl et al. 1998). **[VERIFIED-PAPER]**
- **Location of shock emission:** parabolic **head wave attached to the supersonic tip region** (Krehl et al.). Popular high-speed-video analyses (e.g., SmarterEveryDay) point at the **loop's turning point** as the tip unwinds. These are consistent: the turning point *is* where the tip is fastest. Mild source disagreement remains in framing. **[VERIFIED-PAPER + VERIFIED-SOURCE]**
- **Does the shock occur exactly at Mach 1 or later?** The tip stays supersonic for ~1.2 ms and radiates throughout; the audible crack is the head-wave/N-wave arriving at the listener. The emission therefore **begins at the Mach-1 crossing** and continues through the supersonic window; the loudest arrival depends on listener geometry (closest approach, directivity). **[VERIFIED-PAPER for the 1.2 ms emission window; timing nuance = INFERENCE]**
- **SPL of a whip crack:** no peer-reviewed measurement was found by any of our search tracks. Anecdotal values: ~150 dB SPL at 1 inch, ~120 dB at 1 m (folklore/Reddit); impulse-noise regulatory context (140 dB peak OSHA impulse limit class). **[UNPROVEN — this is a required measurement in A.6.3]**
- **Spectral characteristics of real cracks:** no published spectrum located. **[UNPROVEN — required measurement]**

### A.1.3 Acoustic shock physics: N-waves, Mach cones, nonlinear propagation

**Established:**

- **Sonic-boom N-wave (far field):** pressure signature with a sharp rise to +P, near-linear decay through zero to −P, and a sharp return. For standard simulated aircraft booms: rise time ≈ 3 ms, positive-phase duration ~100–300 ms, overpressure ~50 Pa (~128 dB SPL) after km-scale propagation. **[VERIFIED-SOURCE — NASA NTRS, Leatherwood 1993; Sparrow 2010]**
- **Mach cone:** sin µ = 1/M — the shock is the envelope of piled-up wavefronts from a supersonic source. **[VERIFIED-SOURCE — standard textbook treatment]**
- **Friedlander blast waveform:** p(t) = P₊(1 − t/T₊)·e^(−b·t/T₊) — the most commonly used analytical model for (single-sided) blast pressure histories, including recorded gunshot waveforms (Beck et al. 2011, *JASA*, "Variations in recorded acoustic gunshot waveforms"). **[VERIFIED-PAPER]**
- **Nonlinear steepening:** in lossless nonlinear acoustics (Burgers-type evolution), compressive parts of a wave steepen into shocks; the finite **rise time** observed at any distance is set by the balance of steepening vs. absorption (viscous + relaxation). **[VERIFIED-SOURCE — recent review literature on lossy nonlinear wave equations, e.g., Kaltenbacher 2025, arXiv:2502.08194]**
- **Scale law (small objects):** rise time grows with propagation distance. Kilometre-scale aircraft paths → ms-scale rise; metre-scale bullet/whip paths → µs-scale rise and sub-ms N duration. **[INFERENCE — consistent across the aircraft vs. small-ballistic-object literature; no direct whip measurement retrieved]**
- **Bullet analogy:** a supersonic bullet radiates a ballistic (bow) shock distinct from the muzzle blast; this is the nearest well-documented analogue to whip-crack acoustics. **[VERIFIED-PAPER — Beck et al. 2011]**

**Consequence for us:** at listener distances of ~1–3 m, modelling the whip crack as an **analytic N-wave/Friedlander pulse with physically derived timing, amplitude, and duration** is defensible: metre-scale ballistic shocks are observed as N-type waves at listener range. **[INFERENCE, grounded in the above]**

### A.1.4 Doppler theory: what each formulation actually computes

**Established:**

1. **Classical moving-source Doppler:**
   f′ = f₀ · c / (c − v_s·cos θ)
   Assumptions: point source, steady tone, constant subsonic velocity, far field, lossless non-dispersive medium. **[VERIFIED-SOURCE — textbook]** It is a *frequency formula*, not a waveform operator: it carries no information about waveform memory, transients, or temporal compression of event sequences.
   - As v_s → c the formula diverges (singularity).
   - For v_s > c it is simply invalid (no continuous arrival exists; a shock replaces the tone). **[VERIFIED-SOURCE]**

2. **Retarded-time (propagation-time) formulation:**
   t = τ + R(τ)/c,  where R(τ) = |r_s(τ) − r_obs|
   This maps emission time τ to arrival time t. **[VERIFIED-SOURCE — classical acoustics]**
   - dt/dτ = 1 + R′(τ)/c = 1 − M_r(τ), where M_r = −R′(τ)/c is the radial Mach number (positive when approaching).
   - The observed compression of the emission history is exactly 1/(dt/dτ) = **1/(1 − M_r)** — this reproduces the classical Doppler factor *including* its behaviour as M_r → 1 (unbounded pile-up). **[INFERENCE — direct consequence of the verified map]**
   - Subsonic: the map is monotonic → unique emission time per arrival time.
   - Supersonic: the map becomes non-monotonic → **multiple emission times map to one arrival time**; the wavefront envelope is the Mach cone. **[INFERENCE, supported by the verified Mach-cone envelope geometry]**

3. **Time-varying delay (the audio-engineering form):**
   y(t) = x(t − D(t))
   Instantaneous frequency ratio = 1 − D′(t). **[VERIFIED-SOURCE — J.O. Smith, *Physical Audio Signal Processing*, Doppler-simulation chapter; the delay-line construction is documented, the ratio statement is the standard result of that chapter — INFERENCE for the exact equation]**

4. **Distributed (multi-point) source propagation:**
   p(r_obs, t) ≈ Σᵢ wᵢ(τᵢ) · s(τᵢ) / (Rᵢ(τᵢ)·(1 − M_{r,i}))
   with per-source retarded times τᵢ solving t = τᵢ + Rᵢ(τᵢ)/c; 1/R spreading; the 1/(1−M_r) factor is the standard convective amplification of a moving monopole. **[INFERENCE — standard aeroacoustics result; no snippet captured the full expression]**

**Critical engineering nuance discovered during this research (important — many "Doppler plugins" get this wrong):**

- If the delay is built from the **receiver-time** distance, D(t) = R(t)/c, then D′(t) = R′(t)/c is bounded by v_s/c < 1, so the instantaneous ratio is bounded to [1 − v_s/c, 1 + v_s/c] ⊂ (0, 2]. **The compression factor caps at ≤ 2 no matter how close to Mach 1 the source gets.** **[INFERENCE — direct derivation]**
- Only the **implicit (retarded-time) delay** D(t) satisfying D(t) = R(t − D(t))/c reproduces the true 1/(1 − M_r) singularity and the correct waveform pile-up approaching Mach 1. **[INFERENCE — direct derivation from the verified retarded-time map]**
- Practical consequence: a physically honest Doppler core must wrap a small retarded-time solver (e.g., 3–5 fixed-point iterations per sample/block) around the delay line, rather than reading the instantaneous distance.

## A.2 Published algorithms and physical models

### A.2.1 Whip-motion models (hierarchy from full physics to kinematics)

| Model | Description | Cost | Status |
|---|---|---|---|
| **M1 — Full nonlinear tapered-rod simulation** | Finite-difference solution of the nonlinear equations of motion of a tapered elastic rod (the "Whip waves" machinery). | Offline only; stiff equations; implicit schemes in the literature. | **[VERIFIED-PAPER — McMillen & Goriely 2003]** Ground-truth reference for validating reduced models. |
| **M2 — N-segment lumped mass–spring–damper chain** | Whip discretized into N segments with mass law m(s) = ρ·A(s), symplectic/semi-implicit integration; read out per-segment position/velocity. | Real-time feasible for N ≤ 256 on a modern core. Risk: numerical stiffness from the taper ratio. | **[INFERENCE — standard MBS reduction of the rod model]** |
| **M3 — Loop/energy reduced-order model** | ODE tracking the loop position with energy conservation across the taper: as the loop advances into lighter material, conservation of (a controlled fraction of) the loop energy gives the velocity amplification; tip turn-around kinematics close the model. | Trivial (a few scalar ODEs). Analytically controllable (taper profile, injected energy). | **[INFERENCE — directly inspired by the energy-conservation analysis in Goriely & McMillen 2002]** |
| **M4 — Kinematic trajectory playback** | Parametric tip/loop trajectory (prescribed loop-radius shrink, angular-speed ramp, supersonic window ≈ 1.2 ms, peak Mach ≈ 1.5–2), aligned with published kinematics. | Zero physics cost; fully artist-controllable; perfectly suited to VST parameterization. | **[INFERENCE — engineering construction constrained by VERIFIED-PAPER numbers]** |

**Minimum physical model that can plausibly run in real time:** M3 or M4 for the product; M2 as the first "honest physics" step; M1 kept offline as validation ground truth. **A full finite-element / Cosserat-rod simulation is NOT required** — this is the explicit conclusion of the hierarchy above. **[INFERENCE]**

### A.2.2 Shock-generation models (published analytical forms)

- **N-wave (far-field double pulse):** sharp rise → linear decay → sharp return; parameterized by peak overpressure P, positive duration T, rise time τ_r. **[VERIFIED-SOURCE — NASA sonic-boom literature]**
- **Friedlander waveform:** p(t) = P₊(1 − t/T₊)·e^(−b t/T₊) — standard blast model (gunshot literature). **[VERIFIED-PAPER — Beck et al. 2011]**
- **Ballistic-shock scaling:** for a slender supersonic body, near-field overpressure scales with body diameter and Mach number; far-field N-wave duration scales with body length / propagation distance. **[INFERENCE — standard shock physics; Whitham-lineage theory. Exact Whitham F-function specifics were not retrievable in this pass **UNPROVEN**]**
- **Nonlinear propagation (Burgers / lossy wave equation):** needed only if we wish to *derive* the rise time from first principles rather than parameterize it. Real-time feasible in reduced form (age-variable approximation) but **not necessary for v1** — parameterized rise time suffices. **[INFERENCE]**

### A.2.3 Doppler implementations (published audio-side machinery)

- **Interpolating/de-interpolating delay-line Doppler** (J.O. Smith, DAFx & *Physical Audio Signal Processing*): the standard real-time machinery for moving-source Doppler; y(t) = x(t − D(t)) with fractional interpolation; cross-fade of read pointers needed when read/write pointers approach each other. **[VERIFIED-SOURCE]**
- **Game-middleware practice (Wwise/FMOD):** our searches did **not** retrieve the official Doppler documentation pages (negative result). Conventional wisdom — per-frame radial-velocity ratio, source speed clamped below c — remains **[UNPROVEN]** here; verify directly in the middleware docs if relevant. This does not affect our design (we do not target middleware).
- **Sonic-boom focus/caustic modelling (accelerating supersonic flight):** overpressure can reach ≈ 4× cruise value during focused/accelerating boom conditions (Mach 2). **[VERIFIED-SOURCE — AIP/JASA 2005]** Interesting for us: the whip tip is *decelerating/accelerating through* Mach 1, i.e., exactly the regime where boom focusing/steepening is strongest.

## A.3 Existing open-source implementations

**Negative result (explicit):** no open-source, physically-motivated whip-crack *audio* synthesis implementation was found. **[UNPROVEN domain — but after multiple dedicated searches across all five research tracks, nothing surfaced]**

What does exist:

- **Physics side:** numerical whip simulations exist only inside the academic literature (PRL 2002 / Physica D 2003 — papers, not libraries). No reusable open-source whip-dynamics code was located.
- **Audio side (Doppler):** all open-source Doppler audio plugins found are copyleft and simple (variable delay + distance gain + pan): `igorski/delirion` (GPL-3.0), `usdivad/Melodrumatic` (GPL-3.0, "pitch-shift via delay i.e. the Doppler effect"), `ovniaudio/ovni` (AGPL-3.0, HRTF/Doppler/physical-reverb spatializer). None handle retarded-time exactness or supersonic sources. **[VERIFIED-LICENSE-FILE for each]**
- **Audio side (shock/crack):** no procedural whip-crack synthesizer was found; the Friedlander/N-wave analytical forms are trivial to implement from the cited blast literature. **[INFERENCE]**

**Consequence:** the physical Doppler + crack engine must be **built from scratch** — which is acceptable because (a) basic Doppler via variable delay is trivial, (b) the physics core is deliberately reduced-order, and (c) there is no licensing exposure. **[INFERENCE]**

## A.4 Engineering inference: model selection for our engine

### A.4.1 Doppler model comparison (the five candidate formulations)

| # | Model | Mathematical basis | Advantages | Limitations | RT feasibility | Near Mach 1 | Above Mach 1 | Natural temporal compression |
|---|---|---|---|---|---|---|---|---|
| 1 | **Classical source-velocity formula** | f′ = f₀·c/(c − v_s cosθ) | Exact for its assumptions; one multiply | Tone-only; singular at M=1; no waveform memory | Trivial | Diverges | Invalid | None (frequency formula only) |
| 2 | **Per-frame radial-velocity ratio (game style)** | ratio = 1/(1 − M_r) applied per block/frame | Cheap; stable; bounded | Discontinuous pitch; no transient/event compression; block-quantized; ignores emission-time nonuniformity | Trivial | Clamped by implementation | Clamped/undefined | No |
| 3 | **Time-varying delay, naive (receiver-time distance)** | y(t) = x(t − R(t)/c) | Continuous; compresses waveform events; smooth; trivial implementation | **Compression ratio caps at 1 + v_s/c ≤ 2** — wrong physics near Mach 1 | Excellent (one fractional read/sample) | Benign but WRONG (no pile-up) | Fails silently | Yes, but bounded ≤ 2× |
| 4 | **Time-varying delay, implicit retarded time** | D(t) = R(t − D(t))/c (3–5 fixed-point iterations) | Physically exact subsonic; reproduces true 1/(1−M_r) singularity; compresses waveform events; per-sample control | Iteration cost; unbounded gain at M=1 (needs saturation); breaks at M=1 (map folds) | Good | Correct blow-up → must saturate; the saturation *is* the crack onset cue | Multi-root — must hand over to event/shock model | **Yes — exact** |
| 5 | **Distributed multi-point retarded propagation** | Σᵢ wᵢ(τᵢ)·s(τᵢ)/(Rᵢ(1−M_{r,i})) | Spatially extended source; natural directivity; whoosh + crack from one mechanism; handles per-segment Mach | O(N·iters) per sample; caustic singularities per segment; complex above M=1 | Moderate (N ≤ 64–128 fine on a modern core) | Per-segment blow-ups | Mach-cone arrival ordering appears naturally | **Yes — exact + spatially structured** |

**Selection conclusion [INFERENCE]:** Model 4 (as the single-source core) and Model 5 (as the whip's spatial extension) are the only physically honest choices. Model 3 is a **dead end for crack realism** (the ≤2× cap guarantees the near-sonic pile-up can never be reproduced). Models 1–2 are acceptable only as *analysis/display* formulas or for slow, far-from-sonic motion layers.

### A.4.2 Distributed whip model: how many segments?

The task asks for a conceptual test of N = 8 / 16 / 32 / 64 / 128. Analysis from the established numbers [INFERENCE throughout — to be confirmed by the offline prototype]:

- **What the segments must reproduce:** (a) the macroscopic Doppler sweep during the whoosh (loop travel ~0.1–0.3 s — trivially captured by any N); (b) the **~1.2 ms supersonic window** during which the emission point sweeps at up to ~2·c (≈0.4–0.8 m of the whip); (c) smooth transition into/out of the supersonic regime.
- **Timing resolution:** uniform segmentation of a 2 m whip gives inter-segment propagation-delay steps Δt ≈ (2/N)/c = 5.83/N ms → N=8: 0.73 ms; N=16: 0.37 ms; N=32: 0.18 ms; N=64: 0.09 ms; N=128: 0.046 ms.
  - N=8 cannot even resolve the 1.2 ms supersonic window (≈2 emission steps) → **insufficient**.
  - N=16 gives ~3 steps inside the supersonic window → crude crack shaping.
  - N=32 → ~6–7 steps; N=64 → ~13; N=128 → ~26 steps.
- **Interference criterion:** segment spacing must be small vs. the wavelengths of the radiated content that we care about coherently (whoosh energy ≤ ~10 kHz → λ ≥ 3.4 cm). Uniform 2 m/N: N=64 → 3.1 cm (adequate); N=32 → 6.25 cm (marginal for the highest whoosh content but acceptable since the terminal segment region dominates crack radiation).
- **Recommended structure:** **hierarchical segmentation** — fine near the tip (terminal ~30–40 cm at ~1 cm spacing: 30–40 segments), coarse along the body (10–20 segments). Total N ≈ **48–64**. This matches the physical fact that the radiatively important region during the crack is the terminal supersonic portion, while the body mainly contributes low-level whoosh.
- **Expected verdict:** 8 ✗, 16 ✗ (marginal), 32 ✓ (minimum acceptable), 64 ✓✓ (recommended), 128 ✓ (diminishing returns but affordable). **[INFERENCE — the offline prototype must verify against a continuous-trajectory reference.]**

**Per-segment state (recommendation):** position rᵢ(t), velocity vᵢ(t) (→ local Mach Mᵢ), radius/linear mass density mᵢ′ (taper), emission weight wᵢ (radiation efficiency — see A.5 hypotheses), and per-segment retarded time τᵢ. Acceleration per segment is useful for aeroacoustic "whoosh" loudness arguments but not required in v1. Energy per segment is implicit in the motion model (M3), not tracked per segment.

### A.4.3 Simplified real-time model (the recommended v1 architecture sketch)

```
[Parameter layer: whip geometry, taper, energy, listener]
        │
[Motion model M3/M4: loop ODE or trajectory playback]
        │  r_i(t), v_i(t)  for N = 48–64 hierarchical segments
        ▼
[Per-segment retarded-time solver: 3–5 fixed-point iterations]
        │  τ_i(t), Doppler factor 1/(1−M_{r,i}), 1/R_i spreading, w_i directivity
        ▼
[Propagation delay-line bank: N fractional-delay reads of the source bus]
        │  (+ soft saturation of the 1/(1−M_r) gain: "crack onset" cue)
        ▼
[Summation + far-field 1/(1−M_r) pile-up envelope]
        │
        ├── (Model A) output as-is
        └── (Model B) + [Shock stage: Friedlander/N-wave event scheduler,
                         triggered by segment Mach-1 crossings, amplitude ∝ pile-up,
                         direction/geometry from head-wave model, rise time 5–50 µs,
                         duration 0.5–3 ms]
        ▼
[Output stage: calibration limiter, distance gain]
```

CPU budget estimate [INFERENCE]: N=64 segments × 5 iterations × 1 interpolation read each = ~320 fractional reads/sample ≈ well under 5% of one core at 48 kHz in optimized C++; the shock stage is event-based and negligible. **Real-time feasibility: comfortable.**

### A.4.4 Shock-generation candidates

| Candidate | Form | Pros | Cons | Verdict |
|---|---|---|---|---|
| **Analytic N-wave** | p(t) = +P rise (τ_r) → linear decay to −P → return; duration T | Canonical far-field form; parameters map to physical scale | Purely parametric (no derivation of τ_r) | **Prototype (primary)** |
| **Friedlander pulse** | p(t) = P₊(1 − t/T₊)e^(−b t/T₊) | Standard blast model, well-cited, single-sided, easy to drive from energy | Single-sided (cracks are closer to N-shaped ballistic shocks) | **Prototype (secondary/comparator)** |
| **Pile-up-limit of the Doppler stage** | Let 1/(1−M_r) saturation produce the transient | Single-mechanism elegance; the "crack = Doppler limit" hypothesis | Linear-model pile-up cannot synthesize µs rise times by itself; breaks at M=1 | **Tested in the critical experiment (Model A)** |
| **Nonlinear propagation mini-solver** | Burgers/lossy-wave reduced model | Derives rise time from physics | Cost + complexity; unnecessary for v1 | **Defer** |

**Is a separate explicit shock synthesis stage required?** Physics says: *probably yes* — the linear retarded-time model produces the correct *timing and envelope pile-up* but a µs-scale rise-time transient is a nonlinear-propagation phenomenon that a linear delay-line sum cannot synthesize faithfully, and the linear model structurally breaks at M=1. **[INFERENCE]** — but this is precisely hypothesis **H1/H2** below, designed to be falsified by the Model A vs Model B experiment (A.6.1). The architecture should couple them: the shock stage's trigger and amplitude come from the *same* 1/(1−M_r) pile-up factor, not an arbitrary envelope. **[INFERENCE]**

## A.5 Unproven hypotheses (must be tested, not assumed)

- **H1 (Model A sufficiency):** *Doppler/propagation alone — with exact retarded-time physics, N=64 segments, and honest 1/(1−M_r) saturation — produces a convincing crack.* Status: physics argues against (linear model, no µs rise mechanism) but it has **not been tested**. This is the single most important open question of Track A.
- **H2 (shock coupling):** *driving the explicit shock stage's amplitude/trigger from the Doppler pile-up factor 1/(1−M_r) yields natural, physically-consistent crack timing and loudness* (rather than hand-placed envelopes).
- **H3 (emission weighting):** *per-segment radiation efficiency wᵢ can be approximated from segment linear-mass/acceleration* (aeroacoustic analogy: accelerating compact bodies radiate). No literature was found for whip-body radiation weighting. **[UNPROVEN]**
- **H4 (whoosh generation):** *the pre-crack whoosh can be generated by the same propagation engine driven by turbulent-noise sources whose level/centre-frequency follow segment speeds* — no published procedural whoosh model was found. **[UNPROVEN]**
- **H5 (segment count):** N=48–64 hierarchical segmentation is sufficient to match a continuous-trajectory reference within perceptual tolerance. **[INFERENCE — needs the A/B test]**
- **H6 (loop constancy):** the Goriely–McMillen loop-speed result (loop ≈ constant speed, tip accelerates) holds in our reduced model — affects trajectory design; confirm from the PRL PDF. **[UNPROVEN]**

## A.6 Recommended experiments

### A.6.1 The critical experiment: Model A vs Model B (minimal offline prototype)

**Common input trajectory T_ref** (same for both models — this isolates the shock stage as the only variable):
1. Build T_ref from M4 (kinematic playback) with parameters locked to published numbers: 2 m whip, terminal supersonic window ≈ 1.2 ms, peak Mach ≈ 1.5–2, loop travel time ≈ 0.15–0.25 s.
2. Source signals on the whip bus: (a) synthetic whoosh noise (band-pass noise, speed-tracked centre frequency), (b) a recorded real whoosh (from the benchmark corpus, B.5), (c) periodic impulses (timing-resolution probe).

**Model A (propagation + Doppler only):**
- N=64 hierarchical segments; per-segment implicit retarded-time solver (fixed-point, 4 iterations);
- 1/R spreading + directivity weights + soft-saturated 1/(1−M_r) amplification;
- band-limited fractional-delay reads (windowed-sinc), summed;
- tip Mach **capped at 0.98** (Model A is linear-subsonic by construction — this cap is the honest operating envelope of the hypothesis).

**Model B (Model A + explicit shock stage):**
- Identical propagation stack, Mach uncapped (above M=1 the per-segment delay reads are faded out and replaced by the event scheduler — the hybrid hand-over);
- Shock stage: on each segment's Mach-1 crossing, schedule a Friedlander/N-wave event with amplitude ∝ the pile-up factor at crossing, rise time 5–50 µs (parameter sweep), positive duration 0.5–3 ms (sweep), direction from head-wave geometry.

**Comparison protocol (same T_ref for both):**
- Objective: waveform envelope, peak level, rise time (10–90%), spectral centroid, HF energy > 8 kHz, crest factor, decay shape — each compared against **real whip-crack recordings** made for this project (A.6.3).
- Subjective: small-panel A/B/X against real crack recordings; forced-choice "is this a real whip?" paradigm; report recognition rates.
- **Decision rule:** if Model A reaches listening-test parity with Model B (within confidence intervals), H1 stands and the shock stage is dropped. Physics predicts it will not. **Do not pre-commit to either outcome.**

**Implementation:** offline Python/NumPy or a small C++ offline renderer — no real-time constraints, no GUI, ~1–2 days of work each. Both models share the trajectory and propagation code (only the shock stage differs).

### A.6.2 Additional recommended experiments (ordered)

1. **Segment-count sweep:** re-render Model B at N = 8/16/32/64/128 (uniform) + hierarchical-64, compare against a continuous-trajectory (N=512) reference; metrics: arrival-time error, envelope error, perceptual MUSHRA-style rating. Resolves H5.
2. **Rise-time sweep for the shock stage:** 5/10/20/50/100 µs — measure perceived "sharpness" against real cracks (resolves the unmeasured spectral question).
3. **Doppler-core validation:** compare implicit retarded-time vs naive receiver-time delay output for a subsonic fly-by — confirms the ≤2× cap of the naive form and justifies the solver cost (A.4.1).
4. **Motion-model validation:** M3/M4 trajectory kinematics vs digitized published kinematics (Krehl 1998; PRL 2002 figures) — validates the motion layer before any audio is generated.
5. **Whoosh experiment (H4):** speed-tracked noise source through the propagation engine — naturalness rating vs recorded whooshes.

### A.6.3 Required measurements (on real whips — none of these exist in retrievable literature)

1. **Crack waveform at calibrated distance/angle:** 1 m and 3 m, on-axis and 90°: peak SPL, rise time, positive duration, full waveform capture (≥500 kHz sample rate for rise-time fidelity).
2. **Spectrum** of the crack (octave-band and high-resolution): settles the [UNPROVEN] spectral-content question.
3. **Whoosh** recordings at multiple swing speeds: spectral envelope vs speed (drives H4).
4. **Variability:** ≥ 10 cracks from the same whip — establishes the natural parameter spread for the shock stage.
5. **(Optional, if equipment allows) Schlieren or high-speed video** of a cracker to re-confirm emission location — otherwise rely on Krehl et al.

## A.7 Known failure modes and possible dead ends

| Failure mode / dead end | Why it fails | Mitigation / verdict |
|---|---|---|
| **Naive receiver-time delay Doppler near Mach 1** | Compression capped ≤ 2× (derived in A.1.4) — the pile-up that *is* the crack never appears | **DEAD END for crack realism.** Keep only for gentle motion layers |
| **Classical Doppler formula as a per-sample operator** | It is a frequency formula, not a waveform operator; no transient/event compression; singular | **DEAD END as core** (fine for UI readouts/analysis) |
| **Full nonlinear PDE in the real-time path** | Stiff, unnecessary; published simulations use implicit offline machinery | **Dead end for RT** — keep offline as ground truth (M1) |
| **Unbounded 1/(1−M_r) gain** | Caustic-like blow-up clips harshly; numerical instability of the fixed-point solver near M=1 | Soft saturation + gain-limiting + solver iteration cap; the saturation doubles as the crack-onset cue |
| **Multi-root handling inside a per-sample loop (supersonic)** | Root multiplicity switches discontinuously → clicks; expensive search | **Avoid**: hybrid hand-over to the event-based shock stage at M=1 (A.4.3) |
| **Coherent summation comb filtering across segments** | N near-identical delayed copies → comb coloration if segments are too close & coherent | Physical directivity/weighting decorrelates; keep spacing/wᵢ design; verify in experiment 1 |
| **Aliasing from fast delay modulation** | Fast D′(t) modulations alias (non-bandlimited fractional reads) | Windowed-sinc interpolation; local oversampling during fast events |
| **Zipper/step artefacts on parameter or ratio changes** | Block-quantized ratio updates | Drive the delay derivative continuously per-sample; one-pole smoothing of parameter jumps |
| **Wrong shock parameters** | Rise time too slow → "thud"; duration too long → "boom"; both destroy realism | Parameter sweeps vs measured cracks (A.6.2–2, A.6.3) |
| **Pitch-shifter-style processing for the crack** | Treating the crack as something to "pitch shift" inverts the causality (the crack is generated, not transformed) | The crack belongs to the propagation/shock engine, not the PitchProcessor lane (see B.3) |
| **"Doppler-like is Doppler-correct" fallacy** | Existing plugins that sound right are (verified) naive-delay implementations | Explicitly rejected by this research; correctness standard = retarded-time formulation |

## A.8 Classification summary (Track A)

| Approach | Classification |
|---|---|
| Implicit retarded-time Doppler (per-segment, saturated) | **Viable — prototype** (core engine) |
| Distributed N=48–64 hierarchical segment model | **Viable — prototype** |
| Reduced-order motion (M3 loop-energy / M4 kinematic) | **Viable — prototype** |
| Analytic N-wave / Friedlander shock stage (pile-up-coupled) | **Viable — prototype (Model B)** |
| Model A (Doppler-only crack) | **Hypothesis — test in the critical experiment; physics predicts insufficiency** |
| Naive receiver-time delay Doppler | **Unsuitable near Mach 1** (≤2× cap) — dead end for crack realism |
| Classical/per-frame Doppler formulas as core | **Unsuitable** — analysis only |
| Full nonlinear rod simulation in real time | **Unsuitable for RT** — offline ground truth only |
| Nonlinear-propagation mini-solver for rise time | **Deferred** — parameterize first, derive later if needed |
| Neural shock generation | **Not pursued** (no data, no need) |
| Middleware-style clamped Doppler | **Not applicable** (we are the middleware) |

## A.9 Primary references (Track A)

1. Carrière, Z. (1927). *J. Phys. et le Radium* 8(9), 365–384. (chronophotographic whip study; via secondary analysis)
2. Bernstein, B., Hall, D. A., & Trent, H. M. (1958). "On the Dynamics of a Bull Whip." *J. Acoust. Soc. Am.* 30(12), 1112–1115. https://pubs.aip.org
3. Krehl, P., Engemann, S., & Schwenkel, D. (1998). "The puzzle of whip cracking — uncovered by a correlation of whip-tip kinematics with shock wave emission." *Shock Waves* 8(1), 1–12.
4. Goriely, A., & McMillen, T. (2002). "Shape of a Cracking Whip." *Phys. Rev. Lett.* 88(24), 244301. https://link.aps.org/doi/10.1103/PhysRevLett.88.244301 (free PDF at goriely.com)
5. McMillen, T., & Goriely, A. (2003). "Whip waves." *Physica D* 184, 192–225. DOI 10.1016/S0167-2789(03)00221-5
6. Beck, S. D., et al. (2011). "Variations in recorded acoustic gunshot waveforms" (Friedlander blast model). *J. Acoust. Soc. Am.* https://pubs.aip.org
7. Leatherwood, J. D. (1993) and related NASA sonic-boom literature (N-wave parameters). https://ntrs.nasa.gov
8. Sparrow, V. W. (2010). Far-field N-wave amplitudes. https://ascent.aero
9. J. O. Smith (2010). *Physical Audio Signal Processing*. W3K — Doppler simulation chapter (interpolating delay-line Doppler).
10. Krehl, P. (2001). *History of Shock Waves, Explosions and Impact*. Springer. (historical framing: Lummer 1905, Salcher & Whitehead 1889)
11. Kaltenbacher, M. et al. (2025). Lossy nonlinear wave equations / shock steepening review. arXiv:2502.08194


---

# PART II — SECTION B: MULTI-ALGORITHM PITCH ENGINE

## B.0 Scope and method

Section B determines which *fundamentally different* pitch-shifting approaches should be implemented or prototyped in the future plugin. Every algorithm is evaluated against a fixed attribute grid, with a **special focus on rapidly changing pitch curves** (the Doppler engine will continuously modulate pitch). Claim tags as defined at the top of this report. Quality/latency/CPU numbers come from the research digests (primary sources read during research: Moulines & Charpentier 1990 full PDF; Laroche & Dolson 1999 full PDF; Haghparast/Penttinen/Välimäki DAFx-07 full PDF; Schörkhuber DAFx-12 abstract; Průša & Holighaus 2022; Stylianou 2001; SoundTouch source+COPYING; Rubber Band headers+source; Signalsmith Stretch code+LICENSE; pbshift repo; WORLD repo; audiojs/shift benchmark README).

**Master comparison table** (compact; full detail per algorithm in B.1):

| # | Algorithm | Family | Latency | CPU | Dynamic-curve control rate | Verdict for Doppler-driven engine |
|---|---|---|---|---|---|---|
| 1 | Varispeed / SRC | resampling | < 1 ms | trivial | **per-sample** | **Build (shared core + character mode)** |
| 2 | Variable-delay pitch shift | delay modulation | = max delay excursion | lowest | **per-sample** | **Build (the physical Doppler core)** |
| 3 | OLA + resampling | time-segment | ~20–100 ms | lowest | per hop/sequence | Benchmark baseline only |
| 4 | WSOLA + resampling | time-segment | ~40–100 ms | very low | per sequence (40–90 ms) | Optional slow/static mode |
| 5 | TD-PSOLA | pitch-synchronous | 10–40 ms (analysis) | very low | per period | Benchmark (voice-only) |
| 6 | FD-PSOLA | hybrid | 10–40 ms | low | per period | Skip |
| 7 | Classic phase vocoder | spectral | ~window+hop (23–93 ms+) | moderate | per hop (5.8–23 ms) | **Prototype (baseline)** |
| 8 | Phase-locked PV | spectral | same | moderate | per hop | **Prototype** |
| 9 | Identity/peak phase locking | spectral (technique) | — | negligible add-on | per hop | **Implement (must-have inside 7/8/10)** |
| 10 | Transient-aware PV | spectral | same | moderate | per hop + reset events | **Prototype** |
| 11 | Granular pitch shifting | time-segment | grain (20–200 ms) | low | per grain | Creative mode only |
| 12 | Sinusoidal modeling (MQ) | parametric | ~window+frame | high | per frame (5–10 ms) | Benchmark |
| 13 | Sinusoidal+residual (SMS) | parametric | ~window+frame | high | per frame | Benchmark |
| 14 | Harmonic modeling (HNM) | parametric | 1–2 frames | moderate | continuous θ(t) (design!) | Skip (speech-only) — steal ideas |
| 15 | LPC / source-filter | parametric | 10–20 ms frames | very low | per frame | Skip (voice niche) |
| 16 | WORLD vocoder | vocoder | analysis frames | low-moderate | per frame | Skip (speech-only) |
| 17 | Signalsmith Stretch | external spectral | ≈100–120 ms | modest | per block | **External integration (MIT)** |
| 18 | Rubber Band R2 | external spectral | moderate | low | real-time safe | **External benchmark (GPL/commercial)** |
| 19 | Rubber Band R3 | external spectral | ≥ 50 ms (Live) | high | real-time safe | **External benchmark (GPL/commercial)** |
| 20 | pbshift | external hybrid | 46–128 ms | moderate | per block | Watch (MIT, pre-1.0) |
| 21 | Neural / DDSP | neural-hybrid | frame+inference | high/GPU | frame-rate | **Do not pursue (v1)** |

---

## B.1 Algorithm catalogue (full attribute grid)

### B.1.1 Varispeed / sample-rate conversion

- **Core principle:** y[n] = x[r·n] via a fractional (windowed-sinc) interpolator — the read pointer advances at rate r. All frequencies scale by r; duration scales by 1/r; harmonic ratios preserved. **[VERIFIED-SOURCE]**
- **Pitch range:** unbounded (r > 0).
- **Latency:** interpolator width only (< 1 ms). **[INFERENCE]**
- **CPU:** trivial (8–32-tap sinc per sample).
- **Real-time suitability:** as a *standalone streaming effect*, varispeed drifts (output rate ≠ input rate) — the W3C Web Audio spec explicitly excludes real-time varispeed for this reason. **[VERIFIED-SOURCE]** In a Doppler engine the drift is *physically correct* and is absorbed by the variable-delay/retarded-time machinery (the delay excursion is exactly the accumulated drift).
- **Dynamic pitch modulation:** **best possible** — r is the read-pointer increment, evaluated per-sample; no analysis, no block quantization. Zipper-free by construction when r is continuous. **[VERIFIED-SOURCE + INFERENCE]**
- **Material behaviour:** transients: pristine (waveform compressed, not reconstructed). Harmonic: pristine ratios. Polyphonic: pristine. Noise: natural (spectral scaling). Voice: characteristic but formants shift with pitch ("chipmunk"/"growl").
- **Formant behaviour:** shifts 1:1 with r (no preservation).
- **Stereo/phase coherence:** per-channel identical resampling → **perfect waveform & inter-channel coherence** (deterministic map). Modulation rates also scale with r (a 5 Hz tremolo becomes 7.5 Hz at r=1.5 — "correct sampler behaviour"; measured phase-coherence 0.170 in the audiojs benchmark *because* of rate scaling, which is physics, not an artifact). **[VERIFIED-SOURCE — audiojs/shift]**
- **Typical artifacts:** aliasing if the interpolator is not band-limited (use windowed sinc); spectral/formant scaling.
- **Strengths/weaknesses:** zero-latency, artifact-free, trivially dynamic / changes duration, shifts formants.
- **Implementation complexity:** very low (resampler core).
- **Licensing/dependencies:** none (own code). Reference: audiojs/shift (MIT, JS, reference only).
- **Verdict for our project:** **Build** — as the shared resampler core for the whole engine and as a "varispeed character" mode. Worth implementing from scratch (trivial, no exposure).

### B.1.2 Variable-delay pitch shifting (Doppler-style)

- **Core principle:** y(t) = x(t − D(t)); instantaneous pitch ratio = 1 − D′(t). This is the exact audio-side form of physical Doppler (J.O. Smith). Sustained constant ratio requires periodic delay re-initialization (pointer wrap) with crossfades. **[VERIFIED-SOURCE — J.O. Smith, PASP]**
- **Pitch range:** transiently ±octaves; *sustained* shifts are limited by the delay buffer excursion: excursion = ∫|1 − ratio|dt, so sustained |r−1| ≠ 0 forces wrap-arounds. The two-tap harmonizer (B.1.11-family) solves this on the same infrastructure.
- **Latency:** the maximum |D(t)| excursion + interpolator width (a delay line, not an algorithmic latency).
- **CPU:** one fractional-delay read per sample — cheapest of all families.
- **Real-time suitability:** excellent; proven in Doppler simulators and Leslie emulation. **[VERIFIED-SOURCE]**
- **Dynamic pitch modulation:** **best of every family** — the ratio IS the delay derivative; per-sample; zero analysis; a pitch-curve LFO modulates D(t) smoothly with no zipper and no block-rate phase updates. **[VERIFIED-SOURCE + INFERENCE]**. Exact Mach-1 behaviour requires the implicit retarded-time form (A.1.4) — this is the Track-A coupling point.
- **Material behaviour:** transients: preserved (compressed waveform, not reconstructed). Harmonic/polyphonic/noise: clean. Voice: natural Doppler realism (it *is* the physical model).
- **Formant behaviour:** full-spectrum shift (formants move with ratio — physically correct for Doppler! Formant motion is *wrong* for musical pitch shifting).
- **Stereo/phase coherence:** identical per-channel processing → perfect coherence; for physical sources, per-segment summation is coherent by construction (same source bus).
- **Typical artifacts:** wrap-splice clicks without crossfading; HF loss with linear interpolation (use sinc); amplitude modulation at the crossfade rate; ratio cap ≤ 2 for the naive receiver-time form (A.1.4 — critical).
- **Strengths/weaknesses:** physical, cheapest, fastest dynamic response / sustained-shift excursion limit; formant shift.
- **Implementation complexity:** low (delay bank + sinc interpolator + retarded-time solver for exactness).
- **Licensing/dependencies:** none — all open-source Doppler plugins found are GPL and simple enough that from-scratch is both cleaner and license-safe. **[VERIFIED-LICENSE-FILE ×3]**
- **Verdict:** **Build — this is the core engine for the physical Doppler lane.**

### B.1.3 OLA + resampling

- **Core principle:** fixed-hop overlap-add time-stretch with analysis/synthesis hops in ratio, then resample the stretched signal by the ratio (DAFx-book reference: Sa=256, N=2048 Hann). **[VERIFIED-SOURCE — DAFx book ch. 6 + M-files]**
- **Pitch range:** wide (bounded by windowing practicality).
- **Latency:** ~window length: 20–100 ms. **[INFERENCE from parameters]**
- **CPU:** lowest of the OLA family.
- **Dynamic pitch modulation:** ratio updates at hop/sequence rate → stepping/lag on fast curves. **[INFERENCE]**
- **Material behaviour:** poor on everything phase-critical: grains land at arbitrary phase → destructive interference (audiojs: f0 error 38.33 Hz, **worst of 15 algorithms**); chorus/phasiness; AM at grain rate. Voice: mediocre. Noise: acceptable (statistically similar). **[VERIFIED-SOURCE — audiojs/shift]**
- **Formant behaviour:** shifts with ratio (stretch+resample).
- **Stereo/phase coherence:** decorrelated by random grain alignment unless per-channel synchronized.
- **Typical artifacts:** phasiness, AM, comb-ish coloration.
- **Implementation complexity:** very low.
- **Licensing/dependencies:** DAFx MATLAB code is **educational-only, no commercial use without permission** — reimplement from the book's math. **[VERIFIED-SOURCE — file headers]**
- **Verdict:** **Benchmark baseline only** (the "worst-case" reference point for the benchmark suite). Do not ship.

### B.1.4 WSOLA + resampling

- **Core principle:** OLA plus a per-grain similarity search: each grain is placed within a ±tolerance window maximizing cross-correlation with the previous grain's tail, eliminating phase cancellation. (Verhelst & Roelands, ICASSP 1993.) **[VERIFIED-PAPER]**
- **Pitch range:** wide (0.5–2.0 typical; SoundTouch AUTO maps tempo 0.5→2.0).
- **Latency:** SoundTouch documents **~100 ms** for time-stretching (~40 ms achievable with aggressive settings: 30/20/10 ms — PCSX2 configs). **[VERIFIED-SOURCE — SoundTouch README + docs]**
- **CPU:** real-time on a Pentium-133 with quick-seek (~90% of optimal matches found). **[VERIFIED-SOURCE]**
- **Real-time suitability:** excellent (proven in production for decades).
- **Dynamic pitch modulation:** parameters changeable at runtime, but applied **per sequence (40–90 ms)** — sluggish against Doppler curve rates. **[INFERENCE from structure + DAFx-07]**
- **Material behaviour:** speech/mono: good (audiojs attack correlation 0.995, f0 error 1.67 Hz); "echoing" artifact when slowing (SoundTouch README); dense polyphony: acceptable; transients: moderate smearing (search window); noise: fine. **[VERIFIED-SOURCE]**
- **Formant behaviour:** shifts with ratio.
- **Stereo/phase coherence:** per-channel independent search → possible decorrelation unless M/S or synchronized.
- **Typical artifacts:** echo/doubling on slow-down; formant shift; residual AM.
- **Implementation complexity:** low-moderate (correlation search).
- **Licensing/dependencies:** SoundTouch = **LGPL-2.1+** (author Olli Parviainen — note: the name "Ondrej Parfut" circulating in some secondary sources is wrong). WSOLA algorithm itself unencumbered (patents expired). **[VERIFIED-LICENSE-FILE]**
- **Verdict:** **Optional quality mode for slow/static ratios** (acceptable latency there); not the Doppler core. Implement from scratch or dynamically link SoundTouch (LGPL obligations; awkward for a statically-linked VST).

### B.1.5 TD-PSOLA

- **Core principle:** pitch-synchronous overlap-add on voiced signal: pitch marks every glottal period; 2-period Hanning grains re-emitted at synthesis marks spaced P/β. (Moulines & Charpentier, 1990.) **[VERIFIED-PAPER]**
- **Pitch range:** large for voice (β 0.5–2 well within spec; formal tests up to ±factor 2).
- **Latency:** grain = 2 periods (2.5–25 ms for 80–400 Hz) + F0/voicing analysis lookahead ≈ 10–40 ms total. **[INFERENCE]**
- **CPU:** "very efficient — real-time on an Intel 80386" (1990). **[VERIFIED-PAPER]**
- **Real-time suitability:** excellent.
- **Dynamic pitch modulation:** re-locks every period (2.5–10 ms granularity — fast), but requires continuous pitch-mark computation and an F0/voicing estimate whose errors are catastrophic; onset pitch-mark jitter degrades attacks (audiojs attack corr 0.941). **[VERIFIED-SOURCE + VERIFIED-PAPER]**
- **Material behaviour:** voice: **best-in-class** (formal listening test: all PSOLA variants ≫ LPC); polyphonic: breaks (single-F0 assumption; "destroys polyphony/unvoiced" — audiojs); transients: attack jitter; noise: tonal artifacts when unvoiced segments are repeated (documented in the paper). **[VERIFIED-PAPER]**
- **Formant behaviour:** window-length-dependent: wide-band (L<2P) windows → formant bandwidth broadening; narrow-band (L>4P) → selective harmonic attenuation → "reverberant-sounding distortion." Independent formant control requires FD-PSOLA. **[VERIFIED-PAPER]**
- **Stereo/phase coherence:** phase coherence 0.998 (audiojs) per channel; stereo requires synchronized marks. **[VERIFIED-SOURCE]**
- **Typical artifacts:** reverberant distortion (NB windows), bandwidth broadening (WB windows), tonal noise on unvoiced, attack jitter.
- **Implementation complexity:** moderate (pitch marks + F0).
- **Licensing/dependencies:** open-source references are weak: sannawag/TD-PSOLA (Python, MIT, research-grade: hardcoded singing-voice params, octave errors, quality degrades beyond ~700 cents); maxrmorrison/psola (GPL-3.0, wraps Praat → **Praat GPL contamination** if ported). No production-grade open C++ TD-PSOLA exists. **[VERIFIED-LICENSE-FILE ×2]**
- **Verdict:** **Benchmark-only** (voice material), unless the engine ever guarantees solo-voice input. Implement from the 1990 paper (clean) if needed.

### B.1.6 FD-PSOLA

- **Core principle:** FFT of each 2-period grain before overlap-add synthesis, enabling explicit spectral-envelope/formant manipulation (independent pitch factor β and formant factor γ — DAFx `psolaF.m`). **[VERIFIED-PAPER + VERIFIED-SOURCE]**
- **Pitch range/latency/CPU:** same as TD-PSOLA plus per-grain FFT (~5 MFLOPS in 1990 terms — still cheap). **[VERIFIED-PAPER]**
- **Dynamic pitch modulation:** same per-period re-lock and F0 dependence.
- **Material behaviour:** listening-test-equal to TD-PSOLA; adds formant control. **[VERIFIED-PAPER]**
- **Formant behaviour:** explicit control (γ) — its raison d'être.
- **Verdict:** **Skip** — it is really the frequency-domain lane; the L-D '99 engine (B.1.8/9/10) supersedes it with better dynamic behaviour and no F0 dependence.

### B.1.7 Classic phase vocoder

- **Core principle:** STFT analysis; per-bin phase propagation with instantaneous-frequency estimation; pitch shift = time-stretch by β + resample by 1/β (order matters for CPU: resample-then-stretch is cheaper for upward shifts). Key equations:
  Δφ_k(n) = princarg[φ_k(n) − φ_k(n−1) − 2π·k·H/N]; ω̂_k(n) = 2πk/N + Δφ_k(n)/H; synthesis: φ′_k(n) = φ′_k(n−1) + α·ω̂_k(n)·H_s. (Flanagan & Golden 1966; Portnoff 1981 lineage.) **[VERIFIED-PAPER]**
- **Pitch range:** wide (±octaves).
- **Latency:** ≈ window + buffering: 1024 → ~23 ms, 4096 → ~93 ms + crossfade tail (eval settings seen: 4092-sample Hann, 8192 bins, 1024 hop @ 44.1 kHz). **[VERIFIED-SOURCE + VERIFIED-PAPER]**
- **CPU:** moderate: 2 FFTs/hop/channel + phase math; 75% overlap doubles FFT count vs 50%. **[VERIFIED-PAPER]** A few % of one modern core at 2048/75% stereo. **[INFERENCE]**
- **Real-time suitability:** good (standard in production plugins).
- **Dynamic pitch modulation:** **hop-rate-limited** (5.8–23 ms updates at typical hops); the phase-propagation model "is only correct when the input signal can be modelled as a sum of a small number of **slowly varying** sinusoids" (DAFx-12) — a Doppler curve that moves appreciably within a window causes phase-incoherent frames → warble/detuning between hops. Fast FM *input* is a known PV weakness. **[VERIFIED-PAPER]** Ratio can be applied per-hop but coherence degrades — **marginal for our use case.**
- **Material behaviour:** best: sustained harmonic/polyphonic tones. Worst: transients (smearing), broadband percussion, heavy vibrato/FM, speech (glottal pulse shape changes). **[VERIFIED-PAPER]**
- **Formant behaviour:** none — the whole spectrum scales ("Mickey Mouse"). **[VERIFIED-SOURCE]**
- **Stereo/phase coherence:** independent per-channel phase propagation decorrelates channels (image wobble) — mitigate with shared spectral decisions (see B.1.10 and B.3). **[INFERENCE]**
- **Typical artifacts:** phasiness (loss of vertical/intra-frame phase coherence), transient smearing, echo-ish tail. **[VERIFIED-PAPER — Průša & Holighaus 2022]**
- **Implementation complexity:** moderate (well-documented; many references).
- **Licensing/dependencies:** own code + permissive FFT (KISS FFT/pocketfft/PFFFT — all clean; **FFTW is GPL — avoid**). **[VERIFIED-LICENSE-FILE]**
- **Verdict:** **Prototype (mandatory baseline)** for the spectral lane; expect phasiness; hop-rate ratio updates only.

### B.1.8 Phase-locked phase vocoder (architecture)

- **Core principle:** after inter-frame phase propagation, enforce intra-frame (vertical) coherence by locking bins in each peak's region of influence to the peak's phase behaviour. (Laroche & Dolson 1997/1999; Puckette 1995 origin.) **[VERIFIED-PAPER]**
- Two locking variants (details in B.1.9): identity and peak/scaled.
- **Latency/CPU:** same as classic PV; locking is a negligible add-on; the phase-locked PV is the variant that tolerates 75% overlap where standard PV needs it. **[VERIFIED-PAPER]**
- **Dynamic pitch modulation:** same hop-rate limits as B.1.7, but the L-D '99 frequency-domain shift variant (below) *explicitly supports per-frame varying shift* — the cumulated rotation Z_{u+1} = Z_u·e^{jΔω_{u+1}·R} is defined with frame-varying Δω ("the notation ω_{u+1} indicates that the amount of frequency shift may vary from one frame to the next"). **[VERIFIED-PAPER]** This makes the L-D peak-shift pipeline the most dynamic-friendly spectral method found.
- **Material behaviour:** dramatically reduces phasiness; mildly improves transients; still "fails for percussive sounds and transients in general." **[VERIFIED-PAPER + VERIFIED-SOURCE]**
- **Verdict:** **Implement** (the locking technique is a must-have inside any PV we build).

### B.1.9 Identity vs peak-based phase locking (the technique itself)

- **Core principle:**
  - **Identity locking:** all bins in the region of influence receive exactly the peak's synthesis phase: φ′_l = φ′_peak.
  - **Peak/scaled locking:** bins preserve their *analysis* phase deviation around the peak: φ′_l = φ′_peak + (φ_l − φ_peak).
  Region of influence: midpoints between peaks (or lowest-magnitude bin); peak = magnitude greater than both neighbours each side. **[VERIFIED-PAPER — L-D '99; Průša 2022 description]**
- **Cost:** negligible (peak picking + region assignment).
- **Effect:** cures phasiness (restores vertical coherence), reduces transient smearing. Identity locking is what L-D '99's peak-shift technique implicitly implements. **[VERIFIED-PAPER + VERIFIED-SOURCE]**
- **Verdict:** **Implement from scratch** (trivial, high value, unencumbered — 1999 patents expired).

### B.1.10 Transient-aware phase vocoder

- **Core principle:** detect onsets; re-initialize phases (leave analysis phases unmodified) at the transient-aligned frame position, killing the smearing caused by propagating phases across an attack. (Röbel 2003, DAFx — "A new approach to transient processing in the phase vocoder.") **[VERIFIED-PAPER]**
- Production precedents: Rubber Band R2 `OptionTransientsCrisp` ("reset component phases at the peak of each transient"; optional band-limited reset 150 Hz–1 kHz); zplane élastique advertises tonal/transient component detection. **[VERIFIED-SOURCE — header docs + vendor pages]**
- **Latency/CPU:** same as the host PV + a transient detector (cheap).
- **Dynamic pitch modulation:** reset boundaries interact badly with a simultaneously changing ratio (restart of phase accumulation mid-glide) — **a specific risk for our Doppler use case**; must be benchmarked. **[INFERENCE]**
- **Material behaviour:** transients: much improved; caveat: resets "may cause interruptions in stable sounds present at the same time as transient events" (Rubber Band header). **[VERIFIED-SOURCE]**
- **Verdict:** **Prototype** — combined with L-D '99 pipeline; detection robustness under Doppler FM is a benchmark item.

### B.1.11 Granular pitch shifting

- **Core principle:** constant-rate output grains (Hann/triangular, 4:1 overlap) read from the input at speed r (SuperCollider PitchShift: triangular grains, 4:1 overlap, window 0.02–0.2 s; GRM Pitch lineage). **[VERIFIED-SOURCE]**
- **Pitch range:** 0–4 (SC).
- **Latency:** = grain size (20–200 ms).
- **CPU:** low.
- **Dynamic pitch modulation:** ratio quantized per grain/hop; window not modulatable in SC. **[VERIFIED-SOURCE]**
- **Material behaviour:** textural by design: grain-rate AM, comb filtering from uniform grain placement (SC provides `timeDispersion` to alleviate), chord "crumble" with small grains (audiojs: worst formant distance 3.486 of 15 algorithms; the 398-sample default is deliberately textural). **[VERIFIED-SOURCE]**
- **Formant behaviour:** shifts with ratio.
- **Stereo/phase coherence:** decorrelated unless synchronized.
- **Verdict:** **Creative/textural mode only** — cheap to add once the delay engine exists; not a quality engine. Eventide "Harmonizer"-style dual-tap shifting (the musical version of this family) is covered in B.1.2's two-tap extension: two read taps sweeping one delay line with complementary crossfades (window 1024–4096 → 23–93 ms), modulatable per-sample between wraps, characteristic flutter at crossfade rate = |r−1|/window. **[VERIFIED-SOURCE]** *Naming note: "Harmonizer" is an Eventide trademark — do not use in product naming.* **[INFERENCE]**

### B.1.12 Sinusoidal modeling (MQ — McAulay–Quatieri)

- **Core principle:** frame-wise peak picking, peak matching (birth/death by matching cost), per-partial quadratic-interpolated amplitude/frequency/phase; synthesis via oscillator bank or OLA frames. Pitch shift = scale each track's frequency (and optionally amplitude trajectory) by the ratio. **[VERIFIED-PAPER — McAulay & Quatieri 1986, IEEE Trans. ASSP 34(4)]**
- **Pitch range:** wide (parametric).
- **Latency:** ≈ window + 1 analysis frame (similar order to PV). **[INFERENCE]**
- **CPU:** high — O(P²) worst-case peak matching + oscillator-bank synthesis scaling with partial count; real-time feasible for modest partial counts. **[INFERENCE]**
- **Dynamic pitch modulation:** track frequencies update at analysis-frame rate (5–10 ms) and synthesis is parametric — per-frame ratio is naturally supported with *no phase-propagation inconsistency* (an advantage over PV); but fast FM corrupts IF estimates within the window and causes track birth/death churn. **[INFERENCE]**
- **Material behaviour:** monophonic quasi-harmonic: excellent; noise forced into sinusoids → "musical noise"/birdies; dense polyphony → track confusion. **[VERIFIED-SOURCE + INFERENCE]**
- **Formant behaviour:** separable in principle (amplitude trajectory scaling independent of frequency scaling).
- **Stereo/phase coherence:** per-channel analysis decorrelates unless tracks are matched jointly.
- **Implementation complexity:** high (tracking logic).
- **Licensing/dependencies:** own code; patents expired.
- **Verdict:** **Benchmark-only** (harmonic/voice material) — too fragile as the general engine.

### B.1.13 Sinusoidal + residual modeling (SMS — Serra & Smith)

- **Core principle:** deterministic sinusoids + stochastic residual (LPC-modeled or filtered noise with time-scaled magnitude envelope); pitch shift = scale sinusoid frequencies; residual time-scaled/resampled separately. **[VERIFIED-PAPER — Serra & Smith 1990, Computer Music J. 14(4)]**
- **Latency/CPU:** frame-rate + higher CPU (two models). **[INFERENCE]**
- **Dynamic pitch modulation:** same frame-rate limits as MQ; residual and harmonic parts can desynchronize under rapid ratio changes. **[INFERENCE]**
- **Material behaviour:** fixes MQ's musical-noise on noisy inputs; voice/harmonic: very good.
- **Formant behaviour:** separable (residual envelope independent).
- **Verdict:** **Benchmark-only.** The deterministic/residual split idea is worth borrowing for formant-preserving designs. **[VERIFIED-SOURCE]**

### B.1.14 Harmonic modeling (HNM — Stylianou)

- **Core principle:** voiced band 0–F_max = harmonics with smooth (AR/LSP) spectral model + phase parameters referenced to glottal-closure instants; above F_max = modulated AR-filtered noise. Pitch modification: recursive computation of synthesis time instants from the pitch-modification factor θ(t); interpolation of harmonic amplitudes/phases onto the new harmonic grid; **the harmonic part stays within 0–F_max before and after modification → formants preserved by construction.** **[VERIFIED-PAPER — Stylianou 2001, IEEE Trans. SAP 9(1)]**
- **Dynamic pitch modulation:** *designed for continuous pitch contours* — the synthesis-instant mapping integrates a continuous time-varying θ(t). This is the only classic parametric method whose pitch modification is *formally* defined for continuously varying ratios. **[VERIFIED-PAPER]**
- **Material behaviour:** speech/monophonic voice only — requires reliable F0 + voicing; fails on polyphony. **[VERIFIED-PAPER + INFERENCE]**
- **Formant behaviour:** preserved by construction.
- **Verdict:** **Skip for a general plugin** (speech-only) — but **steal the architecture**: "harmonic grid moves, envelope stays" is the cleanest published formulation of formant-preserving pitch modification, applicable to our spectral lane's envelope-correction stage.

### B.1.15 LPC / source-filter pitch shifting

- **Core principle:** slowly-varying all-pole vocal-tract filter + excitation; modify excitation pitch, keep filter → formants preserved by design. Cheap (order 10–16 Levinson per 10–20 ms frame). **[VERIFIED-SOURCE]**
- **Dynamic pitch modulation:** excitation period can change every frame with no phase-coherence machinery at all — among the most modulation-tolerant *classical* approaches, at the price of heavy timbral coloration. **[INFERENCE]**
- **Material behaviour:** voice: acceptable with classic "vocoded"/buzzy coloration; polyphony/percussion/ensembles: fails (single resonant-tract assumption). **[INFERENCE + VERIFIED-SOURCE]**
- **Formant behaviour:** excellent (it *is* the filter).
- **Verdict:** **Skip** (speech niche). Formant control via explicit envelope filtering (B.6) supersedes it.

### B.1.16 WORLD vocoder (Morise et al.)

- **Core principle:** F0 estimation (DIO fast / Harvest accurate), pitch-adaptive spectral envelope (CheapTrick), aperiodicity (D4C), synthesis (Platinum optional). Pitch modification = modify F0 contour, keep envelope → formant-preserved. **[VERIFIED-SOURCE — GitHub README]**
- **Latency/CPU:** DIO designed for speed (real-time capable; a sequential real-time waveform generator is documented — Morise, APSIPA ASC 2020); Harvest is the slower offline refinement. **[VERIFIED-SOURCE]**
- **Dynamic pitch modulation:** per-frame (F0 contour rate).
- **Material behaviour:** speech-grade only — F0-dependent analysis makes it monophonic-voice; not for instruments/mixes. **[INFERENCE from design]**
- **Formant behaviour:** preserved by construction (pitch-adaptive envelope).
- **Licensing:** **modified-BSD, "no patent in all algorithms."** **[VERIFIED-LICENSE-FILE]**
- **Verdict:** **Skip as an engine** (speech-only scope); CheapTrick's pitch-adaptive envelope estimation is a valuable reference for our formant-preservation stage.

### B.1.17 Signalsmith Stretch

- **Core principle:** header-only C++11 spectral pitch/time shifter — the ADC22 presentation "Four Ways To Write A Pitch-Shifter" final method: spectral processing with special phase/envelope handling, non-linear frequency map ("tonality limit"), custom frequency maps. **[VERIFIED-SOURCE]**
- **Pitch range:** octave-range pitch shifting; time-stretch best within 0.75×–1.5×. **[VERIFIED-SOURCE]**
- **Latency:** from code: `presetDefault` block 0.12·sr / hop 0.03·sr → **≈120 ms total at 48 kHz** (≈100 ms `presetCheaper`; +1 hop with `splitComputation`). **[VERIFIED-SOURCE-derived]**
- **CPU:** modest (~10× slower unoptimized; no official benchmarks). **[VERIFIED-SOURCE + INFERENCE]**
- **Dynamic pitch modulation:** per-block ratio control; quality reputation is high, but the spectral update rate is block/hop-bound (30 ms at default preset) — suitable as a slow-curve musical layer, not the fast Doppler lane. **[INFERENCE]**
- **Material behaviour:** widely praised quality for octave shifts on tonal material; transients handled well by its phase/envelope logic (reputation). **[VERIFIED-SOURCE]**
- **Formant behaviour:** frequency-map logic gives useful timbral control (not full independent formant preservation). **[INFERENCE]**
- **Stereo:** per-channel processing; envelope/phase logic deterministic — reputation for stable stereo. **[INFERENCE]**
- **Licensing:** **MIT** (both the main library and the dsp/ subtree; vendored signalsmith-linear also MIT). **[VERIFIED-LICENSE-FILE]** — *note: often misreported as LGPL; it is not.*
- **Users:** HISE, Qt Multimedia (vcpkg port), GStreamer plugin. **[VERIFIED-SOURCE-ish]**
- **Dependencies:** none (header-only).
- **Verdict:** **Integrate as an external high-quality mode (license-safe)** + benchmark reference. Not the fast-Doppler engine.

### B.1.18 Rubber Band R2

- **Core principle (official):** "block-based phase vocoder with phase resets on percussive transients, an adaptive stretch ratio between phase reset points, and a 'lamination' method to improve vertical phase coherence." Pitch shift = resample + stretch. Source confirms: L-D-style expected-phase propagation, region-of-influence "laminar" locking (maxdist=8, band limits), transient-triggered phase reset via compound percussive+HF+silence detection curves, band-limited reset (150 Hz–1 kHz mixed mode). **[VERIFIED-SOURCE — technical notes + source read]**
- **Pitch range:** wide (±octaves and beyond).
- **Latency:** moderate (standard R2 paths); `minLatency` option exists but JUCE-forum reports "potential tearing during modulation with a change of the pitch parameter." **[VERIFIED-SOURCE]**
- **CPU:** low (much lower than R3). **[VERIFIED-SOURCE]**
- **Dynamic pitch modulation:** ratio changes are real-time-safe, but quality under fast changes is exactly where tearing is reported → treat as slow-curve engine. **[VERIFIED-SOURCE]**
- **Material behaviour:** the industry-standard workhorse: good across mixes, vocals, soft onsets (R3 better); R2 transients via reset.
- **Formant behaviour:** optional formant-preservation option.
- **Licensing:** **GPL-2.0-or-later + commercial dual.** Commercial tiers: £590 (with attribution) / £1490 (small publisher, no attribution) / £9320 (no attribution) — perpetual. No App Store distribution under GPL. **[VERIFIED-LICENSE-FILE + VERIFIED-SOURCE]**
- **Dependencies:** none required (built-in FFT + resampler; optional FFTW/IPP/SLEEF/KissFFT).
- **Verdict:** **External benchmark only** (GPL contaminates a proprietary VST; commercial licence is a legitimate but non-free fallback if we ever want its quality in-product).

### B.1.19 Rubber Band R3 "Finer"

- **Core principle:** multi-resolution window scheme (code in `src/finer/`), transient-aware bin segmentation, independent formant shifting. Official: higher quality than R2 "for most material, especially complex mixes, vocals… soft onsets… substantial bass content. However, it uses much more CPU power than the R2 engine." **[VERIFIED-SOURCE]**
- **Latency:** `RubberBandLiveShifter` (R3-based, fixed-block, RT-safe): "still not a low-latency effect, with a delay of **50 ms or more**." `OptionWindowShort` disables multi-window logic → far lower CPU/delay. **[VERIFIED-SOURCE]**
- **CPU:** "much more" than R2; forum reports of needing high single-core performance for real-time (unverified forum claim). **[VERIFIED-SOURCE + UNVERIFIED]**
- **Dynamic pitch modulation:** ratio changes RT-safe; R3 specifically better at "smooth pitch changes" than R2 (soft onsets) — but still a block-based spectral engine; not a per-sample control path. **[VERIFIED-SOURCE]**
- **Licensing:** same GPL-2.0+ / commercial dual as R2. **[VERIFIED-LICENSE-FILE]**
- **Verdict:** **External benchmark only** — the quality ceiling for offline comparison renders (and the reference target if we ever buy the commercial licence).

### B.1.20 pbshift

- **Identity (positively identified during research):** pbTechLab's `pbshift` — "real-time deterministic pitch-shift / time-stretch C++17 library with multi-resolution and pitch-synchronous voice engines." **[VERIFIED-SOURCE — project page/repo]**
- **Core principle:** shared STFT front end (magnitude/phase/IF/group delay) + multi-resolution spectral engine with identity phase locking, transient "hold, fire, pin", energy-rising partial suppression, stereo phase copy; a separate pitch-synchronous time-domain Voice engine (YIN per hop, consistent-polarity pitch marks, ±0.9-period grain search, 3 kHz crossover, time-reversed grains to de-double highs); offline multi-resolution + WSOLA paths; Kaiser-windowed sinc resampler. **[VERIFIED-SOURCE — project description]**
- **Latency:** Live+Auto ≈ 64 ms; Live+Music/Rhythm ≈ 53.3 ms; StudioRT+Auto ≈ 128 ms; Voice ≈ 46 ms input latency. **[VERIFIED-SOURCE — self-reported API]**
- **CPU:** self-reported 40–77% of real-time budget (stereo 48 kHz, 256–1024-sample blocks). **[UNVERIFIED — self-reported]**
- **Dynamic pitch modulation:** multi-resolution + per-block ratio; determinism gates (bit-exact chunk-size independence) suggest careful control-path design, but it is still block-based. **[INFERENCE]**
- **Licensing:** **MIT** (Copyright pbtechlab). Only dependency: **pffft** (FFTPACK-derived BSD-like — clean). **[VERIFIED-LICENSE-FILE ×2]**
- **Status:** **pre-1.0, active development** — maturity/quality must be verified independently before any integration decision.
- **Verdict:** **Watch + benchmark** (license-clean, architecturally close to our goals; too early to integrate).

### B.1.21 Neural / DDSP-based approaches

- **Core principle (DDSP):** neural network predicts amplitudes/frequencies of a harmonic-oscillator bank + filtered noise, trained with differentiable spectrogram losses — classical DSP synthesis core with network control. **[VERIFIED-PAPER — Engel et al., ICLR 2020]**
- **Latency:** frame-rate control (hop 256 @ 16 kHz ≈ 16 ms historically) + network inference; real-time variants exist (DDSP-VST). **[VERIFIED-SOURCE]**
- **CPU:** small models run on CPU (embedded feasibility studied — "Distilling DDSP" 2025); production-grade on arbitrary instrument input is unproven. **[VERIFIED-SOURCE + INFERENCE]**
- **Dynamic pitch modulation:** frame-rate conditioning; a *pitch shifter* (as opposed to a synthesizer conditioned on pitch) of production grade does **not** exist today — RVC/so-vits-class systems are offline voice-conversion tools. **[INFERENCE from absence of evidence]**
- **Artifacts:** model-conditional timbre shift; transient weakness at large hops. **[VERIFIED-SOURCE — Hayes et al. 2023 review]**
- **Licensing:** models/kernels vary (Magenta Apache-2.0); runtime deps non-trivial for a VST.
- **Verdict:** **Do not pursue for v1** (no production-grade arbitrary-input real-time neural shifter exists; CPU/GPU + licensing complexity). Revisit if a controllable differentiable shifter matures.

---

## B.2 Dynamic pitch-curve test battery (special focus)

**Mandate:** evaluate every candidate against more than static ±3/+12 semitones. The battery (each item 4 s, 48 kHz, mono + stereo variants):

1. Static shift: −12, −5, +1, +7, +12 st.
2. Slow pitch ramp: 1 st/s.
3. Fast pitch ramp: 12 st/s (Doppler-class).
4. Large positive shift: +24 st static.
5. Large negative shift: −24 st static.
6. Rapid pitch reversal: +12 → −12 st in 100 ms (sine of rate-limited controllers — the killer test).
7. Transient input: percussive clicks/drum loop.
8. Noise input: white/pink noise.
9. Harmonic input: saw/harmonic stack, chord.
10. Voice input: sung vowel with vibrato; speech.
11. Whip recording + whoosh recording (from the Track-A corpus — the true target material).

**Expected outcome matrix (grade: ✓ pass / ~ marginal / ✗ fail) — predicted from the research, to be confirmed by benchmark:**

| Algorithm | Static | Slow ramp | Fast ramp | Reversal | Transients | Noise | Harmonic | Voice | Whip/Whoosh |
|---|---|---|---|---|---|---|---|---|---|
| Varispeed | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ (it *is* the physics) |
| Variable-delay/harmonizer | ✓ | ✓ | ✓ | ✓ | ~ (splice flutter) | ✓ | ✓ | ✓ | ✓ |
| OLA | ✗ | ✗ | ✗ | ✗ | ✗ | ~ | ~ | ✗ | ✗ |
| WSOLA | ✓ | ~ | ✗ | ✗ | ~ | ✓ | ✓ | ✓ | ✗ |
| TD-PSOLA | ✓ | ✓ | ~ (F0 lag) | ~ | ~ | ✗ | ✓ | ✓✓ | ✗ (fails non-voice) |
| Classic PV | ✓ | ~ | ✗ (warble) | ✗ | ✗ | ~ | ✓ | ~ | ✗ |
| PV + locking + L-D'99 per-frame shift | ✓ | ✓ | ~ | ~ | ~ (with reset) | ~ | ✓✓ | ~ | ~ |
| Transient-aware additions | ✓ | ✓ | ~ | ~ | ✓ | ~ | ✓✓ | ~ | ~ |
| Granular | ~ | ~ | ~ | ~ | ✗ | ~ | ~ | ~ | ✓ (textural whoosh!) |
| MQ/SMS | ✓ | ~ | ✗ | ✗ | ~ | ~ | ✓ | ✓ | ✗ |
| HNM | ✓ | ✓ | ~ | ~ | ~ | ✗ | ✓ | ✓ | ✗ |
| LPC | ~ | ✓ | ~ | ~ | ✗ | ✗ | ~ | ~ | ✗ |
| WORLD (voice) | ✓ | ✓ | ~ | ~ | ~ | ✗ | ✓ | ✓✓ | ✗ |
| Signalsmith Stretch | ✓✓ | ✓ | ~ | ~ | ✓ | ~ | ✓✓ | ✓ | ~ |
| Rubber Band R2 | ✓✓ | ✓ | ~ (tearing) | ~ | ✓ (crisp) | ~ | ✓✓ | ✓ | ~ |
| Rubber Band R3 | ✓✓ | ✓✓ | ~ | ~ | ✓✓ | ~ | ✓✓ | ✓✓ | ~ |
| pbshift | ✓? | ✓? | ~? | ~? | ✓? | ~? | ✓? | ✓✓? (Voice) | ? |
| Neural/DDSP | ? | ? | ? | ? | ~ | ~ | ? | ~ | ? |

**Discriminating observations from the research:**
- The **reversal test** is where phase-vocoder phase propagation and WSOLA sequence quantization fail hardest (update-rate + coherence restart). **[INFERENCE + VERIFIED-SOURCE (Rubber Band tearing report)]**
- The **whip/whoosh material** is where the time-domain/physical family is the only one that is *structurally correct* — everything that reconstructs (OLA/WSOLA/PV) smears sub-ms event structure. **[INFERENCE]**
- Only the L-D '99 formulation documents **per-frame-varying shift** as a designed mode. **[VERIFIED-PAPER]**

## B.3 Architecture research

### B.3.1 Should there be a common PitchProcessor interface? Yes — with a strict contract

```cpp
// Interface sketch (architecture only — NOT implementation)
struct PitchCurve {           // control path: per-sample ratio, block-quantizable
  const float* ratio;         // r[n], continuously interpolated by the engine
  int length;
};
interface PitchProcessor {
  // --- contract ---
  LatencyInfo latency() const;              // input & output latency, reported, not guessed
  bool supportsDynamicRatio() const;        // engines declare their control-rate ceiling
  void process(const AudioBus& in, AudioBus& out, const PitchCurve& curve);
  void reset();
  // --- optional capabilities (capability query, not type switch) ---
  FormantMode formantMode() const;          // shifted / preserved / independent
  TransientInfo transientBus();             // engines may consume/donate detections
};
```
- **Recommended members:** Resampler (shared core), VariableDelay, OLA, WSOLA, TDPSOLA, FDPSOLA, PhaseVocoder, PhaseLockedVocoder, TransientAwareVocoder, Granular — plus adapter wrappers for externals (Signalsmith, RubberBand-if-licensed, pbshift).
- **Rule:** the interface must *expose* per-sample ratio and let each engine quantize internally; `supportsDynamicRatio()`/control-rate declarations prevent misuse (e.g., routing the Doppler curve into a 40–90 ms WSOLA sequence updater silently).

### B.3.2 Shared component pool (which algorithms can share what)

| Component | Shared by |
|---|---|
| **FFT (pocketfft/PFFFT) + windowing** | Classic PV, phase-locked PV, transient-aware PV, L-D '99 shift, MQ, SMS, HNM, (FD-PSOLA) |
| **Resampler (Kaiser-sinc)** | Varispeed, OLA/WSOLA post-resample, PSOLA grain resampling, harmonizer read taps, Doppler propagation reads |
| **Delay buffers + fractional read machinery** | Variable-delay, harmonizer/dual-tap, granular, **physical Doppler propagation bank (Track A)** |
| **Transient detector** | Transient-aware PV, harmonizer intelligent splicing, (whip-crack onset analysis in Track A!) |
| **Pitch tracker (F0 + voicing)** | TD-PSOLA, FD-PSOLA, HNM, LPC, WORLD voice lane |
| **Phase analysis (IF/group delay)** | PV family, MQ, pbshift-style engines |
| **Formant/envelope estimator (cepstral or true-envelope)** | PV formant preservation, SMS residual, HNM envelope |
| **Fundamentally independent architectures** | WORLD (its own DIO/Harvest/CheapTrick/D4C stack), neural runtimes, external libraries (Signalsmith/RubberBand/pbshift wrap behind adapters, not shared internals) |

### B.3.3 Should the physical Doppler engine bypass the PitchProcessor abstraction? **Yes.**

Reasons (engineering, strong):
1. **Category difference:** a PitchProcessor is a *duration-preserving* musical transform; the physical Doppler engine is a *duration-modulating* physical propagation (output time ≠ input time by design — the retarded-time map). Forcing it through the same interface is a category error. **[INFERENCE]**
2. **Supersonic event semantics:** above Mach 1 the model needs event-based shock scheduling, which has no PitchProcessor representation. **[INFERENCE from A.4]**
3. **Per-segment multi-tap semantics:** the whip is N sources with individual retarded times, not one ratio curve. **[INFERENCE]**
4. **Shared infrastructure, not shared interface:** the Doppler engine still *uses* the same delay-bank/resampler/transient components — shared code without shared contract.

**Recommended architecture:**
```
AudioGraph
├── PropagationEngine (Track A: retarded-time delay bank, segment bus,
│                       shock event scheduler) — dedicated interface
├── PitchProcessor lane (musical pitch effects, B.3.1) — feeds the
│   PropagationEngine's source bus OR post-processes its output
└── Shared pool: FFT / resampler / delay / transient / F0 / envelope
```


## B.4 Licensing classification

All claims verified against actual license files/texts where marked; **none** were inferred from repository descriptions alone.

| Library / component | License (verified) | Classification for proprietary commercial VST |
|---|---|---|
| **Rubber Band (R2+R3)** | GPL-2.0+ / commercial dual (COPYING verified) | **Commercial licence required** (tiers: £590 attribution / £1490 / £9320) |
| **zplane élastique (PRO/EFFICIENT/INTONE)** | Proprietary SDK, quote pricing | **Commercial licence required** (opaque pricing) |
| **Signalsmith Stretch** | **MIT** (LICENSE.txt verified — commonly misreported as LGPL) | **Safe for proprietary integration** (keep notices) |
| **pbshift (pbTechLab)** | MIT (verified); dep pffft = FFTPACK-derived BSD-like | **Safe** — but pre-1.0 maturity risk |
| **WORLD** | Modified-BSD, "no patent in all algorithms" | **Safe** (speech-focused) |
| **SoundTouch** | LGPL-2.1 (COPYING.TXT verified) | **LGPL conditions** — poor fit for a statically-linked proprietary VST |
| **Bungee (open core)** | MPL-2.0 (verified) | Usable with file-level copyleft isolation |
| **stftPitchShift** | MIT | Safe (reference) |
| **phaseret (RTPGHI)** | GPL-3.0 | **Research reference only** (paper itself is CC BY 4.0 → clean reimplementable) |
| **maxrmorrison/psola** | GPL-3.0 (Praat-derived) | **Incompatible** (Praat contamination on ported code) |
| **sannawag/TD-PSOLA** | MIT | Research reference only (quality caveats) |
| **CREPE** | MIT | Offline benchmarking only (CNN too heavy for RT path) |
| **librosa/pYIN** | ISC | Offline benchmarking only (Python) |
| **KISS FFT / pocketfft** | BSD-3-Clause (verified) | **Safe** |
| **PFFFT** | FFTPACK/UCAR BSD-like (verified — *not* FFTW-derived) | **Safe** (incl. vendored sse2neon MIT) |
| **FFTW** | GPL (+ paid commercial) | **Avoid** |
| **Intel IPP** | Proprietary paid | Optional commercial |
| **JUCE** | AGPLv3 + commercial dual (LICENSE.md verified) | **JUCE commercial licence required** for proprietary plugin |
| **VST3 SDK** | **MIT (now — Steinberg)** (verified; GPLv3/proprietary dual licensing retired) | **Safe** — the classic VST3 licensing blocker no longer applies |
| **Open-source Doppler plugins (delirion, Melodrumatic, ovni)** | GPL-3.0 / AGPL-3.0 (each verified) | **Research reference only** — and trivially reimplementable from scratch anyway |
| **DAFx book MATLAB code** | Educational-only header (no commercial use without permission) | **Reimplement from the book's math, do not port the code** |
| **"Harmonizer" (name)** | Eventide trademark | Naming caution only |

## B.5 Benchmark strategy

### B.5.1 Common benchmark corpus (fixed, versioned)

1. Pure sine (440 Hz, 5 s) — pitch-error ground truth.
2. Harmonic tone (saw @ 220 Hz + vibrato).
3. Complex tonal source (piano chord / mix excerpt).
4. Voice: sung vowel (vibrato) + speech (male/female).
5. Percussion: drum loop + isolated clicks.
6. Noise: white + pink.
7. Whoosh recording (from Track-A measurements).
8. **Whip crack recording** (from Track-A measurements, ≥ 500 kHz capture).
9. Fast transient: castanets/impulse train.
10. Stereo ambience (correlated + decorrelated variants).
11. (Track-B battery items from B.2 as the dynamic overlay.)

### B.5.2 Measurable metrics (per algorithm × corpus × battery item)

| Metric | Procedure |
|---|---|
| **Pitch error** | pYIN/CREPE on shifted sine/harmonic items vs. expected instantaneous f₀(t) — including *tracking lag* under ramps/reversals (cross-correlate measured vs. expected curve) |
| **Spectral error** | Log-spectral distance vs. ideal varispeed reference (which is artifact-free by definition) |
| **Transient preservation** | Attack-correlation on the transient items (audiojs-style) + onset-time error |
| **Phase coherence** | Per-alignment metric on harmonic stacks (audiojs: PSOLA 0.998 vs OLA-family lower) |
| **Stereo coherence** | Inter-channel cross-correlation delta vs. input |
| **Latency** | Measured impulse-identity delay (never vendor-claimed) |
| **CPU** | Real-time factor per block size (32/64/128/256/512/1024), single core, pinned |
| **Artifact rate** | Automated detector: warble (f₀ flutter variance), phasiness (spectral flatness of residual vs. varispeed reference), AM at crossfade/grain rate |
| **Dynamic pitch stability** | Ramp/reversal tracking error + listening-flagged glitch count |

### B.5.3 Subjective listening tests

- **MUSHRA-style** with anchors: hidden reference, 3.5 kHz lowpass anchor, OLA (low anchor), varispeed (mid anchor), plus the engines under test; 8–12 trained listeners; per-material ratings + forced preference on the battery items.
- **Whip/whoosh realism** (Track-A specific): A/B/X against real recordings, recognition-rate reporting — ties Track B benchmarking to Track A truth data.
- **Rapid-modulation specific:** "does the pitch feel glued to the curve?" pairwise tests using the fast-ramp/reversal battery.

### B.5.4 Benchmark implementation plan

- Offline render harness (Python/C++), identical corpus + battery for all engines; external engines (Signalsmith, RubberBand, pbshift, SoundTouch) wrapped as offline renderers for fairness (no block-size gaming).
- All results published in a versioned matrix alongside this report when the prototypes exist.

---

## B.6 Final recommendations

### 1. Minimum prototype set (build first — order matters)

1. **Retarded-time Doppler core** (variable-delay + implicit solver + saturated 1/(1−M_r) + sinc reads) — the Track-A engine core *and* the fastest PitchProcessor member. **[from scratch]**
2. **Dual-tap harmonizer with intelligent splicing** on the same delay infrastructure (sustained-range extension + classic character). **[from scratch]**
3. **Kaiser-sinc resampler core** (shared by everything). **[from scratch]**
4. **N = 48–64 hierarchical segment whip model + Friedlander/N-wave shock stage** — plus the **Model A vs Model B critical experiment** (A.6.1). **[from scratch]**
5. **Classic PV + identity/peak phase locking** as the spectral baseline. **[from scratch — L-D '99 + Röbel '03, patents expired]**

### 2. Extended research prototype set

- **L-D '99 per-frame-varying frequency-domain shift** pipeline (the most dynamic-friendly spectral method — explicit per-frame shift design). **[from scratch]**
- **Transient phase reset** stage on top of it (Röbel-style; benchmark reset-vs-rapid-curve interaction).
- **WSOLA engine** (quality mode for slow/static ratios). **[from scratch or link SoundTouch under LGPL terms]**
- **TD-PSOLA reference** for the voice benchmark lane. **[from scratch from the 1990 paper]**
- **Phase-gradient PV (RTPGHI) reference** — the spectral quality ceiling. **[reimplement from the CC BY 4.0 paper; phaseret GPL-3 code is reference-only]**
- **Granular creative mode** (cheap once delay infra exists).
- **Formant preservation via cepstral/true-envelope correction** (borrowing HNM's "grid moves, envelope stays" + CheapTrick's pitch-adaptive envelope ideas).
- **pbshift** integration trial (MIT; verify maturity first).

### 3. Algorithms worth integrating into the final VST

- **Own implementations:** retarded-time variable-delay core; dual-tap harmonizer; varispeed mode; L-D'99 spectral engine (+locking + transient reset + envelope-based formant control); granular character mode; WSOLA slow/static mode.
- **External (license-clean):** **Signalsmith Stretch (MIT)** as the high-quality slow-curve mode. FFT: **pocketfft or PFFFT**. Framework: **JUCE (commercial licence)** + **VST3 SDK (now MIT)**.
- **Conditional external:** Rubber Band commercial licence (from £590) if its quality/robustness beats our spectral lane at ship time; pbshift if it matures.
- **Physical Doppler engine:** dedicated PropagationEngine (not a PitchProcessor) sharing the resampler/delay/transient pool.

### 4. Algorithms worth keeping only as benchmark implementations

- Classic PV (unlocked) — baseline.
- OLA — worst-case anchor.
- Rubber Band R2 + R3 (external offline renders) — industry quality ceiling.
- RTPGHI/phase-gradient — spectral quality ceiling.
- MQ/SMS — harmonic-material reference.
- WSOLA (SoundTouch offline render) — TD quality reference.
- élastique demo renders — commercial quality ceiling (no linkage, renders only).

### 5. Algorithms that should not be pursued

- **HNM, LPC, WORLD** as product engines (speech-only scope; wrong domain for an instrument-grade general shifter).
- **FD-PSOLA** (superseded by the spectral lane).
- **Neural/DDSP** for v1 (no production-grade arbitrary-input real-time shifter exists; CPU/licensing complexity).
- **Naive OLA** as a product mode.
- **(Track A)** naive receiver-time Doppler near Mach 1; classical-formula-as-operator; full nonlinear PDE in the real-time path; nonlinear-propagation mini-solver (defer).

### 6. Dependencies and licences (final-shape summary)

- **From-scratch core** (Doppler, harmonizer, resampler, L-D'99 PV): no dependencies, no exposure.
- **Signalsmith Stretch:** MIT — integrate, keep notices.
- **FFT:** pocketfft (BSD-3) or PFFFT (BSD-like) — both clean; FFTW excluded (GPL).
- **JUCE:** commercial licence required for proprietary distribution (AGPL alternative).
- **VST3 SDK:** MIT — no constraint.
- **Rubber Band:** only under commercial licence (or as external benchmark renders under GPL, unlinked).
- **SoundTouch:** LGPL — only dynamically linked if used at all.
- **élastique:** commercial quote — benchmark renders only.
- **Track-A engine:** zero external dependencies (deliberate).

### 7. Recommended architecture for combining physical Doppler with independent pitch algorithms

```
┌────────────────────────────────────────────────────────────────┐
│ AudioGraph                                                      │
│                                                                │
│  [Source bus] ──► PitchProcessor lane (musical pitch:           │
│        ▲           varispeed | harmonizer | L-D'99 spectral |   │
│        │           WSOLA | granular | Signalsmith external)     │
│        │                 │ (per-sample ratio control path)      │
│        │                 ▼                                      │
│  [Whip motion model M3/M4: segment trajectories]                │
│        │                                                       │
│  PropagationEngine (dedicated, bypasses PitchProcessor):        │
│   • per-segment retarded-time solver + delay bank (shared       │
│     resampler/delay machinery)                                  │
│   • saturated 1/(1−M_r) pile-up (crack-onset cue)               │
│   • event-based shock scheduler (Friedlander/N-wave) for        │
│     supersonic segments                                         │
│   • shared transient detector (also serves PitchProcessor lane) │
└────────────────────────────────────────────────────────────────┘
```

**Why this split (recap):** the physical engine is duration-modulating and needs supersonic event semantics — both are outside the PitchProcessor contract; the musical engines are duration-preserving and need musical quality. They share *components* (resampler, delay, FFT, transient, envelope) but not *contracts*. The Doppler curve never routes into hop/sequence-quantized engines (their control-rate ceilings are declared, queried, and enforced by the interface).

---

## Negative results — explicitly not hidden

1. **No peer-reviewed whip-crack SPL or spectrum exists in retrievable literature** — we must measure (A.6.3). All circulating numbers are folklore.
2. **No open-source, physically-motivated whip-crack audio synthesis exists** — the whole Track-A audio layer is from-scratch by necessity.
3. **Wwise/FMOD Doppler clamping practice could not be verified** in this research pass (searches did not reach the official docs).
4. **Exact whip tip Mach number and tip acceleration numbers remain unverified from primary PDFs** (attributed ~Mach 2; acceleration [UNPROVEN]).
5. **Rubber Band fast-modulation quality is user-reported as "tearing"** (JUCE forum) — a negative data point for using R2/minLatency on rapid curves.
6. **pbshift quality/CPU claims are entirely self-reported** (pre-1.0 project) — unverified.
7. **Neural real-time pitch shifting for arbitrary input: no production-grade system found** — treated as absent, not merely unsuitable.
8. **M/S stereo processing for coherence preservation: no source found** — flagged [UNPROVEN] for the benchmark, not assumed.
9. **The often-cited SoundTouch authorship "Ondrej Parfut" is wrong** (it is Olli Parviainen) — corrected here because licensing diligence depends on correct attribution.
10. **Signalsmith Stretch is repeatedly misreported as LGPL — the actual license file is MIT.** Verified directly.

## B.7 References (Track B)

1. Verhelst, W. & Roelands, M. (1993). "An overlap-add technique based on waveform similarity (WSOLA)." ICASSP.
2. Moulines, E. & Charpentier, F. (1990). "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones." Speech Communication 9, 453–467.
3. Laroche, J. & Dolson, M. (1999). "New Phase-Vocoder Techniques for Pitch-Shifting, Harmonizing and Other Exotic Effects." IEEE WASPAA '99. (+ their IEEE Trans. Speech & Audio Proc., May 1999.)
4. Flanagan, J. & Golden, R. (1966). Bell Syst. Tech. J. 45; Portnoff, M. (1981). IEEE Trans. ASSP 29(3) — phase vocoder origins.
5. Röbel, X. (2003). "A new approach to transient processing in the phase vocoder." DAFx-03.
6. Průša, Z. & Holighaus, N. (2022). "Phase Vocoder Done Right." arXiv:2202.07382 (+ phaseret toolbox, GPL-3).
7. Schörkhuber, C. et al. (2012). "Pitch shifting using the constant-Q transform." DAFx-12.
8. McAulay, R. & Quatieri, T. (1986). "Speech analysis/synthesis based on a sinusoidal representation." IEEE Trans. ASSP 34(4).
9. Serra, X. & Smith, J. (1990). "Spectral modeling synthesis." Computer Music J. 14(4).
10. Stylianou, Y. (2001). "Applying the HNM in concatenative speech synthesis." IEEE Trans. SAP 9(1).
11. Morise, M. et al. (2016). "WORLD: a vocoder-based high-quality speech synthesis system." IEICE E99-D(7). (+ APSIPA 2020 real-time generator.)
12. Engel, J. et al. (2020). "DDSP: Differentiable Digital Signal Processing." ICLR.
13. Haghparast, A., Penttinen, O., Välimäki, V. (2007). "Real-time pitch-shifting with flutterless time-varying factor." DAFx-07.
14. J. O. Smith (2010). Physical Audio Signal Processing. W3K.
15. Zölzer, U. (ed.) DAFx Book, 2nd ed., ch. 6 (time-segment processing) — code educational-only.
16. Vendor/primary sources verified during research: breakfastquay.com (Rubber Band technical + license + COPYING), Signalsmith-Audio/signalsmith-stretch (LICENSE.txt + code), github.com/pbtechlab/pbshift, mmorise/World (LICENSE.txt), surina.net + SoundTouch COPYING.TXT, zplane.de technology pages, JUCE LICENSE.md, steinbergmedia/vst3sdk (MIT), audiojs/shift (benchmark README), SuperCollider PitchShift docs, W3C Web Audio spec.

---

*End of report. Next phase: the prototype experiments defined in A.6 and B.6.1 — no implementation decisions are final until the Model A/B critical experiment and the dynamic-pitch benchmark have run.*
