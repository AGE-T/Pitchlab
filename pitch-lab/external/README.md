# Vendored third-party code (architecture §J)

This directory holds vendored third-party sources — one sub-directory per
library. **At the implementation freeze it is EMPTY by design: the CI build
has zero third-party dependencies.**

Vendoring rules (architecture §J; enforced for every future vendoring):

1. One directory per library: `external/<name>/`.
2. `LICENSE.txt` copied **verbatim** from the upstream release used.
3. `ORIGIN.toml` recording: exact version/commit, source URL, retrieval
   date, licence verification note (who checked what, against which file),
   and the intended purpose in Pitch Lab.
4. Licence must be permissive (BSD/MIT/BSL/ISC-class). GPL/LGPL/AGPL code is
   never vendored here (subprocess adapters only, post-v0.1).
5. Licence status recorded in the research report is RE-VERIFIED at
   vendoring time (licenses change; research verification is dated).
6. Re-vendoring = new `ORIGIN.toml` entry; silent version drift is forbidden
   (Operating Principles §123 — locked dependency state).

Planned v0.1 implementation-phase vendoring (audited in the build & CI
environment specification §7):

| Library | Purpose | Licence | Consumer |
|---|---|---|---|
| pocketfft | FFT for the phase-vocoder engines + STFT analysis helpers | BSD-3-Clause (re-verify at vendoring) | `native.pv.classic`, `native.pv.phaselocked`, analysis STFT |
| doctest | unit/contract test framework | BSL-1.0 | `pitch-lab/tests/` |

Explicit non-dependencies: FFTW (GPL), libsndfile (own WAV I/O), JUCE
(future VST project only), Python anything (owner lock L-1).
