Evidence organization:
  linux_build/      R24-v2 configure + build logs (native Release/ASan, nine VST3 variants)
  vst3_validation/  wrapper 22/22, module integration 12/12 (real dlopen), SDK validator 47/47 per variant; dual null test
  regression/       R24-v2 native suite (110/110) + the EIGHT hash gates under the Auto default
  sanitizers/       ASan/UBSan suite run (110/110, zero reports)
  benchmarks/       13-case bench at Scalar-forced and at the Auto production default
  corrections_v2/   baseline record, class-ID audit, binary audits, audit tools
  R24_original/     the ORIGINAL R24 analysis-round evidence (g0..g6), preserved unmodified

The full R23 evidence archive (round 23 delivery) remains the separate
Octavox_RE_evidence_round23.zip and is NOT redistributed inside this package
(see research/R23/ for the R23 research documents and REFERENCES.md).
