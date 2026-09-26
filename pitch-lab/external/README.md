# Vendored third-party code (architecture §J)

This directory holds vendored third-party sources — one sub-directory per
library. **It was empty by design at the implementation freeze; cycle 1
(2026-09-26, implementation spec §17 step 1) vendored `doctest` and cycle 4
(2026-09-26, §17 step 4) vendored `pocketfft` per the documented plan below.**
Vendoring rule (build & CI environment specification §7): `doctest` is
dev/test tooling only, never linked into DSP runtime code; `pocketfft` is
the ONE documented exception — it is linked into the DSP runtime (the
phase-vocoder engines) as "use: lab binary" per that same plan. The DSP
core is otherwise zero-dependency.

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

Vendored so far:

| Library | Vendored | Version | Licence (vendoring-time verified) | Purpose |
|---|---|---|---|---|
| doctest | 2026-09-26 (cycle 1) | 2.4.12 | **MIT** (see `doctest/ORIGIN.toml` — the previously recorded "BSL-1.0" was an empirical documentation error, corrected at vendoring-time re-verification) | unit/contract test framework |
| pocketfft | 2026-09-26 (cycle 4, §17 step 4) | cpp-branch commit `c90e55b3d529f8efa40ed01a20de22405f45fc65` (2026-06-30) | **BSD-3-Clause** (verified against BOTH the LICENSE file and the header's own licence block — see `pocketfft/ORIGIN.toml`) | FFT for the phase-vocoder engines (DSP runtime — the documented "lab binary" exception) + future STFT analysis helpers |

Still planned (build & CI environment specification §7): nothing further in v0.1.

Explicit non-dependencies: FFTW (GPL), libsndfile (own WAV I/O), JUCE
(future VST project only), Python anything (owner lock L-1).
