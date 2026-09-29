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
| vst3sdk | 2026-10-01 (product phase) | 3.7.14 (tag `v3.7.14_build_55`, commit `43b4e366`; VSTGUI commit `b1e82355`) | **Steinberg VST3 dual licence — used under the PROPRIETARY VST3 licence path, NOT GPLv3** (see `vst3sdk/ORIGIN.toml`); VSTGUI inside the tree is BSD-3-Clause | the official Steinberg VST3 SDK + VSTGUI for the VST3 product layer (plug-in, editor, validator); never linked into the v0.1 DSP core |

Still planned (build & CI environment specification §7): nothing further in v0.1.

## Product-phase vendoring exception (recorded 2026-10-01)

Rule 4 above (permissive-only) has ONE documented exception, recorded in
`vst3sdk/ORIGIN.toml` and in
`research/pitch-lab-vst3-product-phase-specification.md` §5: the Steinberg
VST3 SDK is dual-licensed (GPLv3 OR the proprietary Steinberg VST3 licence
agreement — `vst3sdk/LICENSE.txt` + `VST3_License_Agreement.pdf`, verbatim
in the tree). Pitch Lab uses it under the **proprietary VST3 licence**
(the intended licence for closed-source VST3 plug-ins) — the GPLv3 path is
explicitly NOT taken, and no GPL code enters the DSP runtime or the product
binary through this tree. VSTGUI (the `vstgui4/` subtree of the same
vendored tree) is BSD-3-Clause. The v0.1 research DSP core remains
zero-dependency and links nothing from `vst3sdk/`.

Explicit non-dependencies: FFTW (GPL), libsndfile (own WAV I/O), Python
anything (owner lock L-1). JUCE remains a non-dependency per the explicit
product-phase architecture decision (official Steinberg VST3 SDK + its
own VSTGUI integration instead — the owner directive pins this).
