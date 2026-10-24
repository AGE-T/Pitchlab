# Downloads

| file | size | SHA-256 |
|---|---|---|
| `Octavox_RE_evidence_round11.zip` | 1 672 425 B (~1.59 MB) | `6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b` |
| `Octavox_RE_evidence_round11.zip.sha256` | sidecar checksum | — |

Served from `public/` — download from the app root page (`/`) or directly at
`/Octavox_RE_evidence_round11.zip`. The same files are mirrored here in `download/`.

**Package:** round-11 edition of the Octavox RE evidence package (255 files + `checksums.txt`,
256 zip entries). New since round 10: `docs/OCTAVOX_ROUND11_FINDINGS.md` (the independent
numerical audit — all six round-10 claims reproduced, the max-error location and residual
direction corrected, the one-`P`-ulp excess qualification; the demo validation with the
stereo defect found and fixed; the acceptance suite and the period-aligned improvement),
`evidence/analysis_round11/` (numeric audit + 45/45 gated acceptance tests + improvement note),
`poc/octavox_v2.py` + the aligned example output; 10 files updated (7 canonical docs + the
corrected `r10_verify.py` with its regenerated output; the original output preserved as
`r10_verify_results_v1_superseded.txt`). Verify after extraction:
`sha256sum -c checksums.txt` (255/255 OK expected); re-run the gates:
`python3 evidence/analysis_round11/r11_numeric_audit.py` (exit 0) and
`python3 evidence/analysis_round11/r11_acceptance_tests.py` (45/45, ~2.5 min).

The target binary `Ox.vst3` is intentionally not included (see `DELIVERY_ROUND11.md`; it was
also not present in the round-11 environment — all inputs are the recorded OBS bit patterns).
The round-10 edition is preserved at `/home/z/octavox/delivery/Octavox_RE_evidence_round10.zip`
(SHA-256 `e7f9067ababaa033493b7a08730ffbc279ca2aa5295b246831daab6bf2237d9a`).
