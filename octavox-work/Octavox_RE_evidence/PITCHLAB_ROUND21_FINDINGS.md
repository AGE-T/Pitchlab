# PITCHLAB ROUND 21 FINDINGS

**Product:** PitchLab Harmonizer (the independent harmonizer; provisional
identity introduced this round) · **Core:** octavox-native 16.1.0-R21
(Config::f0_min added — default bit-exact; every existing hash and behaviour
unchanged, gated by the 96-case suite) · **Original-target research:** the
Eventide Octavox static RE continues (10 prioritized items closed/upgraded).
The original binary was NEVER executed.

## Verdict table

| Item | Verdict |
|---|---|
| P0 product identity | **DONE** — Octavox removed from all user-facing generated product strings; vendor `PitchLab Project`; contact fields EMPTY; historical RE docs keep "Eventide Octavox" (research term); FUIDs preserved for the two Balanced variants; new deterministic FUID for Lagrange6-Hybrid |
| P0 parameter semantics | **DONE** — Spread → **Harmony Map** (title-only; id/range/state/DSP unchanged); Global Pitch Shift / Voice N Transpose clarifications; 38 static help texts; gated by test case 21 |
| P1 three VST3 variants | **DONE** — `OCTAVOX_VST3_DETECTOR=DECIMATED\|HYBRID\|FULLRATE` × interpolation axis; Linux: 21/21 wrapper tests × 4 builds, 12/12 integration × 4, validator 47/47 × 4; Windows x64 modules built + structurally verified (3 user-facing + 1 FullRate benchmark) |
| P1 real-audio harness | **DONE** — `octavox_r21_realaudio` (external WAV, 3 detectors, trajectories + 10 metrics + JSON); synthetic/real categories separated; the R20 "real-material" docs corrected |
| P1 minimum pitch | **DONE** — `Config::f0_min` (default 60 bit-exact); battery over {32.7,41.2,55,60,80,110} Hz; original's Low Note CLOSED statically ("Minimum Pitch", C0..B3, default C2=65.4 Hz); **default NOT changed** |
| P1 stereo matrix | **DONE** — the four behaviour classes distinguished + measured; the inaccurate R20 comment corrected; linked-tracker design evaluated (not implemented) |
| P2 static RE (10 items) | **7 CLOSED / 2 parameter-level OBS with bounded UNK / 1 upgraded** — see the verdict lines below |
| P2 harmony mapper | **DONE** — mathematical spec + independent implementation + **545/545 reference vectors (worst deviation 0.0)**; not host-exposed (ids 0–37 untouched) |
| P3 packaging hygiene | **DONE** — manifest regenerated from the actual tree; the 12 stale + 1 missing + 50 unlisted entries reconciled and recorded; fresh-extraction verification passes with exact file count, zero stale, zero missing, zero unlisted; archive SHA-256 recorded |
| Gates | 96/96 core acceptance (unchanged suite); all reference hashes unchanged; 4 Windows PE32+ modules verified |

Static-RE item verdicts (details: `evidence/analysis_round21/r21_static_findings.md`):
pan = OBS routing/consumer, UNK mix law · per-voice feedback = **CLOSED**
(exists: −100..0 dB → 10^(dB/20) linear, mute-gated) · pitch tracking =
**CLOSED** (off ⇒ all voices unity rate — R20 wording corrected) ·
instrument = **CLOSED** (anti-alias cutoff tables, not tracker profiles) ·
low note = **CLOSED** (Minimum Pitch, 48 choices C0..B3, default C2) ·
crossfade = **two-ends OBS** (binding observed; internal build DER) ·
randomize = **CLOSED** (producer chain fully traced) · MIDI = parameter OBS,
routing UNK (no DSP-side MIDI modules — precise negative result) ·
tempo/sync = parameters + chunk tags OBS, conversion UNK (host-side) ·
snapshots = chunk system OBS, semantics UNK. Bonus: Live Mode CLOSED (+40
samples per-voice delay compensation).

## P0 — Identity changes and compatibility implications

