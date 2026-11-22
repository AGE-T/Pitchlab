# PITCHLAB — RESEARCH REFERENCES (citations only; no copyrighted content is redistributed)

All external material below is referenced by citation/URL only. The R24-v2
package contains exclusively our own implementation, measurements, and
documentation. No third-party copyrighted text, code, or media is included.

## Pitch detection / YIN

* A. de Cheveigné, H. Kawahara: **"YIN, a fundamental frequency estimator
  for speech and music."** JASA 111(4):1917–1930, 2002.
  DOI: 10.1121/1.1458024.
  (The difference function, cumulative mean normalized difference (CMNDF),
  and the thresholded absolute-minimum search used by the engine's detector
  follow this paper's formulation; our derivation record:
  `research/R23/PITCHLAB_R23_YIN_MATHEMATICAL_DERIVATION.md`.)
* A. de Cheveigné: "Pitch detection algorithms." UNIGE/IRCAM training
  material (historical context for the autocorrelation family).
* Theparabolic interpolation of the CMNDF minimum follows the standard
  3-point vertex fit (see the derivation record above for our exact
  algebra).

## Interpolation / fractional delay lines

* T. I. Laakso, V. Välimäki, M. Karjalainen, U. K. Laine: **"Splitting the
  unit delay — tools for fractional delay filter design."** IEEE Signal
  Processing Magazine 13(1):30–60, 1996. DOI: 10.1109/79.482137.
  (6-point Lagrange interpolation and its group-delay/error properties.)
* V. Välimäki, T. I. Laakso: "Fractional Delay Filters — Design and
  Applications." Chapter 3 in Nonuniform Sampling: Theory and Practice,
  Kluwer, 2001.

## Real-time safety / plugin architecture

* Timur Doumler: "Real-time audio programming: the audio callback."
  Meeting C++ / ADC talks (allocation-free audio-thread discipline as
  practised by the engine's process() contract).
* Ross Bencina: "Real-time audio programming 101: time waits for nothing."
  (Classic statement of the no-locks/no-allocation audio thread rules.)

## SIMD / numerical reproducibility

* Intel 64 and IA-32 Architectures Optimization Reference Manual
  (AVX2/FMA latency/throughput characterisation context for §2 of the
  benchmark record).
* D. Goldberg: "What Every Computer Scientist Should Know About Floating-
  Point Arithmetic." ACM Computing Surveys 23(1):5–48, 1991
  (reassociation/rounding semantics underlying the bit-exactness rules).
* GCC documentation: `-ffp-contract`, `-mavx2`, `-mfma` (per-translation-unit
  ISA isolation and FMA contraction control).

## VST3

* Steinberg VST3 SDK v3.7.12_build_20 (GPLv3 dual-licensed; obtained by
  `tools/get_vst3_sdk.sh` with pinned SHAs — see
  `source/vst3/VST3_WRAPPER.md` for the acquisition record). The SDK is NOT
  redistributed inside this package; the pinned-SHA script is.
* Steinberg: "VST 3 Developer Portal" — bundle layout, moduleinfo, and the
  conformance validator (47-test suite executed per delivered bundle).

## Project-internal source records (shipped in this package)

* R23 round documents: `research/R23/` (YIN implementation spec, variant
  taxonomy, decimation filter study, tracking-control spec, benchmark
  protocol/results, decision record, …).
* R24 round documents: `research/R24/` (SIMD capability-layer findings,
  measured benchmark decision record, R21 identity-lineage findings).
* R24-v2 correction records (this delivery): package root —
  `LINUX_COMPATIBILITY_REPORT.md`, `VST3_IDENTITY_DECISION_RECORD.md`,
  `SIMD_DEFAULT_DECISION_RECORD.md`, `DELIVERY_LINUX_R24_V2.md`.
