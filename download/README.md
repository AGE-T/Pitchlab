# Downloads

| file | size | SHA-256 |
|---|---|---|
| `Octavox_RE_evidence_round12.zip` | 1 776 634 B (~1.69 MB) | `a597546534c3f27bd941dea4a427ececbd9972e63b1fa183485eba509cdb0902` |
| `Octavox_RE_evidence_round12.zip.sha256` | sidecar checksum | — |
| `Octavox_RE_evidence_round11.zip` | 1 672 425 B (~1.59 MB) | `6ff8173a9e98e41092d762f32862ad88dd399291a6019cc29e71582194cf215b` |

Served from `public/` — download from the app root page (`/`) or directly at
`/Octavox_RE_evidence_round12.zip`. The same files are mirrored here in `download/`.

**Package:** round-12 edition of the Octavox RE evidence package (285 files + `checksums.txt`,
286 zip entries). New since round 11: `docs/OCTAVOX_ROUND12_FINDINGS.md` (static advance —
the interpolator is a static 256×6 `.rdata` kernel that is **not** Lagrange; the settled path
reads ONE interpolated tap, so the target cannot sit in a two-tap blend at unity; crossfade
mechanics decoded; Q-14's premise answered), `native/` (the independent C++17 DSP core —
`delayline_aligned` ported with the unity engagement fix: **bit-exact delayed copy at exact
unity, 0.0000 dB comb spread vs the Python v2 reference's 16.43 dB**; 51/51 release tests,
51/51 ASan+UBSan, ≈ 35–55× the Python reference's speed; CMake + tests + bench + CLI +
architecture note), `evidence/analysis_round12/` (4 read-only capstone scans of the shipped
binary + the bit-exact OBS kernel extraction + native test/bench/metrics results). The
round-11 suites re-run from the package: numeric audit exit 0, acceptance 45/45. Verify after
extraction: `sha256sum -c checksums.txt` (285/285 OK expected); build and test the native core:
`cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release && cmake --build native/build -j &&
./native/build/octavox_tests` (51/51, exit 0).

The target binary `Ox.vst3` is intentionally not included (see `DELIVERY_ROUND12.md`; it was
read-only-analysed this round — digest verified before and after every scan — but never
executed). Earlier editions are preserved under `/home/z/octavox/delivery/` (round 11
SHA-256 `6ff8173a…94cf215b`, round 10 `e7f9067a…3626dd90`).