* **Names:** display names/bundles are now
  `PitchLab Harmonizer Lagrange6 Balanced` / `PitchLab_Lagrange6_Balanced.vst3`,
  `PitchLab Harmonizer Lagrange6 Hybrid` / `PitchLab_Lagrange6_Hybrid.vst3`,
  `PitchLab Harmonizer ObsKernel6 Balanced` / `PitchLab_ObsKernel6_Balanced.vst3`
  (plus FullRate benchmark builds, offline only). Vendor: `PitchLab Project`;
  URL/e-mail: EMPTY (nothing invented). moduleinfo.json carries the same
  identity (verified). The validation kit, build reports and docs follow.
* **What "Octavox" still legally means in the tree:** the RE research
  documentation (docs/, evidence/) where it denotes the RESEARCHED BINARY; the
  internal C++ namespace `octavox::*`; historical evidence files (provenance
  preserved — NOT renamed).
* **FUID compatibility:**
  * Lagrange6+Decimated ("Balanced"): **preserved lineage FUID**
    {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60} — sessions saved by R16–R20
    "OctavoxNative"/"OctavoxRE Lagrange6" modules restore (state blob 'OVR1'
    v1/v2-compatible; the variant selections are compile-time authoritative).
  * ObsKernel6+Decimated ("Balanced"): **preserved R18–R20 FUID**
    {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}.
  * Lagrange6+Hybrid: **new deterministic FUID** {BD4A5DDF-24B1-2C39-DD2B-7F74A7A867CB}
    (factory {5062BAD4-A7A0-F34E-B3A8-1E2EA53A5619}) = SHA-256("PitchLab R21
    deterministic FUID mint v1 (2026-11-02) :: processor class UID for the
    PitchLab Harmonizer Lagrange6 Hybrid variant")[0..15] — a NEW class identity
    (nothing to be compatible with; the Hybrid detector was never shipped as a
    module before).
  * Every other buildable (interp × detector) combo carries its own minted
    FUID (full matrix in octavox_build_config.h) so no two variants share a class.
* **Variant authority over state:** neither the interpolation nor the detector
  mode is ever silently overridden by saved state (processor restore pins both
  to the build's compile-time selections — gated by test case 21 with
  tampered-foreign-value blobs restoring with surface values intact).

## P1 — The three VST3 variants (and the benchmark build)

Central selection (native/vst3/CMakeLists.txt): `OCTAVOX_VST3_INTERPOLATION`
(LAGRANGE6|LAGRANGE6Q|OBSKERNEL6) × `OCTAVOX_VST3_DETECTOR`
(DECIMATED|HYBRID|FULLRATE) — translated to exactly one `_INTERP_*` and one
`_DET_*` compile definition consumed only via octavox_build_config.h (the
single identity authority; a bundle-name mismatch fails the module-integration
tests by construction).

Validation (all four builds; evidence in
evidence/analysis_round21/logs/r21_vst3_*.txt):
wrapper 21/21 × 4 (the new case 21 gates the identity/titles/help/variant
authority), integration 12/12 × 4, Steinberg validator 47/47 × 4, core
96/96. FullRate remains buildable for offline/benchmark comparison only
(96.8% of CPU at 96 kHz — never the recommended real-time mode; the R19/R20
measurements stand).

## P1 — Real-audio comparison harness + the synthetic correction

`octavox_r21_realaudio` accepts external WAV files (PCM 16/24/32 + float32 —
readWav extended) or a --list; per input × mode (FullRate/Decimated/Hybrid on
identical audio and settings) it records the raw trajectory (64-sample
cadence, CSV), median/max cent difference vs the FullRate reference (or vs
--annotate ground-truth spans when supplied — the no-ground-truth honesty
contract is in the tool header), octave-disagreement, dropouts, jitter,
transient recovery, relative lock delay, CPU, output discontinuity scan, and
file metadata; JSON + trajectories out.

Demonstrated end-to-end on three SYNTHETIC WAVs (category "synthetic",
honestly labelled: evidence/analysis_round21/realaudio_demo/ +
r21_realaudio_demo.json): Hybrid's median agreement with FullRate is
0.007–0.031 cents vs Decimated's 0.031–0.336; the speech-like corpus shows
13.6% octave disagreement for BOTH dec+hyb at gating edges (honest data).
The recommended user-supplied real test set (male/female singing, spoken
voice, bass, electric/acoustic guitar, sustained, fast vibrato, portamento,
breath, transients, multi-pitch) is documented in the tool header; the R20
synthetic corpora + results are preserved as the separate synthetic category,
and every doc that called them "real material" now carries the correction.

## P1 — Configurable minimum detection frequency (Low Note investigation)

`Config::f0_min` (Hz): the detector's lower validity gate AND lag-domain floor
(tau_max = rate/f0_min drives both window laws); validated into [20,200] Hz
and < fmax; **default 60.0 = the R12–R20 fixed law bit-exactly** (the 96-case
suite + all reference hashes unchanged); buffer-determining for re-prepare;
NOT serialized in the state blob (the v2 layout is frozen — a restored session
keeps the default; documented).

Battery (octavox_r21_fmin; 6 floors × 3 modes × 2 rates × 14 tones;
r21_fmin.json): all three detectors detect from the floor upward with
93–98% success and sub-5-cent median error; 27.5 Hz is correctly rejected by
every floor ≥ its own value. Cost (Decimated, 48 kHz, in-range tones):
32.7 Hz → 1050 ns/s (2.9×), 41.2 → 706 (2.0×), 55 → 381 (1.06×), 60 → 358
(baseline); raising the floor to 80/110 LOSES the low tones (65.8%/33.3%
in-range success) — the floor is a hard gate.

**The original's Low Note (new static evidence):** "Minimum Pitch" — a 48-choice
note selector (C0..B3 = 16.35..246.9 Hz), default C2 = 65.4 Hz, consumed as
the tracker's period-search base (T+0x5F70 = x, +2 for the max index). The
original's default is HIGHER than our 60 Hz; its selectable range reaches
C0 = 16.35 Hz. **Our default stays 60.0 Hz** — the change is not silently
made; the config API + battery evidence + the original's mapping are all
recorded for a future decision.

## P1 — Stereo/voice-delay matrix (the corrected interpretation)

See docs/R21_DUALMONO_STEREO_CORRECTION.md (full text): the four classes
measured (r21_stereo.json): (1) exact dual-mono transparency — bit-identical
L/R in EVERY identical-input scenario (0 violations); (2) same-pitch voices,
distinct default delays — measured inter-voice lag exactly 32 samples
(0.667/0.333 ms), 0.0 when forced equal; (3) Harmony-Map pitch — V1 ratio
2^(7/12) measured; (4) true L/R divergence only from non-identical input
(1-sample offset → −23 dB RMS; 0.1% imbalance → −60 dB; decorrelated →
−3.9 dB). The user's Haas observation = (2)+(3) (inter-voice temporal
mechanism), NOT an L/R mechanism. The linked-stereo tracker design is
evaluated (fixes class-4 divergence; couples anti-phase material; NOT
implemented, NOT the default). r20_dualmono.cpp's inaccurate comment is
corrected in place.

