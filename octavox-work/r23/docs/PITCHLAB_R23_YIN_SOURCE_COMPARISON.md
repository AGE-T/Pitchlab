# PITCHLAB R23 — YIN Source Comparison (Gate 3)

**Sources reviewed with actual code (fetch dates 2026-11-04; full
per-source evidence: `evidence/analysis_round23/g3a1_repos.md`,
`g3a2_sources.md`, `g3b1_librosa.md`, `g3b2a_aubio.md` — fetched-URL
appendices in each).** Classes: OBS = read from fetched source; DER =
derived; UNK = unresolved after retries. AGPL-3.0 code was described only,
never copied.

| Source | Version/commit | License [OBS] | Difference function | CMNDF start | Threshold + rule | Parabolic target | Step 6? |
|---|---|---|---|---|---|---|---|
| Original paper (de Cheveigné & Kawahara 2002) | JASA 111(4) | (text; mirror fetch FAILED → wording UNK, structure known from implementation cross-refs) | fixed-W one-sided frame | lag 1 (canonical) | 0.10–0.15; first local min below; global-min fallback; interpolated ordinate for the test | abscissa from RAW d (paper's bias note) | YES (temporal, ±20% re-run) |
| hyuncat/YIN-algorithm | main (no LICENSE file → all-rights-reserved, terms UNK) | NONE [OBS] | shrinking N−τ via constant-energy FFT identity (docstring overclaims) | lag 1 ✓ | 0.1 + global-min fallback; troughs from argrelmin of RAW d | RAW d | NO (single frame) |
| ABSounds/YIN-Pitch | main | **AGPL-3.0** [OBS] | brute-force shrinking, j=2..W−τ (skips sample 0 — off-by-one [OBS]) | lag 1 ✓ | **0.4** (0.2 in demo), NO fallback (returns None) | CMNDF | NO; offline zero-phase SciPy decimation (non-real-time design) |
| Tonalux article | — (educational) | — | d = 2r(0)−2r(τ) (equal-energy special case presented as general [OBS]) | article's `cmndf()` is MISNAMED (computes raw d) | checked at crossing sample; no local-min descent, no fallback | — | one vague sentence only |
| Rust `pitch-detection` | 0.3.0 (2022-06-30) | MIT/Apache-2.0 [OBS] | fixed-W (matches paper) | **off-by-one**: d'(τ)=(τ+1)d(τ)/Σ_{1..τ} ⇒ d'(1)=2 [OBS] | 1−clarity_threshold (callers pass 0.6–0.7 ⇒ 0.3–0.4); no fallback; deepest (not first) local min | CMNDF | **explicitly NOT implemented** (module doc + TODO comment [OBS]) |
| librosa (doc/main) | 1.0.0 | ISC [OBS] | shrinking (≡ zero-extended shift; verified 4.3e-14 by the agent) | lag 1 (full range) | 0.1; **first local min below threshold, global-min fallback** (the paper's rule — the task's "global argmin" hypothesis REFUTATED [OBS]) | CMNDF | no (that is pyin's HMM domain) |
| aubio | 0.4.7/0.4.9 sources | **GPL-3.0+** [OBS] (reuse FORBIDDEN — description only) | classic: fixed W=bufsize/2 time-domain; yinfast: same via FFT+prefix sums | lag 1 (warm window) | **0.15** live (0.1 only in a compiled-out block); first-min + global fallback | CMNDF (3-point) | no |

**The five biggest deviations from canonical YIN across implementations**
(the round's synthesis; details in the agent evidence files):

1. **Nobody implements Step 6** except the paper (and our reference
   implementation) — the temporal best-local-estimate stage is absent from
   every fetched library; each substitutes something else (librosa: a
   separate pyin HMM; aubio: nothing; Rust crate: nothing, documented).
2. **The Rust crate's CMNDF is off-by-one** (d'(1)=2 instead of 1) and its
   `clarity` is sign-broken (a clean sine yields −1.07 instead of ~1.0) —
   frequency output remains excellent [OBS, verified by simulation].
3. **Threshold chaos**: 0.1 / 0.15 / 0.2 / 0.3–0.4 / 0.4 / 0.85(yinfft)
   across sources; two sources drop the global-min fallback (ABSounds,
   Rust), changing failure semantics from "always an estimate" to
   "unvoiced".
4. **Parabolic target splits** (raw d vs CMNDF) with real accuracy
   consequences (our Gate-4 measurement: raw-d abscissa ≤0.03 c vs
   CMNDF +0.09 c at 880 Hz — the paper's bias note is correct and
   measurable).
5. **Support conventions differ silently** (fixed-W vs shrinking N−τ vs
   aubio's W=bufsize/2) — the engines' windows are NOT interchangeable;
   our reference implements and validates all three forms against
   independent vectors.

**Reuse verdicts:** paper formulas only for our proprietary C++ (the
algorithm is public mathematics; no patent status claim made — UNK);
librosa ISC permissive but Python (we re-derive); Rust crate MIT/Apache-2.0
(behaviour verified INDEPENDENTLY before trusting — and it failed two
correctness checks); aubio GPL-3.0 and ABSounds AGPL-3.0 — code reuse
FORBIDDEN, documentation only. The hyuncat repo has NO license — treated as
all-rights-reserved (reference-only).
