# Octavox RE evidence — delivery downloads

| round | package | SHA-256 |
|---|---|---|
| 8 (handover) | Octavox_RE_evidence_round8.zip | 04fe8f948e20812f533d3a4e32bbb8bac483593cea80451c8f8aa4875f33d445 |
| 10 | Octavox_RE_evidence_round10.zip | (see the .sha256 sidecar) |
| 11 | Octavox_RE_evidence_round11.zip | 6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b |
| 12 | Octavox_RE_evidence_round12.zip | a597546534c3f27bd941dea4a427ececbd9972e63b1fa183485eba509cdb0902 |
| 15 | Octavox_RE_evidence_round15.zip | 990b1d48cff8aa69186cd6855d1512abacbfe829a2924a0d6a7934affc7208ec |
| **16 (latest)** | **Octavox_RE_evidence_round16.zip** | **c6a022ed7f19b9cc8dd56a98778c71583cad5d98b9a6801eaf722e145161d519** |

Round 16: evidence reconciliation (16 tagged corrections, history preserved),
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