## P2 — Original-target static RE

`evidence/analysis_round21/r21_static_findings.md` (the 10 items, each with
producer/consumer/addresses/classes + the negative-results section) +
`r21_param_strings.txt`, `r21_param_string_refs.txt`, `r21_param_blocks.txt`,
`r21_scan_rdata_strings.txt`, `r21_scan_stereomixer.txt`. Headline: the
complete JUCE host-parameter name table (30 families) + its single
registration function located; per-voice Pan%d and Feedback%d parameters
CONFIRMED to exist; Minimum Pitch/Crossfade/Randomize/Pitch
Tracking/Instrument/Live Mode closed at the record level; MIDI/tempo/snapshot
machinery is JUCE-side (precise negative results in the DSP graph).

## P2 — The independent harmony mapper

docs/R21_HARMONY_MAPPER_SPEC.md (the full mathematical spec: all recovered
tables with VAs, the zone-run fill, the walk arithmetic with exact
addresses, the key-ratio law, the composition law, determinism, the honesty
section) · native/include/octavox/harmony.hpp + native/src/harmony.cpp (the
implementation: mapRow / mapChoice incl. the authored scal-mux irregularity
and the chromatic/whole-tone raw-index switch, verbatim) ·
evidence/analysis_round21/r21_harmony_vectors.json (545 vectors incl. 10
closed-form musical anchors + determinism duplicates) · the CLI verifier
`octavox_r21_harmony --verify` = **545 passed, 0 failed, worst relative
deviation 0.0** (the C++ and the independent Python generator agree
bit-exactly). NOT host-exposed; parameter ids 0–37 untouched.

