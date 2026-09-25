# Pitch Lab — Reference Model Notes

**What counts as a reference, and why** (architecture §H.2; implementation
specification §10.3). Authority: the architecture document and the
implementation specification supersede this note if they ever disagree.

## Reference ENGINE (role: render comparison anchor)

`native.varispeed` — band-limited (Kaiser windowed-sinc + pre-shift
anti-alias filtering, architecture §I.2) rate-following engine. Chosen
because varispeed is artifact-free by construction for band-limited inputs
[RESEARCH FINDING — research report §B.1.1]: every frequency scales by the
ratio, harmonic ratios and transient waveforms are preserved (compressed,
not reconstructed), and inter-channel coherence is exact.

- It is a reference **anchor**, not a "best" judgement, and is never used to
  judge creative-mode output (creative mode has no correctness target).
- UsageClass remains `Prototype`; the reference ROLE is a registry flag
  (`isReferenceRole = true`) and manifests tag reference renders.
- One reference render per (input, curve, fs) is produced by the ordinary
  render path each run.
- Its RateFollowing duration behaviour (output length follows the integrated
  curve) is the *defining* property the length policy §D.4 protects — the
  reason varispeed must never be forced into a fixed-length contract.
- Known deviations from "pure physics": the conservative anti-alias
  pre-filter policy (whole-curve max ratio, spec §6.1) can over-attenuate
  high frequencies relative to an instantaneously ideal filter; this is a
  documented, deterministic, measured property.

## Reference PITCH TRACKER (role: analysis measurement instrument) — OPEN

The intended reference is **pYIN** (Mauch & Dixon 2014). Under the owner's
C++-only constraint (L-1) no Python/librosa runtime is admissible. Research
(worklog Task 10-c) found no acceptable off-the-shelf C++ pYIN:

| Candidate | Licence | Verdict |
|---|---|---|
| c4dm/pyin (original) | GPL-2.0+ | copyleft — not linkable into the Lab binary |
| xstreck1/LibPyin | GPL-3.0 | same copyleft problem |
| Essentia PitchYinProbabilistic | AGPL-3.0 | heavyweight + copyleft |
| aubio (yin/yinfft) | GPL-3.0 | plain YIN only + copyleft |
| **Sleepwalking/libpyin + libgvps** | **BSD-3-Clause** | full pYIN, C99, dependency-light, but unmaintained (2020), minimal API, small documented deviations — build-verified during research |

**Open decision OD-12** (owner): (A) vendor the BSD-3 libpyin with a
validation gate; (B) clean-room C++ pYIN from the 2014 paper + librosa
parameter documentation (recommended — licence-clean, fits the
own-primitives philosophy; ~800–1200 LOC); (C) interim plain C++ YIN with a
documented deviation; (D) defer the tracker (pitch-error/lag metrics wait).

Until OD-12 is resolved, tracker-dependent metric modules ship with the
tracker interface and analytic-corpus expected-value paths, and report
status `tracker-unavailable` for measured-f0 comparisons.

## What is deliberately NOT a reference

- External engines (Rubber Band, élastique): quality ceilings, never
  correctness references (post-v0.1, subprocess adapters only).
- OLA: worst-case anchor for listening tests, not a correctness reference.
- The synthetic corpus: analytic ground truth (expected f0(t), expected
  lengths) is computed from the generator parameters, not from any engine.
