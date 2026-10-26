# Octavox RE evidence — delivery downloads

| round | package | SHA-256 |
|---|---|---|
| 8 (handover) | Octavox_RE_evidence_round8.zip | 04fe8f948e20812f533d3a4e32bbb8bac483593cea80451c8f8aa4875f33d445 |
| 10 | Octavox_RE_evidence_round10.zip | (see the .sha256 sidecar) |
| 11 | Octavox_RE_evidence_round11.zip | 6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b |
| 12 | Octavox_RE_evidence_round12.zip | a597546534c3f27bd941dea4a427ececbd9972e63b1fa183485eba509cdb0902 |
| **15 (latest)** | **Octavox_RE_evidence_round15.zip** | **990b1d48cff8aa69186cd6855d1512abacbfe829a2924a0d6a7934affc7208ec** |

Round 15: the eight-voice harmonizer API completed (per-voice pitch ratio / gain / mute /
base delay, wet/dry mix with the latency-aligned dry path, click-free bypass, block-boundary
parameter semantics) — 85/85 acceptance cases (release + ASan), default path bit-identical
to 12.0.0 (four reference hashes); static: Q-A/Q-B/Q-C closed (dead record field; the
+0x5F14 cadence selector {2,4,8}; the kernel table's second consumer = the output-stage
"just for the filter" fractional-delay modules), Q-D open with a plan. VST3 readiness
assessed; no wrapper built by design. Includes the rounds-13/14 scope rebuilt as the 14.0.0
recovery record after two documented environment resets (provenance inside).

Verify: `sha256sum -c Octavox_RE_evidence_round15.zip.sha256` then the instructions in
DELIVERY_ROUND15.md inside the package.