## P3 — Packaging hygiene (the stale manifest)

The R20 archive's manifest situation (audited at round start, exactly as the
specification stated): 581 files on disk vs 531 manifest entries — 518 matched,
12 stale (the R20-modified sources), 1 missing (poc/poc_run.log), 50 unlisted
(the R20 additions). Resolution: the manifest is REGENERATED from the actual
extracted tree; the 12 modified files keep their new (correct) hashes; the
missing poc_run.log is recorded as DELETED (NOT silently restored); the 50+
new R21 files are listed. The fresh-extraction verification (exact file count,
zero stale, zero missing, zero unlisted, verified archive SHA-256) is recorded
in checksums.txt's R21 section + the delivery notes.

## Environment (recorded honestly)

* **6th sandbox reset mid-round** (the whole r21 working tree was wiped after
  the P0 code changes were written): recovered by re-extracting the R20
  archive and re-applying every change (the idempotent re-apply scripts
  `octavox-work/r21/reapply_patch.py` + `reapply_patch2.py` are preserved in
  the work area, outside the evidence tree); cmake reinstalled via pip
  (4.4.4); a running state backup was kept thereafter.
* **Windows toolchain deviation:** llvm-mingw's Linux-hosted builds are no
  longer published (only Windows-hosted assets remain; wine/PE execution is
  blocked by the sandbox) — the R21 Windows modules are cross-built with the
  **Zig toolchain** (`zig c++ -target x86_64-windows-gnu`: clang/LLVM
  frontend, lld, mingw-w64 headers, static libc++), producing GENUINE PE32+
  x64 DLLs with the MS x64 ABI importing only KERNEL32/UCRT. Two SDK shim
  fixes were needed (the aligned_alloc include-order and the exec-order for
  moduleinfotool; both documented in the shim headers + this report). The
  Linux validation of every variant is unaffected (same sources, same
  variant defines).

## Artifacts & reproduction

* Linux core: `native/build_r21/octavox_tests` (96/96) + the R21 tools.
* Linux VST3 (4 variants): `native/build_vst3*/` — wrapper 21/21,
  integration 12/12, validator 47/47 each (logs:
  evidence/analysis_round21/logs/).
* Windows (3+1 variants): `native/build_win_*/VST3/Release/PitchLab_*.vst3`
  (PE32+ DLL, GetPluginFactory export, FUIDs verified in the Windows COM GUID
  layout, moduleinfo.json per bundle) + `octavox_dual_nulltest.exe` (the kit's
  user-runnable A/B/null test).
* Evidence: evidence/analysis_round21/ (r21_static_findings.md,
  r21_harmony_*.json/py/log, r21_stereo.json, r21_fmin.json,
  r21_realaudio_demo.json + realaudio_demo/ + traj/ + logs/ + the scans).
* Docs: PITCHLAB_ROUND21_FINDINGS.md (this file), docs/R21_HARMONY_MAPPER_SPEC.md,
  docs/R21_FEATURE_RECONSTRUCTION_MATRIX.md, docs/R21_DUALMONO_STEREO_CORRECTION.md,
  the R20_PARAMETER_SEMANTICS.md R21 addendum, the R20 findings correction
  banner.
