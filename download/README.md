# Octavox RE evidence — delivery downloads

| round | package | SHA-256 |
|---|---|---|
| 8 (handover) | Octavox_RE_evidence_round8.zip | 04fe8f948e20812f533d3a4e32bbb8bac483593cea80451c8f8aa4875f33d445 |
| 10 | Octavox_RE_evidence_round10.zip | (see the .sha256 sidecar) |
| 11 | Octavox_RE_evidence_round11.zip | 6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b |
| 12 | Octavox_RE_evidence_round12.zip | a597546534c3f27bd941dea4a427ececbd9972e63b1fa183485eba509cdb0902 |
| 15 | Octavox_RE_evidence_round15.zip | 990b1d48cff8aa69186cd6855d1512abacbfe829a2924a0d6a7934affc7208ec |
| 16 | Octavox_RE_evidence_round16.zip | c6a022ed7f19b9cc8dd56a98778c71583cad5d98b9a6801eaf722e145161d519 |
| 18 | Octavox_RE_evidence_round18.zip | 35a855e77925e51027416b5d2c68ab5a4fc7f0ceb6eb9bea762864c0af02187a |
| **19 (latest)** | **Octavox_RE_evidence_round19.zip** | **c0a7ee46ca1646eb0735fe20ae5a476f753ee67a98aeb0164a09de74505227da** |
| **19 Windows dual build** | **OctavoxRE_R19_Windows_x64_Dual_Build.zip** | **a0f02b994aa86ebfa9023207f206c03890940234007c747d57519a34e07eee73** |
Round 19: the real-time fix (DetectorMode::Decimated — a properly low-pass
filtered, decimated control-rate detector near 24 kHz with the FullRate
reference mode retained bit-exact; 96 kHz stereo 8 voices drops from
100.6% to 13.2% CPU = 86.8% headroom, worst battery error 0.19 cent), the
click investigation (the 320-render offline continuity suite with
sample-accurate wrap/commit timestamps; the initial-lock / retune /
base-delay / voice-count pops are eliminated 32-67x by the crossfaded
transitions; the wet/dry instant mix step is bounded), the read suppression
(bit-identical, test-gated), and the dual-interpolation verification
(engine AND module level: unity bit-identical, -24 st a genuine -38.5 dB
RMS difference — the "sounds identical" perception quantified; Windows
modules proven to select their kernels via 3 distinct hashes + the
user-runnable dual null test). Suite 85 -> 93 cases; release 93/93 and
ASan+UBSan 93/93 with zero sanitizer reports; the 15.0.0 behaviour still
reproduces bit-exactly under the legacy pins.

Verify: `sha256sum -c Octavox_RE_evidence_round19.zip.sha256` then the
instructions in DELIVERY_ROUND19.md inside the package.
 evidence reconciliation (16 tagged corrections, history preserved),
reproducible validation workflow (per-test durations, filters/groups, machine-
readable JSON with environment records; release 85/85 in 41.0 s, ASan+UBSan
85/85 in 6.16 min — the ">5 min" report resolved), 96 kHz profiling (detector
83-95 % share; deliberate no-change decision; 11.9x margin via the tested
detection_interval knob), and the REAL VST3 wrapper milestone: the module
OctavoxNative.vst3 (native/vst3/, core untouched) passes the VST3 SDK's own
validator 47/47 and the 8-case wrapper suite 8/8 — sample-accurate automation
proven bit-exact vs the core's block-boundary implementation, state
save/restore bit-exact, 0 allocations in process(). SDK acquisition record
with pinned SHAs inside (the SDK itself is not redistributed).

Verify: `sha256sum -c Octavox_RE_evidence_round16.zip.sha256` then the
instructions in DELIVERY_ROUND16.md inside the package.
