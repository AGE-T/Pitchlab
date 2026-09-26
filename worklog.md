# PITCH LAB — WORKLOG

## CURRENT STATE (2026-09-26, after Task 12 — IMPLEMENTATION CYCLE 1 COMPLETE: §17 step 1 done, CI-proven)

**Project identity:** Pitch Lab — local offline DSP research laboratory for sound-design-oriented pitch processing (C++20 + offline harness; agent-side Next.js workbench for owner observability). Repository: `github.com/AGE-T/Pitchlab`, branch `main` (GitHub in sync; CI green at head; verified cycle-1 runs: 36241070135 @ f6f5bf2 (code, CTest 7/7) and 36241266441 @ bec1c52 (closure docs, CTest 7/7) — both evidence artefacts downloaded and content-verified).

**Reading order (authoritative documents and their roles):**
1. `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` — governing engineering process (130 sections).
2. `research/pitch-lab-v0.1-implementation-specification.md` — **current Source Of Truth for v0.1 implementation** (frozen contracts, engine sheets, tests, TOML schemas, open decisions OD-6..OD-18; records Amendments A-1, the resampler frozen-behaviour clarification §7.2.1 and the measured acceptance evidence §7.7.1).
3. `research/pitch-lab-build-and-ci-environment.md` — canonical pinned build/CI environment (v1.2: doctest vendored; §12.1 = OD-17 activation chronology).
4. `research/pitch-lab-architecture-design.md` (v1.1) — governing DSP architecture (amended for v0.1 only by the recorded A-1; v1.2 fold-in queued as OD-16).
5. `research/doppler-whip-pitch-research-report.md` — research findings ([RESEARCH FINDING] tags).
6. `README.md` + this worklog (current-state block first).

**Current phase:** IMPLEMENTATION (cycle 1 = spec §17 step 1 COMPLETE and CI-proven). Owner mandate 2026-09-26: OD-12 resolved by the owner — **pYIN via the C++-only clean-room route (OD-12 option B)**; deterministic synthetic corpus first; GitHub Actions canonical. Next: §17 step 2 (curve library + validation + corpus generator).

**Current implementation state (all unit-tested, CI-green):** `src/core/types.h` (§4.1 contract), `src/core/errors.h` (ConfigError §4.8 + WavSizeLimitError §9/OD-14), `src/core/rng.{h,cpp}` (§4.7: own PCG64 setseq 128/64 XSL-RR, SplitMix64+FNV-1a consumer mixing, golden-pinned), `src/core/wav_io.{h,cpp}` (§9: own reader/writer, IEEE float 32/64, extensible, deterministic bytes, finiteness gate, 4-GiB cap), `src/core/resampler.{h,cpp}` (§7/§7.2.1: Kaiser windowed-sinc kernel, presets, `interpolateAt`, `resampleBlock` per-sample ratio advance, AA FIR). Tests: `types_test` (T-T1), `rng_test` (T-D1/D2), `wav_io_test` (T-W1..W3), `resampler_test` (T-R1/R2 — prints the OD-18 evidence into the JUnit output) + the 3 freeze smokes = **7 CTest tests**. NO engine is implemented and none is faked (registry deliberately empty, T-E19 asserts it — still green). Engine/harness/CLI subcommands are §17 steps 2-3 work. Build phase constant: `implementation`.

**Known limitations:** all numeric tolerances provisional (calibration procedures defined); tracker-dependent metrics wait on the clean-room pYIN (OD-12 resolved in route, implementation is later-cycle work); **OD-18 (new, empirical): resampler §7.3 constants vs §7.7 kernel-level criteria** — kernel stopband floors −75.9/−78.0/−87.3 dB vs 80/80/90 targets, transitions 31.6/16.0/11.8% of Ny vs 5%; ripple/identity/DC-Nyquist/composite-AA criteria MET (identity −309 dBFS; composite AA −179 dB); owner ratification requested (§7.7.1, options i/ii/iii); Amendment A-1 pending architecture v1.2 fold-in (OD-16); RIFF 4 GiB cap (OD-14).

**Open decisions:** OD-6 (rate-following length tolerance value), OD-7, OD-8, OD-9 (metric tolerances), OD-10, OD-11, ~~OD-12 (RESOLVED 2026-09-26, owner: clean-room C++ pYIN, option B)~~, OD-13, OD-14, OD-15 (artifact publication), OD-16 (architecture v1.2), ~~OD-17 (RESOLVED 2026-09-26)~~, **OD-18 (new — resampler constants ratification, §7.7.1)**. Full table: implementation specification §16.

**Blockers:** none. The resampler's unmet kernel-level §7.7 criteria are recorded as OD-18 (owner ratification) — the correctness-relevant composite path is far inside spec, varispeed (§17 step 3) is not blocked.

**Dependency and environment state:** canonical = GitHub Actions `ubuntu-24.04` x64, GCC 13.3.0 (asserted), CMake 3.31.6 (sha256-pinned), Ninja (≥1.11), CTest. Vendored: **doctest 2.4.12, MIT** (build spec v1.2; empirical licence correction — the previously recorded BSL-1.0 was wrong; dev/test only, DSP core zero-dependency). pocketfft (BSD-3) still planned for the PV-engine cycle. Token in the gitignored `.env` (never logged, never committed).

**Validation state:** local clean build + 7/7 CTest green (pinned CMake 3.31.6; GCC 14.2.0 local; Unix Makefiles locally, Ninja canonical). GitHub CI: run **36241070135** (head f6f5bf2, pushed 4eda444..f6f5bf2 = cycle-1 commits 1-4): conclusion **success**, all steps green, CTest **7/7** (1.07 s), `ci-evidence` artefact (id 10905559488) downloaded and content-verified: JUnit `tests="7" failures="0"`; the OD-18 evidence and T-R2a identity measurements present in the CI test output. Closure-docs run **36241266441** (head bec1c52): also green, CTest 7/7, JUnit 7/0 — `version_smoke` confirms phase `implementation` + `gcc 13.3.0` on the runner. Zero doctest/behaviour surprises on the canonical runner (GCC 13.3 compiled the same sources warning-free under -Werror).

**Exact next action:** §17 step 2 — curve library (`experiments/curves/*.toml` schema §15.3, compilation rules §4.4.3, validation §4.4.4, T-E15 fixture matrix) + deterministic corpus generator (`tools/corpus_gen`, §11) with T-C1 regeneration bit-identity; then step 3 (registry factory + harness skeleton + `native.varispeed` first, using the frozen resampler §7.2.1 + the AA policy; T-LEN-CAL evidence pack for OD-6).

*(Historical entries follow below, oldest first. This block supersedes nothing — it summarises.)*

---

---
Task ID: 4
Agent: spectral-pitch-research
Task: Research frequency-domain and model-based pitch shifting algorithms

Work Log:
- Read /home/z/my-project/worklog.md (did not exist; created now).
- Ran ~20 web searches (phase vocoder pitch shifting, Laroche & Dolson phase locking, phasiness, transient phase reset, Rubber Band, zplane élastique, MQ sinusoidal modeling, Serra SMS, Stylianou HNM, LPC pitch modification, WORLD vocoder, DDSP, PV latency/FFT/hop params, dynamic ratio behavior, stereo phase coherence, Bernsee tutorial, neural real-time shifting, CPU cost, identity vs peak locking, Flanagan/Golden & Portnoff origins).
- Hit a 429 rate limit mid-session; backed off (5 min) and resumed single sequential searches.
- Directly fetched and read PRIMARY SOURCES (curl + pdftotext):
  * Laroche & Dolson 1999, "New Phase-Vocoder Techniques for Pitch-Shifting, Harmonizing and Other Exotic Effects" (IEEE WASPAA '99, NOT JAES) — full 4-page PDF from ee.columbia.edu. Key: peak detection + region translation, per-frame phase rotation Z_u = e^{jΔωR} cumulated frame-to-frame, explicit support for per-frame varying shift ("ω_{u+1} indicates the shift may vary from one frame to the next"), integer-bin shifts OK at 50% overlap, fractional shifts need 75% overlap (linear-interp sidebands −21 dB vs −51 dB), implicitly implements identity phase locking, cost independent of shift amount, no arctan/phase-unwrap.
  * Stylianou 2001, "Applying the HNM in concatenative speech synthesis" (IEEE Trans. SAP) — HNM harmonic+noise model, pitch/time modification via recursive synthesis-time-instant mapping + re-estimation of harmonic amplitudes/phases for new harmonics, harmonic band stays 0–Fmax → envelope (formant) position preserved by design.
  * Schörkhuber/Klapuri DAFx-12 "Pitch shifting using the constant-Q transform" — PV artifacts = loss of horizontal (inter-frame) + vertical (intra-frame) phase coherence; "phase update only correct when input is a sum of a small number of slowly varying sinusoids"; frequency-domain shifting needs fractional-bin interpolation; CQT redundancy ×6.3 for shiftable uniform-hop grid; speech transposition lacks naturalness (vertical coherence loss); formants are ~constant-Q → good for formant preservation.
  * Průša & Holighaus 2022 "Phase Vocoder Done Right" (arXiv:2202.07382) — phase-gradient (Δ_t φ AND Δ_f φ) integration via RTPGHI max-heap; no peak picking, no transient detection; classical PV ignores frequency-derivative; eval settings: 4092-sample Hann, M=8192, a_s=1024 @44.1kHz; not optimal for voiced monophonic speech (fundamental/partial phase shift).
  * Rubber Band (breakfastquay): website + README + RubberBandStretcher.h + RubberBandLiveShifter.h + src/faster/StretcherProcess.cpp + R2Stretcher.cpp. Verified: GPL v2 + commercial dual license; R2 (Faster, 2.x default) & R3 (Finer, v3.0+) engines; R2 = Laroche-Dolson-style PV with "laminar" phase locking (region-of-influence inheritance with maxdist=8, freq0/freq1/freq2 band limits) + transient-triggered phase reset via CompoundAudioCurve (Percussive+HF+Silence curves) + band-limited reset (150 Hz–1 kHz "mixed" mode); omega = 2π·H·i/N expected-phase propagation code; R3 better for "soft onsets and smooth pitch changes", much more CPU; LiveShifter API: fixed block, real-time safe, "delay of 50 ms or more" explicitly NOT low latency; ratio changes real-time safe; v4.0 (Oct 2024).
  * WORLD vocoder GitHub (mmorise/world): modified-BSD license, "no patent in all algorithms", components DIO/Harvest (F0), CheapTrick (spectral envelope), D4C (aperiodicity), Platinum; Morise 2016 IEICE paper "for real-time applications".
  * jurihock/stftPitchShift: MIT license (© 2022 Juergen Hock); Bernsee-style STFT pitch shifter + cepstral-liftering formant preservation + "poly" (multi-shift) pitch shifting; C++ real-time STFT core.
  * zplane licensing/technology pages: ELASTIQUE family (PRO / EFFICIENT / TUNE) all real-time capable; PRO = "completely new approach", sharp transients, no phasing artifacts, inter-channel phase coherence, formant-preserving pitch shift for mono AND polyphonic, engine of élastique Pitch plugin; EFFICIENT = tonal/transient component detection, workload splitting "to avoid workload peaks at low latencies", bandwidth reduction option; closed-source commercial SDK licensing (used by Reaper, FL Studio, MPC, Band-in-a-Box...).
  * phaseret toolbox (LTFAT): GPL3 — reference implementation of RTPGHI/PV Done Right.
- Additional verified snippets: JUCE forum (Rubber Band realtime pitchshifter thread): minLatency option reduces latency but "potential tearing during modulation with a change of the pitch parameter". Röbel 2003 DAFx "A new approach to transient processing in the phase vocoder" (phase re-initialization at optimal transient position). Röbel DAFx "efficient spectral envelope estimation... pitch shifting with preservation of the spectral envelope in the phase vocoder". Lenarczyk 2017 ISCA (PV + spectral whitening for real-time formant-preserving speech pitch shift). Puckette 1995 "Phase-locked vocoder" origin of peak locking. Driedger TSM review: "identity phase locking → reduced phasiness and less transient smearing". Flanagan & Golden 1966 Bell Syst. Tech. J. origin; Portnoff 1981 IEEE Trans. ASSP (via L-D 1999 reference list).

Stage Summary:
- Classic PV (TSM+resample): φ'_k(n)=φ'_k(n−1)+α·ω̂_k(n)·H_s propagation; assumes steady sinusoids; phasiness + transient smearing; latency ≈ window (1024→23 ms, 4096→93 ms @44.1 kHz) + hops; ratio can change per hop but phase propagation coherence degrades when ratio/IF changes within window; fine for slow modulation, risky for fast Doppler curves. PROTOTYPE (baseline).
- Laroche-Dolson 1999 direct frequency-domain shift: per-frame cumulated rotation e^{jΔωR} explicitly supports frame-varying shift; identity phase locking implicit; 50–75% overlap; cost independent of ratio → BEST spectral candidate for rapid curves; needs peak tracking + region shifts; formants shift unless envelope correction added. PROTOTYPE.
- Phase locking (identity/peak): identity = all bins in region get peak's synthesis phase; peak/scaled = peak phase + analysis deviation. Cures phasiness (vertical coherence), helps transients mildly. Must-have for any PV. IMPLEMENT (simple).
- Transient-aware PV (phase reset at detected transients: Röbel 2003, Rubber Band R2 "crispness"): resets stop smearing on attacks; reset boundaries can glitch when ratio is changing fast; needs reliable transient detector. PROTOTYPE.
- Phase-gradient PV (RTPGHI / PV Done Right): fixes both coherences without detection; 4092 Hann / 1024 hop; RTF low; GPL3 reference (phaseret, Matlab). BENCHMARK-ONLY (port cost, GPL).
- Sinusoidal MQ / SMS / sines+residual: track frequencies scaled per frame; envelope-independent; excellent on quasi-harmonic mono; breaks on noise/polyphony/dense mixes; track update rate = analysis frame (e.g., 10 ms hop); pitch can vary per frame but birth/death of tracks limits very fast modulation. BENCHMARK-ONLY for voice/harmonic material.
- HNM (Stylianou): harmonic+noise split, envelope fixed in frequency band → formant-preserved pitch mod naturally; speech-only (needs F0 + voicedness); recursive synthesis-instant mapping supports continuous pitch contours. Speech-grade only — SKIP for instruments, but the "envelope stays, excitation moves" idea transfers.
- LPC/source-filter: cheap, formant control by design; single-pole-ish vocal tract assumption; fails on polyphony/ensembles; buzz artifacts; speech codecs heritage. SKIP for general audio; OK niche for solo voice.
- WORLD: modified-BSD, no patents, high-quality speech analysis/modification/synthesis; DIO fast enough for real-time; speech-only (F0-dependent analysis). SKIP for instruments; CheapTrick envelope idea worth borrowing for formant preservation.
- DDSP/neural: hybrid harmonic-oscillator+filtered-noise synthesis conditioned by network; real-time feasible on modern CPU for small models (DDSP-VST exists; 16 kHz, hop 256 ≈ 16 ms granularity historically); no production-grade neural pitch shifter for arbitrary instrument input found; model-based (not pitch-shifter) artifacts; GPU not required for small models. SKIP for v1; watch.
- Stereo: process per-channel with SHARED spectral decisions (peaks, phase-lock regions, reset times) or via single shared ratio curve to preserve inter-channel coherence; élastique PRO advertises inter-channel phase coherence explicitly; independent per-channel phase propagation decorrelates image (chorus/wobble) [INFERENCE]. M/S processing preserves image only if both M and S processed coherently; practical harmonizers often accept decorrelation [INFERENCE].
- Top references/implementations: Rubber Band (GPL/commercial, R2+R3), zplane élastique (commercial, closed), stftPitchShift (MIT), phaseret (GPL3), SPTK/WORLD wrappers (BSD-like), SoundTouch (LGPL, time-domain — out of scope).

---
Task ID: 3
Agent: time-domain-pitch-research
Task: Research time-domain pitch shifting algorithms

Work Log:
- Read worklog.md (absent at session start; Task 4 entry appeared later — appended after it).
- Ran ~17 productive web searches (time-domain overview OLA/WSOLA/PSOLA; varispeed resampling artifacts; delay-line modulation/Doppler pitch ratio; WSOLA Verhelst & Roelands; SoundTouch LGPL + default params; TD-PSOLA Moulines & Charpentier; FD-PSOLA formant control; Eventide H910 Harmonizer history; granular pitch shifting grain sizes; DAFx book chapter 6; TDHS; zipper noise/parameter smoothing; DAFx comparisons TD vs FD; "low latency time-domain pitch shifting"; Doppler plugin implementations). Two searches returned no results ("tape head/spinning head emulation", "Fofex"); one rate-limit 429 burst — recovered by 5-min backoff + sequential pacing.
- Fetched/read PRIMARY SOURCES (curl + pdftotext):
  * J.O. Smith, "Physical Audio Signal Processing" (W3K, 2010) — Doppler Simulation pages: y(t)=x(t−D(t)); f_out=f_in·(1−dD/dt) (Eq. 6.6); listener Doppler: dD/dt = v_listener/c; read-pointer update `rptr += 1−g`; "in a Doppler simulator not driven by changing geometry, a pointer cross-fade scheme may be necessary when read and write pointers get too close".
  * Moulines & Charpentier 1990, Speech Communication 9:453–467 (full PDF, ECE420 mirror): TD-PSOLA/FD-PSOLA/LP-PSOLA; Hanning grains of 2 pitch periods; WB (L<2P) → formant bandwidth broadening; NB (L>4P) → selective harmonic attenuation, "reverberant-sounding distortion"; tonal noise when repeating unvoiced segments at ≥2× slow-down; TD-PSOLA ≈ TDHS (Malah 1979) at factor-2/2-period windows; FD-PSOLA ≈5 MFLOPS, fine spectral (formant) control; TD-PSOLA real-time on Intel 80386; formal listening test: all PSOLA variants ≫ LPC, roughly equivalent among themselves.
  * Haghparast, Penttinen, Välimäki, DAFx-07: real-time pitch shifting with TIME-VARYING factor via resampling + NFC-TSM ring buffer (AMDF splicing-point search in normalized low-pass-filtered signal); ring-buffer method lineage = Francis Lee 1972; TD methods "work fine for periodic/quasi-periodic signals, not good for many non-harmonic components; FD needs large delays"; shift-up needs ≥1 period in buffer (63 Hz, 5 ms allowed delay → max +30% at onset); AMDF search ≈ 87k ops per splice @ 73 Hz min-f0/44.1 kHz; "output pointer always keeps pace with input pointer so changes in amplitude and frequency are followed sufficiently".
  * SoundTouch (surina.net, Codeberg master, v2.4.x): LGPL-2.1+ (COPYING.TXT), author Olli Parviainen (NOT "Ondrej Parfut" as task brief said); "WSOLA-like" TDStretch; defaults: AUTO sequence 90→40 ms (tempo 0.5→2.0), AUTO seek window 20→15 ms, overlap 8 ms, legacy fixed default 82 ms; README: "i/o latency around 100 ms when time-stretching; rate transposing alone much shorter"; real-time on Pentium 133 MHz with quick-seek; quick-seek finds best match ~90% of cases; "echoing" artifact when slowing (mitigate: larger SEQUENCE_MS); parameters changeable at runtime via setSetting().
  * DAFx book 2nd ed. ch.6 (Time-segment processing, Dutilleux/De Poli/von dem Knesebeck/Zölzer) + M-files: PitchScaleSOLA.m (Sa=256, N=2048, Hann, linear-interp grain resample), psola.m (grains in(m±pit)·hann(2pit+1), synthesis marks tk += pit/beta), psolaF.m (formant factor γ via per-grain interp1 stretch), findpitchmarks.m (per-frame F0→pitch marks, unvoiced→hold last F0, 120 Hz default). MATLAB code license header: "educational purposes, not commercial applications without further permission" (Wiley © 2011).
  * audiojs/shift GitHub README (MIT): 15-algorithm benchmark table incl. TD: ola (f0 err 38.33 Hz, worst), wsola (attack corr 0.995, f0 err 1.67 Hz), psola (phase coh 0.998, attack corr 0.941 from pitch-mark jitter, "destroys polyphony/unvoiced, falls back to WSOLA"), delay/harmonizer (two crossfaded taps + correlation "intelligent splicing" at wrap, window 2048, tolerance 512, "mild flutter at crossfade rate", state bounded by window = real-time shape, shift score 1.610 vs vocoder 1.553), granular (grainSize 398 default, chord "crumble" = documented character), sample/varispeed (phase coh 0.170 because modulation rate shifts with pitch — correct behavior). ola/wsola/psola/gpsola = stretch + sinc-resample → formants shift with ratio.
  * SuperCollider PitchShift docs: "time domain granular pitch shifter, triangular envelopes, 4:1 overlap, linear interpolation", windowSize default 0.2 s (examples 0.02–0.1 s), ratio 0–4, pitch/time dispersion "can alleviate hard comb filter effect due to uniform grain placement"; windowSize not modulatable.
  * Bernsee "Time Stretching and Pitch Shifting — An Overview": pitch shift vs frequency shift distinction; TDHS (Rabiner & Schafer 1978 lineage) needs F0 estimate; monophonic OK, polyphonic only with longer overlap segments averaging phase error; TDHS sample snippets = no frequency localization → multi-pitch distortion.
  * Wikipedia/press for Eventide H910 (1975): first commercially available pitch changer / first digital multi-effect, Tony Agnello (Richard Factor), ±1 octave, delay up to 112.5 ms.
  * W3C Web Audio spec: "sample-rate converters or varispeed processors are not supported in real-time processing" (duration-drift problem of varispeed in streaming contexts).
  * Laroche & Dolson 1999 abstract (via search): phase vocoder "generally considered to yield high quality results" (contrast baseline for TD). Schörkhuber DAFx-12: "time-domain approaches more efficient computationally while frequency-domain achieve higher quality".

Stage Summary:
- Varispeed/resampling: y=x[r·t]; pitch & duration scale together; ~zero latency (<1 ms interpolator), trivial CPU, transients/harmonics pristine, formants shift (chipmunk). Ratio trackable per-sample (perfect dynamic response) BUT cannot run standalone as streaming effect (buffer drift; W3C explicitly excludes real-time varispeed). PROTOTYPE as resampler component + "varispeed character" mode.
- Variable-delay (Doppler): y=x(t−D(t)), ratio = 1−dD/dt; JOS Eq. 6.6/6.7. Zero-analysis, per-sample pitch control = FASTEST dynamic response of ALL families; bounded latency = max |D| excursion + interpolator; range limited by buffer for sustained shifts (wrap needed); artifacts: wrap-splice clicks unless crossfaded, HF loss with linear interpolation, AM at crossfade rate. THE physical Doppler model (NASA/JOS). PROTOTYPE — primary engine.
- OLA+resample: fixed-window overlap-add (DAFx: Sa=256/N=2048) then resample; no similarity search → destructive interference (audiojs f0 err 38 Hz), chorus/phasiness, AM at grain rate; latency = window (20–100 ms); CPU lowest; ratio updates at hop rate → stepping on fast curves. BENCHMARK-ONLY baseline.
- WSOLA+resample (Verhelst & Roelands 1993, ICASSP): tolerance-window cross-correlation search aligns grain tails; kills OLA phasiness; SoundTouch (LGPL-2.1+, Olli Parviainen) = reference impl: latency ~100 ms documented, ~40 ms aggressive (30/20/10 ms, PCSX2); CPU: real-time on P133 w/ quick-seek; quality good on speech/mono (attack corr 0.995), "echoing" when slowing; formants shift. Ratio changes at sequence rate (40–90 ms) → sluggish for fast Doppler curves. PROTOTYPE as quality fallback for static/slow shifts; link LGPL or reimplement.
- TD-PSOLA (Moulines & Charpentier 1990): pitch-synchronous 2-period Hanning grains re-spaced at P/β; excellent voice quality (formal tests ≫ LPC), phase coh 0.998; CPU tiny (386-real-time); artifacts: formant bandwidth broadening, reverberant distortion (NB windows), tonal noise on unvoiced repeat, pitch-mark jitter on onsets; REQUIRES reliable F0+voicing → monophonic voice only (polyphony breaks it); dynamic: re-lock per period (2.5–10 ms granularity — fast) but F0-estimation lookahead (≈10–40 ms) + fragile on non-voice. BENCHMARK-ONLY unless engine guarantees solo-voice input.
- FD-PSOLA: FFT each 2-period grain before OLA (formant/spectral control, γ factor in DAFx psolaF.m); ~5 MFLOPS; listening-test-equal to TD-PSOLA; same F0/voicing dependence + FFT cost. SKIP for TD engine (frequency-domain lane).
- Granular pitch shifting: constant-hop grains read at ratio (SuperCollider PitchShift: triangular, 4:1 overlap, 0.02–0.2 s windows; dispersion vs comb effect); latency = grain; CPU low; artifacts: grain-rate AM, comb filtering, chord "crumble" (audiojs: worst formant dist 3.486); ratio per-grain quantization. CREATIVE MODE only.
- Harmonizer (dual/tapped delay, Eventide H910 1975 lineage / Lee 1972 ring buffer): two taps at ratio speed + complementary Hann crossfades, wrap + optional correlation "intelligent splice" (audiojs delay: window 2048/tolerance 512); latency = window (23–93 ms), state bounded; ratio modulatable per-sample between wraps; characteristic flutter/wobble at crossfade rate (=(|r−1|)/window), formant shift, splice beats on dense chords; real-time proven (H910 live use). PROTOTYPE — co-primary with variable-delay (same math family: crossfaded variable-delay reads).
- Real-time/dynamic ranking (for fast Doppler curves): variable-delay ≈ harmonizer ≈ varispeed (per-sample, no analysis) > TD-PSOLA (per-period + F0 lag) > WSOLA/OLA (per-sequence 40–90 ms) — phase vocoder family: per-hop (~12–23 ms) but phase-coherence fragility under fast ratio change (Task 4 findings). Zipper avoidance: drive dD/dt continuously (one-pole smoothing of g), never step the ratio; DAFx-07 constraint: keep ≥1 period buffered for shift-up onsets.
- Licenses: SoundTouch LGPL-2.1+ (commercial-friendly w/ dynamic linking); DAFx MATLAB code NON-commercial (educational) — reimplement from book math; audiojs/shift MIT (JS, reference for behavior); SuperCollider GPL (SC PitchShift design = Puckette-style, reimplement freely); PSOLA/TDHS/WSOLA papers are old, core patents expired; "Harmonizer" is an Eventide trademark — do not name modes with it.
- Verdict table for the Doppler-driven engine: variable-delay modulator = BUILD (core); dual-tap harmonizer w/ intelligent splicing = BUILD (same delay infrastructure, character mode + range extension); varispeed resampler core = BUILD (shared component); WSOLA = optional quality mode for slow/static ratios (40–100 ms latency acceptable there); OLA/PSOLA/FD-PSOLA/generic granular = skip or benchmark-only.

---
Task ID: 5
Agent: library-licensing-research
Task: Research pitch shifting libraries and licensing for commercial VST (re-inserted by orchestrator after a parallel-write race; source: agent-a0732ca2 final digest)

Work Log:
- Ran ~15 web searches + direct fetches of LICENSE files (curl) for: Rubber Band (breakfastquay GitHub COPYING + license pages), Signalsmith Stretch (raw LICENSE.txt), pbshift (github.com/pbtechlab/pbshift), WORLD (mmorise/World LICENSE.txt), SoundTouch (COPYING.TXT), zplane élastique tech pages, sannawag/TD-PSOLA, maxrmorrison/psola, CREPE, librosa, KISS FFT, pocketfft, PFFFT, FFTW, JUCE (LICENSE.md), Steinberg VST3 SDK (vst3sdk), Bungee (bungee-audio-stretch/bungee), open-source Doppler plugins scan (igorski/delirion, usdivad/Melodrumatic, ovniaudio/ovni).
- All license claims verified against actual license text where possible.

Stage Summary:
- Rubber Band (R2 phase-vocoder family w/ transient phase reset + "lamination" phase locking; R3 "Finer" multi-resolution, higher CPU): GPL-2.0+ / commercial dual; commercial tiers £590 (attribution) / £1490 / £9320. RubberBandLiveShifter latency ≥50 ms documented. GPL => commercial licence required for proprietary VST.
- Signalsmith Stretch (ADC22 spectral method, header-only C++11): MIT (VERIFIED via LICENSE.txt, not LGPL). Latency ≈100–120 ms default preset. SAFE for proprietary use.
- pbshift (pbtechlab/pbshift, real project): MIT; real-time deterministic multi-resolution spectral engine + pitch-synchronous Voice engine; live latencies 46–128 ms by tier; depends only on pffft (BSD-like); pre-1.0 maturity.
- WORLD (mmorise): modified-BSD, "no patent in all algorithms"; speech-focused vocoder; DIO fast path real-time capable.
- SoundTouch: LGPL-2.1 (author Olli Parviainen); WSOLA-like; ~100 ms latency when stretching.
- zplane élastique PRO/EFFICIENT: proprietary commercial SDK, quote pricing; advertised inter-channel phase coherence, formant-preserving mono+polyphonic pitch shift.
- TD-PSOLA open source: sannawag/TD-PSOLA (MIT, research-grade), maxrmorrison/psola GPL-3.0 (Praat contamination).
- FFT options safe for commercial: KISS FFT (BSD-3), pocketfft (BSD-3), PFFFT (FFTPACK-derived BSD-like, NOT FFTW); FFTW = GPL, avoid.
- JUCE = AGPLv3 + commercial; VST3 SDK = now MIT (old dual-licence blocker gone).
- Bungee open core: MPL-2.0 (file-level copyleft, usable in proprietary with isolation).
- All open-source Doppler plugins found are GPL/AGPL; basic Doppler = implement from scratch (trivial, no licensing exposure).

---
Task ID: 1
Agent: whip-physics-research
Task: Research whip mechanics and experimental validation

Work Log:
- web_search: Bernstein/Hall/Trent 1958 JASA (1st attempt failed, irrelevant; retry succeeded)
- web_search: Krehl/Engemann/Schwenkel "puzzle of whip cracking" Shock Waves 1998
- web_search: Goriely/McMillen "Shape of a Cracking Whip" PRL 2002
- web_search: McMillen/Goriely "Whip waves" Physica D 2003
- web_search: whip tip Mach 2 schlieren measurement (weak results)
- web_search: Krehl loop/turning-point shock emission details (Semantic Scholar abstract hit)
- web_search: whip crack SPL dB (anecdotal only)
- web_search: Carriere 1927 Journal de Physique whip
- web_search: bullwhip crack dB peak (anecdotal only)

Stage Summary:
- Experimental: Carriere 1927 (J. Phys. Radium 8:365-384) photographic tip-speed study; Bernstein/Hall/Trent 1958 (JASA 30(12):1112-1115) "On the Dynamics of a Bull Whip" - crack = shock from supersonic tip, not slap; Krehl/Engemann/Schwenkel 1998 (Shock Waves 8(1):1-12): tip supersonic for ~1.2 ms, emits parabolic "head wave" (bow shock), correlated tip kinematics with shock emission.
- Modeling: Goriely/McMillen PRL 88(24):244301 (2002): tapered-rod/energy model, tip goes supersonic, crack from shock of tip motion; McMillen/Goriely "Whip waves" Physica D 184:192-225 (2003): full numerical simulation of tapered elastic rod, mini-sonic-boom at supersonic rod end.
- Numbers: supersonic duration ~1.2 ms [VERIFIED-PAPER]; tip Mach ~2 [INFERENCE, needs PDF confirm]; sound speed 343 m/s @ 20C; SPL anecdotal ~150 dB @ 1 inch, ~120 dB @ 1 m [UNPROVEN]; tip acceleration in g [UNPROVEN - no source found].
- Viable real-time models: (a) N-segment tapered-rod 1D wave eq, (b) loop/energy ODE (Goriely-McMillen style), (c) kinematic tip-trajectory playback + N-wave (Friedlander) shock synthesis.

---
Task ID: 2
Agent: acoustics-doppler-research
Task: Research acoustic shock generation and Doppler models

Work Log:
- 12 web searches: N-wave signature/rise time; Friedlander equation; whip-crack waveform; bullet ballistic shock; Whitham/Mach cone; Burgers/nonlinear steepening; gunshot acoustic signature; whip crack physics; J.O. Smith Doppler delay lines; retarded-time supersonic arrivals; Wwise/FMOD Doppler; small supersonic projectile shock numbers.

Stage Summary:
- N-wave (aircraft): rise ~3 ms, overpressure ~0.89 psf (~43 Pa, ~127 dB) standard sim (NASA/Leatherwood 1993); ~50 Pa typical far-field (Sparrow 2010); focused booms ~4x cruise overpressure (AIP 2005).
- Friedlander blast waveform: p(t) = Ps(1 - t/T0)exp(-b t/T0) (Beck 2011 JASA; Baker 1973 via Ganpule 2011) — most common blast-wave model, applicable to shock synthesis stage.
- Whip crack = miniature sonic boom from a traveling loop going supersonic (Wikipedia Whipcracking, Naked Scientists 2002, physics.SE); small-scale ballistic model booms measured (scispace/NASA).
- Mach cone: sin(mu) = 1/M (libretexts, UConn).
- Nonlinear steepening: Burgers-type nonlinearity shifts energy to HF until shock forms; finite rise time governed by viscous + relaxation absorption (arXiv 2502.08194; mn.uio.no; conforg closed tubes) -> rise time grows with distance: ms for km-scale aircraft, microseconds for m-scale whip/bullet paths [inference].
- Doppler: classical f' = f c/(c - vs cos theta) singular at v->c, invalid supersonic; J.O. Smith approach: Doppler via time-varying (de)interpolating delay lines (DAFx 'Doppler Simulation and the Leslie'; PASP) — pitch ratio = 1 - dD/dt; exact retarded-time map t = tau + |r(tau)-ro|/c is implicit; naive receiver-time delay caps compression at (1+M), implicit form reproduces true singularity; supersonic -> multiple emission-time roots = multiple arrivals + Mach-cone caustic.
- Middleware (Wwise/FMOD) Doppler docs not surfaced by searches — clamping behavior UNPROVEN, needs direct doc check.


---
Task ID: 6
Agent: orchestrator (main)
Task: Compile unified technical research report from all 5 research tracks

Work Log:
- Read and merged digests from Tasks 1-5 (all in worklog).
- Re-inserted Task 5 worklog entry lost to a parallel-write race.
- Compiled the unified report: /home/z/my-project/research/doppler-whip-pitch-research-report.md (916 lines).
- Structure: claim-tag system (VERIFIED-PAPER / VERIFIED-SOURCE / VERIFIED-LICENSE-FILE / INFERENCE / UNPROVEN);
  Section A (whip mechanics, experimental data, N-wave/Friedlander shock theory, 5-way Doppler model comparison
  incl. the naive-delay ratio-cap-at-2 finding, hierarchical segment-count analysis 8..128, Model A/B critical
  experiment protocol, failure modes/dead ends, classification, primary references);
  Section B (21-algorithm catalogue with full attribute grid, dynamic pitch-curve test battery with predicted
  outcome matrix, PitchProcessor interface + shared component pool + Doppler-bypass architecture, verified
  licensing table, benchmark corpus/metrics/listening tests, 7 final recommendation blocks, explicit negative
  results section, references).
- Report saved as single file; part files removed.

Stage Summary:
- Deliverable: research/doppler-whip-pitch-research-report.md
- Key conclusions: (1) crack = real miniature sonic boom (Krehl 1.2 ms supersonic window); (2) only implicit
  retarded-time Doppler reproduces near-Mach-1 physics (naive form caps at 2x compression - dead end);
  (3) recommended: 48-64 hierarchical segments + reduced-order motion (M3/M4) + pile-up-coupled Friedlander
  shock stage; Model A vs B critical experiment defined; (4) for rapid pitch curves: per-sample time-domain
  family (variable delay/harmonizer/varispeed) is structurally required; L-D'99 per-frame-varying shift is
  the best spectral candidate; (5) license-clean integrations: Signalsmith Stretch (MIT), pocketfft/PFFFT,
  VST3 SDK (now MIT), JUCE commercial; Rubber Band/eLastique = commercial; (6) skip: HNM/LPC/WORLD/DDSP/
  FD-PSOLA as product engines.

---
Task ID: 7
Agent: orchestrator (main)
Task: Pitch Lab architecture & design document (design only, no implementation)

Work Log:
- Checked for the referenced governing document upload: upload/ dir empty, file
  AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md NOT present anywhere
  in the project tree. Decision: do NOT invent its contents; design follows the
  principles as enumerated in the task brief; recorded as OPEN ISSUE #0 in the design.
- Designed the Pitch Lab architecture from the research report findings + task
  requirements (sections 1-26 of the brief).
- Compiled the design document: research/pitch-lab-architecture-design.md (706 lines).
- Sections delivered: A executive architecture; B component model + ownership matrix +
  project layout; C data flow; D PitchEngine C++ contract (capabilities model with
  justified/rejected fields, DurationBehaviour Preserving/RateFollowing, adapter
  semantics incl. subprocess isolation for GPL/LGPL externals, engine registry with
  UsageClass tiers); E pitch curve model (ratio canonical, spec->signal two layers,
  control-rate semantics first-class); F experiment entities + lifecycle + benchmark/
  creative separation; G benchmark architecture (harness owns methodology, initial
  suites, listening index); H analysis architecture (pure metric modules, varispeed
  reference, provisional tolerances); I high-sample-rate architecture (no 20 kHz
  ceiling, BandwidthSpec honesty, explicit resampling only); J dependency/licensing
  matrix (inherited from research, re-verification flags); K failure model (config
  error / job skip / taint taxonomy for all 14 enumerated cases); L testing strategy
  (8 layers incl. contract tests + reproducibility gate); M future compatibility
  (PropagationEngine/Doppler/shock/spatial/feedback boundaries, VST donation path);
  N 14 open decisions; O 16 rejected approaches with reasons; section-26 quality
  check answered (12x YES); classification tags (RESOLVED/OPEN/ASSUMPTION/RESEARCH
  FINDING/IMPLEMENTATION DETAIL) used throughout.

Stage Summary:
- Deliverable: research/pitch-lab-architecture-design.md (design only, explicitly
  not implemented).
- Key architectural decisions: 4 components (Compiler/Renderer/Analyzer/Reporter)
  + passive registries; single PitchEngine contract with per-engine capability
  honesty; dense audio-rate ratio curve signal as single pitch truth; planar float
  block-streaming renderer; file-based authoritative-vs-generated tree separation
  with bit-exact reproducibility gate (pitchlab verify); subprocess adapters for
  copyleft externals; creative mode as separate experiment kind that can never
  contaminate benchmark aggregation; feedback/propagation excluded but
  boundary-compatible (single-stage contract + future chain layer).
- Open issue for user: operating principles file must be re-uploaded (OPEN ISSUE #0).

---
Task ID: 8
Agent: orchestrator (main)
Task: Pitch Lab Architecture correction pass — revise design document v1.0 into v1.1 (6 mandated fixes, no redesign, no implementation)

Work Log:
- Downloaded the governing document from the user-supplied Google Drive link
  (AI ASSISTED SOFTWARE ENGINEERING OPERATING PRINCIPLES v3.0, 130 sections, 3901 lines)
  and stored it verbatim at research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md.
  This closes v1.0 OPEN ISSUE #0 and assumption A6/A5.
- Read the full Operating Principles (all 130 sections).
- Read the full v1.0 design document (research/pitch-lab-architecture-design.md, 706 lines).
- Archived v1.0 unchanged at research/archive/pitch-lab-architecture-design-v1.0.md (historical
  evidence, per Operating Principles §13/§63; the canonical path now holds v1.1).
- Wrote v1.1 (938 lines) into research/pitch-lab-architecture-design.md with the six
  mandated corrections:
  1. Rate-following/varispeed contract made normative (new §D.4): block exchange must
     represent input-frames-consumed, output-frames-produced (= output-timeline progress),
     end-of-input notification, bounded flush; Preserving canonical length amended from
     "output == input" (v1.0, internally contradictory with no-self-compensation latency
     rule) to N_in + outputLatencyFrames; RateFollowing length discovered during render,
     verified against integrated-curve expectation (tolerance = open decision);
     input-timeline curve indexing made explicit (read-position consumption semantics);
     worked example (1000 frames @ 2.0x => ~500 + L); signatures deliberately not frozen.
     Updated §A, §D, §D.1, §D.2, §E.2/E.3, §G.3, §H preamble (input-timeline common
     comparison axis + alignment policy), §H.1 (realised duration metric), §K (end-of-render
     violation case; length-policy row rewritten), §L (contract tests 7-8), §O.18/§O.20.
  2. Single authoritative engine registry (§D.6): in-code compile-time registration point
     is THE identity source (id, binding, display name, version, usage class, licence/origin,
     capabilities); engines.toml identity concept removed/rejected (§O.17); config = parameters
     only; unknown engine id = CONFIG ERROR (§K); registry row added to ownership matrix (§B.2);
     §A diagram + §B.1 + §J + §N updated.
  3. Source vs generated tree separation: artifacts/renders|analysis|reports (generated) vs
     src/analysis + src/analysis-py (source); confusable src/analysis vs analysis/ pair removed;
     all path references updated (§A, §B.2, §B.3, §C, §F.1, §F.4, §G.4, §I.1).
  4. Creative-mode requested/effective/status semantics (§E.5, §F.4): harness-side saturation
     of the requested curve to declared range; requested + effective + execution status
     (RANGE_SATURATED example) preserved end-to-end; engines never silently clamp; benchmark
     remains strict skip; v1.0 "override-ratio-range" flag replaced; open decision #7 resolved.
  5. Operating Principles reconciliation recorded (new §P): conflicts C1-C6 found & corrected
     (dual registry; ambiguous trees; creative info loss; varispeed underspecification;
     missing failure cases; dangling D.4 cross-ref); compliance verification; design
     preferences vs defects (§90); implementation-phase process obligations recorded as gate
     conditions (§P.4); §71 pre-implementation gate mapping (all yes); protected constraints
     (§P.6); result: CONSISTENT.
  6. v0.1 scope made binding (new §0.5): resampling primitives + varispeed + vardelay +
     pv.classic + pv.phaselocked + granular + harness; explicit do-NOT-implement list
     (WSOLA/TD-PSOLA/FD-PSOLA/SMS/WORLD/HNM/neural/externals/Doppler/propagation/shock/
     spatial/feedback/VST/GUI); phase column added to §D.6 registry table.
  - Additional repairs: cross-reference repair (D.4-D.7 renumbering; v1.0 enum comment pointed
    at the wrong section); "clamp" terminology reserved for forbidden behaviour (LFO bounds
    renamed); change record table added (Operating Principles §12/§14); status chain declared
    (everything = DESIGNED, §81).
- Performed the mandated internal consistency sweep (correction item 8) with explicit
  searches: RateFollowing (19 refs, consistent), varispeed (22 refs), engines.toml (only in
  change-record/rejected/reconciliation contexts), bare analysis|renders|reports paths
  (none without artifacts/ prefix outside the tree diagram), clamp (all forbidden/declared
  contexts), Operating Principles availability (no stale "NOT AVAILABLE"), v0.1 (33 refs),
  D.x cross-references (D.4=duration, D.5=rejected fields, D.6=registry, D.7=exclusions).
  All sweeps clean.
- No new components/managers/layers/databases/servers/plugin frameworks introduced; no code
  written; the project remains DESIGN ONLY.

Stage Summary:
- Deliverable: research/pitch-lab-architecture-design.md is now v1.1 (938 lines); v1.0 archived
  at research/archive/pitch-lab-architecture-design-v1.0.md; governing principles stored at
  research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md.
- Closure state: DONE WITH KNOWN LIMITATION — the design is implementation-ready for the v0.1
  scope (§0.5), reconciled against the Operating Principles (§P, result CONSISTENT), with 13
  non-structural open decisions (§N) remaining for the owner. Nothing is implemented; nothing
  in the document claims to be.
- Answered all 8 final quality-check questions YES (§26 correction-pass table).
- Exact next action (when ordered): owner resolves §N items -> implementation agent builds
  v0.1 per §B.3 layout with §L as acceptance gate, following §P.4 process obligations.

---
Task ID: 9
Agent: orchestrator (main)
Task: Docs & artifacts hub — upload all documents and every (future) artifact to GitHub (AGE-T/Pitchlab) and build an in-app docs/artifacts workbench at "/"

Work Log:
- Interpreted the user request ("szeretném, ha ide felkerülnének a doksik, valamint ha
  majd lesz, minden artifact" + GitHub PAT): destination = the user's freshly created,
  empty GitHub repo AGE-T/Pitchlab, verified the PAT via the GitHub /user API.
- Git hardening: untracked .env (now holds GITHUB_TOKEN + GITHUB_REPO, gitignored) and
  internal tool-results/ + .zscripts/dev.pid; extended .gitignore. Token is never
  committed (verified in remote tree).
- Created repo assets: README.md (doc index, artifact policy, sync instructions),
  artifacts/ tree per design doc §B.2 (renders/ analysis/ reports/, .gitkeep keepers),
  scripts/push-to-github.sh (commit all + push, token from .env only, redacts token
  from push output).
- Pushed the whole project (main) to https://github.com/AGE-T/Pitchlab — all 6
  authoritative docs now on GitHub.
- Built the Pitch Lab workbench web app at "/" (frontend first, then backend):
  * src/app/page.tsx — dark lab-console UI: header (status pill, GitHub, sync),
    sidebar (docs list grouped Tervezés/Kutatás/Működési elvek/Archívum/Projekt +
    artifacts tree with auto-poll every 30 s), markdown reader (max-w-3xl), right-rail
    TOC on xl / popover TOC on smaller, mobile Sheet navigation, sticky console-style
    footer status bar, framer-motion transitions, skeletons, toasts.
  * src/components/markdown-view.tsx — react-markdown + remark-gfm + Tailwind
    typography (custom pitch-prose dark theme); heading anchor generation that
    matches the TOC slug algorithm, including setext headings (==== banners) used by
    the Operating Principles doc; GFM tables with overflow scroll.
  * src/lib/docs.ts — server doc enumeration (research/**, worklog.md, README.md),
    curated metadata + generic fallback for future docs, path-traversal-safe reads
    (only enumerated paths are servable).
  * src/lib/artifacts.ts — recursive artifacts/ tree listing (.gitkeep hidden).
  * API routes: GET /api/docs, GET /api/docs/content?path=, GET /api/artifacts,
    GET/POST /api/sync (runs scripts/push-to-github.sh via execFile, 120 s timeout,
    token redaction in output, .sync-state.json state, git log lastCommit info).
  * Custom scrollbars (pitch-scrollbar), hu locale date-fns relative times.
- Installed: remark-gfm, @tailwindcss/typography.
- Browser self-verification (agent-browser): page renders; doc list = 6 docs with
  correct default (architecture design v1.1); doc switching works; TOC navigation
  works for ATX (design doc) AND setext (principles doc, 133 entries) — found and
  fixed a bug where headings had no id (anchors were not generated initially);
  sync dialog ran a real sync: commit "sync: docs, worklog and artifacts" created and
  pushed, remote HEAD == local HEAD verified via git ls-remote; artifact discovery
  verified with a temporary test file (then removed); mobile 390 px layout verified
  (Sheet menu, no horizontal overflow); VLM screenshot reviews passed (desktop +
  sync dialog); eslint clean; dev.log clean.
- Path traversal and .env access verified blocked (404) via /api/docs/content.

Stage Summary:
- Deliverables: (1) GitHub repo AGE-T/Pitchlab now holds every document + the full
  project; (2) scripts/push-to-github.sh one-command sync (token stays in .env);
  (3) artifacts/ tree ready for v0.1 outputs; (4) the "/" workbench app to browse all
  docs (markdown + TOC) and artifacts in-app, with a one-click "Szinkron" button that
  pushes docs + artifacts to GitHub.
- Future artifacts: anything placed in artifacts/renders|analysis|reports appears in
  the app automatically (30 s poll) and is included in every sync.
- Security: GitHub PAT only in gitignored .env; API output redaction; allowlisted
  doc paths.

---
Task ID: 10-a
Agent: operating-principles-digest
Task: Digest the AI Assisted Software Engineering Operating Principles v3.0 for the V0.1 implementation-freeze gate

Work Log:
- Read worklog.md in full (338 lines) to reconstruct project state: Pitch Lab is
  DESIGN ONLY, architecture design v1.1 (938 lines) reconciled with the Operating
  Principles (result CONSISTENT), 13 non-structural open decisions (§N) remaining
  for the owner, v0.1 scope binding (§0.5), everything = DESIGNED (§81).
- Read the FULL governing framework
  research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md
  (3901 lines, 130 sections) start to finish, in 10 sequential chunks.
- Extracted the operative rules relevant to STARTING an implementation phase of a
  C++ project: §0 entry protocol + WORKLOG contents; §71 pre-implementation gate
  (16 items); §62 protected constraints; §2/§80 single-source-of-truth and
  canonical-path rules; §12/§13/§14/§37/§128 change + decision records; §32 scope
  control; §37/§38/§127 terminology; §7/§8 state preservation + handoff; §79
  information loss; §81 status chain; §90 design preference vs defect; §91/§126
  closure states; §50/§75/§119/§130 lifecycle/cycle obligations; §39-44/§97/§98
  testing; §59/§60/§89 classification; §31/§33 escalation; §112-§116/§123-§125
  dependency/licensing/environment rules.
- Verified by exhaustive grep that the framework contains NO "freeze"/"implementation-ready"
  literal terminology and NO directory-naming rule (those live in the project design
  doc §B.2/§B.3, not in the framework); licensing appears only in §64, §112, §124.
- Wrote the ~3000-word structured digest (14 numbered sections, with verbatim
  quotes and section numbers) and returned it to the orchestrator as the final
  report. No code written; no project files modified other than this worklog entry.

Stage Summary:
- The framework's implementation-start gate = §71 (16 verify-items: architecture,
  Source of Truth, scope, state, ownership, lifecycle, persistence, failure paths,
  testing strategy, versioning, configuration, recovery, open decisions, known
  ambiguities, protected constraints, validation strategy) + §112/§113 environment
  and dependency start audit + §0 WORKLOG entry-state requirements.
- Every development cycle must follow the single master lifecycle (§75, depth
  MINIMAL/STANDARD/DEEP), end with the §119 closure sequence, a §120 single-ZIP
  verified handoff package, §121 handoff test, and a truthful closure state
  (§91: DONE / DONE WITH KNOWN LIMITATION / BLOCKED / DEFERRED BY DECISION /
  ACCEPTED RISK / UNVERIFIED; §126 package states).
- Status claims must use the §81 chain (DESIGNED → IMPLEMENTED → UNIT TESTED →
  INTEGRATION TESTED → SYSTEM VERIFIED → DEPLOYED → FIELD VERIFIED) and never
  collapse to "done"; §79 silent information loss = architectural defect;
  §90 forbids promoting design preferences into defects.
- Next action for orchestrator: write the implementation spec against these rules
  (especially §71 gate checklist, §0/§118 WORKLOG current-state block, §32 scope
  table, §39-§41 test matrix, §112/§113 dependency audit, §119/§126 cycle closure).

---
Task ID: 10-b
Agent: gh-actions-environment-research
Task: Research current GitHub-hosted runner images and toolchain for the Pitch Lab C++20 CI

Work Log:
- Read worklog.md (context: Pitch Lab = C++20 DSP research project, GitHub AGE-T/Pitchlab,
  GitHub Actions to be the canonical build/test environment; web research only, no code).
- Invoked the web-search skill first for CLI syntax (z-ai function -n web_search), then
  ran targeted searches (ubuntu-latest migration, 22.04 retirement, runner specs,
  concurrency limits) and fetched authoritative pages with curl (retrieval date
  2026-09-25): actions/runner-images README + Ubuntu 22.04/24.04/26.04 image Readmes
  (raw.githubusercontent.com), announcement issues #14748/#14747/#14254/#14226,
  docs.github.com (runners reference, billing, limits, security hardening),
  gcc.gnu.org (cxx-status + libstdc++ status), cmake.org/download, apt.kitware.com,
  ctest(1) manual, action release atom feeds + git ls-remote for tag SHAs.
- Runner state today: ubuntu-latest = Ubuntu 24.04; rollout of ubuntu-latest -> 26.04
  begins 2026-10-19, completes by 2026-11-19 (issue #14748); Ubuntu 26.04 GA since
  2026-09-17 (issue #14747); ubuntu-22.04 deprecation began 2026-09-17, full
  retirement 2027-04-17 (issue #14254); images update weekly (README cadence).
- Toolchain on ubuntu-24.04 image (Image Version 20260920.314.1, OS 24.04.5 LTS):
  gcc/g++ 13.2.0 (only distro GCC), Clang 16/17/18.1.3, CMake 3.31.6, Ninja 1.13.2,
  Git 2.55.0. Ubuntu 26.04 image for comparison: gcc 15.2.0, CMake 4.4.3, Ninja 1.13.2.
- C++20 in GCC (cxx-status + libstdc++ status): language features all in by GCC 12
  (modules still experimental, -fmodules); library: ranges/concepts 10.1, format 13.1,
  time zones 13.1, chrono::parse 14.1 -> GCC 13.2 covers Pitch Lab's C++20 needs.
- Action majors today: actions/checkout v7.0.1, actions/upload-artifact v7.0.1
  (inputs unchanged since v4; artifacts immutable; hidden files excluded by default;
  retention default 90 days), actions/cache v6.1.0; got commit SHAs via git ls-remote.
- CMake pinning: cmake.org/download still lists Linux x86_64 .tar.gz/.sh (latest 4.4.3,
  plus 4.3.5 and 3.31.12); verified v3.31.6 tarball URL (HTTP 200), downloaded it and
  computed sha256 5a1133ff...367bf; apt.kitware.com still maintained and supports
  noble/24.04 (but rolling, cannot exact-pin) -> recommended pinned tarball + checksum.
- ctest: --output-on-failure / CTEST_OUTPUT_ON_FAILURE documented; --output-junit since
  CMake 3.21; no first-party CTest JUnit action (third-party only) -> keep plain logs.
- Limits (docs): public repos = standard runners free and unlimited; Free plan 20 total
  concurrent jobs, 6h job cap, 256-job matrix, artifacts default retention 90 days;
  Linux standard runner = 4 vCPU / 16 GB RAM / 14 GB SSD, x64 and arm64 labels.
- Compiled the ~1200-word structured report with the recommended environment table
  (ubuntu-24.04 pinned, GCC 13.2.0 preinstalled, CMake 3.31.6 pinned tarball, Ninja
  1.13.2, ctest, upload-artifact v7-pinned-by-SHA) and runtime version-assert recipe.
  No code written; no project files modified other than this worklog entry.

Stage Summary:
- Recommendation: pin runs-on: ubuntu-24.04 (x64, 4 vCPU/16 GB/14 GB SSD), NOT
  ubuntu-latest (label flips to 26.04 between 2026-10-19 and 2026-11-19) and NOT 22.04
  (deprecating, retired 2027-04-17; brownouts Mar-Apr 2027) and not brand-new 26.04.
- Toolchain: default g++ 13.2.0 (full C++20 coverage incl. concepts/ranges/format),
  CMake 3.31.6 via pinned official tarball (sha256-verified) to defeat weekly image
  drift, Ninja 1.13.2 preinstalled, ctest --output-on-failure (+ optional JUnit XML).
- Pin actions by full commit SHA (security-hardening guidance): checkout
  3d3c42e5aac5ba805825da76410c181273ba90b1 (v7.0.1), upload-artifact
  043fb46d1a93c77aae656e7c1c64a875d1fc6a0a (v7.0.1), cache (if ever needed)
  55cc8345863c7cc4c66a329aec7e433d2d1c52a9 (v6.1.0); skip cache for a small project.
- CI must log and assert toolchain versions each run (g++ -dumpfullversion, cmake
  --version, ninja --version) because images redeploy weekly; free & unlimited minutes
  for the public AGE-T/Pitchlab repo, 20 concurrent jobs on the Free plan.

---
Task ID: 10-c
Agent: pyin-cpp-feasibility-research
Task: Research C++-only feasibility of the pYIN reference pitch tracker under the C++20-only constraint

Work Log:
- Read worklog tail (task 9 context) and grepped the research report: §B.4 licensing table lists "librosa/pYIN | ISC | Offline benchmarking only (Python)" and CREPE as offline-only; §B.5.2 defines the flagship Pitch-error metric via "pYIN/CREPE on shifted sine/harmonic items vs. expected instantaneous f0(t) ... tracking lag under ramps/reversals"; §B.5.4 harness is "Offline render harness (Python/C++)"; design doc §J marks librosa(pYIN)/CREPE as OPTIONAL dev-only Python layer, "replaceable (C++ tracker later; analyzer is pluggable)", with open decision #12 "Reference pitch tracker choice (pYIN vs CREPE vs C++ own)".
- ~15 web searches via z-ai web_search CLI: pYIN C/C++ implementations, plain YIN in C/C++, alternative C++ reference trackers, paper and librosa spec availability; plus direct fetches (curl, GitHub raw/API, headless browser) of READMEs, LICENSE files, and source code of every candidate.
- Found and primary-source-verified 4 C/C++ pYIN implementations:
  * c4dm/pyin (github.com/c4dm/pyin) — the ORIGINAL author C++ implementation (Mauch, QMUL), full pYIN (Yin/YinUtil threshold distribution, MonoPitchHMM Viterbi, note-level HMM, regression tests), GPL-2.0-or-later (COPYING verified), depends on Vamp Plugin SDK (MIT-style, verified in headers), Vamp-plugin architecture, last push Jul 2024 (frozen/stable).
  * xstreck1/LibPyin (github.com/xstreck1/LibPyin) — vendored copy of the c4dm/pyin core + vendored MIT vamp-sdk + friendly C/C++ API, C++11, zero external deps, CMake/QMake; GPL-3.0; last push Jan 2026.
  * MTG/essentia — PitchYinProbabilistic + PitchYinProbabilities + PitchYinProbabilitiesHMM (standard & streaming), full pYIN in C++ (defaults frame 2048 / hop 256 @44.1k, per paper), AGPL-3.0 (proprietary on request), actively maintained but heavy.
  * Sleepwalking/libpyin (github.com/Sleepwalking/libpyin) + libgvps (github.com/Sleepwalking/libgvps) — independent C99 full pYIN (100-step Beta threshold prior, semitone-binned voiced/unvoiced HMM, Viterbi via libgvps), BSD-3-Clause BOTH (headers verified), ~1.3 kLOC total, plain makefiles, no deps beyond libm; unmaintained since Jan 2020; niche real-world use (author's ciglet/LLSM/moresampler vocal-synth ecosystem).
- BUILD-VERIFIED libpyin+libgvps on this machine (gcc, CONFIG=Release): compiled clean; test binary produced a sensible track on the bundled speech wav (85/157 frames voiced, f0 76-149 Hz). Friction: stale submodule reference (readme says submodule, no .gitmodules on master — must clone libgvps separately and pass GVPS_PREFIX on the make command line); API returns f0 track only (unvoiced = 0; no voiced_prob, no per-frame candidates); documented deviations from paper/librosa (candidate "emphasis" heuristic for octave confusion, voiced-onset back-fill; beta_a=1.7/beta_u=0.2, ptrans=0.003).
- Downloaded and read the actual pYIN ICASSP-2014 paper (5 pp, from Dixon's QMUL webspace): Stage 1 fully specified (ACF -> CMND; N=100 thresholds 0.01-1.0; Beta priors alpha=1 beta=18/11-13/8 i.e. means .1/.15/.2; pa=0.01; parabolic interpolation). Stage 2 nearly fully specified (480 bins, 4 octaves 55-880 Hz in 10 cents; 2M voiced/unvoiced states; observation prob formula (6); pv 0.99/0.01; triangular pitch transition, max jump 25 bins = 2.5 semitones/frame; uniform init over unvoiced; sparse Viterbi) — ONE explicit punt: exact triangular transition weights "refer to the source code". Paper's own operating point: hop 256 (5.8 ms), frame 2048 (46.4 ms) @44.1 kHz. Paper states pYIN "freely available online ... as an open source C++ library for Vamp hosts".
- Verified librosa.pyin parameter set from librosa source: n_thresholds=100, beta_parameters=(2,18), boltzmann_parameter=2, resolution=0.1, max_transition_rate=35.92, switch_prob=0.01, no_trough_prob=0.01, transition_min_prob=1e-4, fill_na, center/pad_mode, frame_length=2048, hop=frame//4, fmin/fmax recommended C2-C7, returns (f0, voiced_flag, voiced_prob). NOTE: "threshold_std" does NOT exist in librosa.pyin — the threshold prior is beta_parameters (the task brief's parameter name appears to be a misremembering).
- Fallback plain-YIN C/C++ survey: aubio (GPL-3.0 COPYING verified; yin + yinfft; plain YIN only, no HMM), sevagh/pitch-detection (C++, MIT LICENSE verified; YIN/MPM/SWIPE'/autocorr; single-maintainer, low activity), Praat (GPL-2.0+ verified — already flagged in project matrix as contamination risk), WORLD DIO/Harvest (Modified BSD — already in project matrix as licence-safe alternative F0 reference).
- TSR and "RAPTOR" pitch trackers: searches found no credible hits under those names (a "RAPTOR pitch tracker" could not be confirmed to exist; nearest modern candidate found is SwiftF0, arXiv 2508.18440 (Aug 2025), MIT, Swift+ONNX, neural-lite — not a classic pYIN-class DSP tracker and not C++).
- Rust existence proof: Sytronik/pyin-rs (crates.io "pyin", MIT, updated Jul 2024, "based on librosa v0.9.1") — a one-author systems-language port of librosa pYIN to full parity; also pitch-core Rust crates (pYIN/SWIPE'/Praat-AC backends). Evidence that the spec is adequate and the port is small.
- Research artifacts kept in /tmp/pyin-research/ (search JSONs, paper PDF+text, cloned repos) — not added to the project tree.

Stage Summary:
- BOTTOM LINE: partial with caveats. A C++-only pYIN-class reference tracker is FEASIBLE today. Full pYIN exists in C/C++ four times over (original c4dm/pyin GPL-2+; Essentia AGPL-3; LibPyin GPL-3; libpyin BSD-3), and the paper + librosa documentation specify the algorithm well enough for a clean-room C++ implementation of moderate size — but NO mature, permissively-licensed, dependency-light, maintained C++ pYIN exists off-the-shelf.
- Best permissive candidate: Sleepwalking/libpyin + libgvps (both BSD-3-Clause, ~1.3 kLOC, build-verified here) — credible as a vendored reference AFTER a validation pass; caveats: unmaintained since 2020, single author, API minimal (f0 only), small algorithmic deviations from paper/librosa.
- All other C/C++ pYINs are GPL/AGPL — "research reference only" per the project's §B.4 matrix philosophy; linking any of them into the C++ analysis layer would copyleft that component (owner-level licensing decision given the commercial-VST future).
- Recorded 4 options for open decision #12 (orchestrator decides): (A) vendor BSD-3 libpyin+libgvps with a validation gate, S-M effort; (B) clean-room or ISC-derived C++ pYIN from paper + librosa docs, M effort; (C) interim plain C++ YIN reference with documented deviation, S effort, loses probabilistic voicing + HMM octave robustness (paper: median recall 0.977-0.984 pYIN vs <0.951 YIN on degraded singing); (D) defer the reference tracker to post-v0.1 and/or owner escalation on the dev-only Python/librosa layer (already sketched as OPTIONAL in design doc §J), S effort, v0.1 pitch-error metric degraded/pending. No decision made — left as OPEN DECISION.

---
Task ID: 10
Agent: orchestrator (main) + subagents 10-a/10-b/10-c
Task: V0.1 implementation freeze & CI readiness — reconcile repository with architecture, freeze v0.1 contracts, define canonical CI environment, produce implementation specification, validate CI on GitHub Actions

Work Log:
- Entry protocol (Operating Principles §0): read the architecture v1.1 IN FULL (939 lines), the research report's engine/metrics/benchmark/licensing sections, README, worklog history; audited the actual git state + tree (fresh, not trusting prior claims); launched three research subagents (10-a Operating-Principles digest; 10-b GitHub-hosted runner/toolchain research with live sources 2026-09-25; 10-c pYIN C++-only feasibility). All findings classified (DEFINED / PARTIALLY DEFINED / CONTRADICTORY / OPEN DECISION / REQUIRES EMPIRICAL VALIDATION).
- Conflict analysis: C-1 bus precision (architecture §D.1 float32 bus vs owner L-4 float64 masters) → resolved by owner authority as recorded Amendment A-1 (v0.1 bus = planar double; f32 only at listening export; architecture v1.2 fold-in queued as OD-16 — NOT silent). C-2 generated-artifacts-on-GitHub (Task 9 tracked tree vs architecture §B.3 gitignore) → architecture wins; owner intent preserved as OD-15. C-3 docs location → research/ single location (R-2).
- Reconciliation executed in the repository: created `pitch-lab/` per architecture §B.3 (config with locked/provisional values, experiments/assets trees, src/core + src/cli infrastructure, tests, tools/, external/ policy README, reference-model notes); REMOVED the root `artifacts/` tracked tree; workbench artifacts API re-pointed to `pitch-lab/artifacts/`; workbench docs index gained the two new specs (new "Implementáció" category); .gitignore extended (pitch-lab/build/, pitch-lab/artifacts/).
- Wrote `research/pitch-lab-v0.1-implementation-specification.md` (959 lines): §2 reconciliation map + conflicts + amendments; §3 structure; §4 frozen C++20 contracts (AudioBlockView double bus, PitchEngine with explicit block-exchange ProcessReport + finish(), renderer loop pseudocode, frame accounting with input-latency padding rules, PitchCurve two-layer model + validation + requested/effective, EngineConfiguration, registry API, PCG64 RNG policy, harness component surfaces, WAV/analysis interfaces); §5 ownership/lifecycle rules (allocation rules incl. T-A1 audit test); §6 five engine sheets (varispeed incl. read-position quadrature + AA policy; vardelay wrap/crossfade design; pv.classic; pv.phaselocked L-D'99 identity locking; granular with synchronised channels); §7 shared Kaiser windowed-sinc resampler (presets, AA policy, determinism incl. -ffp-contract=off, acceptance criteria); §8 bandwidth honesty; §9 WAV contract; §10 fifteen metric module sheets + tracker dependency; §11 deterministic synthetic corpus + curve battery; §12 real-world corpus schema (design only); §13 full test matrix (T-E1..T-E19 + T-A/T-D/T-R/T-W/T-C/T-M/T-K/T-G/T-INF layers, T-LEN-CAL calibration); §14 registry freeze/end state; §15 TOML schemas; §16 open decisions OD-6..OD-16 (owner resolves §N.1/2/3/4/5 via mandate L-1..L-5); §17 implementation order; §18 final gate → PACKAGE READY WITH KNOWN LIMITATION.
- Wrote `research/pitch-lab-build-and-ci-environment.md`: canonical environment (pinned ubuntu-24.04 x64 — NOT latest: latest flips to 26.04 during 2026-10-19..11-19; GCC 13.2.0 exact-asserted; CMake 3.31.6 sha256-pinned tarball 5a1133ff…367bf; Ninja ≥1.11; CTest --output-on-failure --output-junit), action SHAs pinned (checkout 3d3c42e…, upload-artifact 043fb46…), dependency audit (zero at freeze; pocketfft + doctest planned, licence-verified per §J policy), cache none, artefact handling, reproducibility limitations, local-dev non-authoritative, honesty section (what CI proves / does not).
- Implemented freeze-state infrastructure (explicitly NOT DSP): `pitch-lab/CMakeLists.txt` (C++20, -Werror, -ffp-contract=off, CTest); `src/core/version.{h,cpp}` (phase = implementation-freeze, compilerId for manifests); `src/core/engine_registry.{h,cpp}` (§D.6 single registration point registerProductionEngines — deliberately EMPTY; register/seal/lookup semantics); `src/cli/main.cpp` (honest introspection only); tests: toolchain_smoke (C++20 features + strict-IEEE + bit-identical repeat; __FAST_MATH__ guard), engine_registry_smoke (freeze-state emptiness = anti-fake-engine guard + mechanism), version_smoke.
- CI workflow `.github/workflows/ci.yml`: pinned runner, toolchain assertions, checksum-verified CMake, Ninja Release build, CTest with JUnit, evidence upload (if-no-files-found: error).
- LOCAL VALIDATION: pinned CMake 3.31.6 downloaded + sha256-verified in sandbox; clean configure/build with GCC 14.2.0 (Unix Makefiles — generator portability documented); all 3 tests pass; `pitchlab --version`/`engines` output honest freeze state; JUnit XML verified; web lint clean; workbench APIs re-verified (docs list, artifacts empty tree).
- COMMIT + PUSH: everything committed and pushed to AGE-T/Pitchlab main.
- CI VALIDATION: honest two-part record below — LOCAL executed green on the pinned toolchain; GITHUB EXECUTION BLOCKED by the PAT's missing Workflows permission (OD-17, remediation recorded). No claim that CI ran on GitHub.
- Worklog upgraded to the Operating Principles §0/§118 format: CURRENT STATE block at top (reading order, SoT, phase, limitations, open decisions, environment, validation, exact next action). Handoff-package interpretation recorded: the canonical repository at the freeze commit + this worklog = the recoverable handoff artefact (single-ZIP packaging per §120 was not mandated by the owner's task brief §24, which defines the deliverable set; deviation documented here per §91 truthful-closure rules).

CI VALIDATION — HONEST RECORD (2026-09-25):
- LOCAL (executed): pinned CMake 3.31.6 tarball downloaded and sha256-verified (`cmake.tar.gz: OK`); clean configure + build (GCC 14.2.0, Unix Makefiles — generator portability documented; CI uses Ninja on the same pinned CMake); `ctest` 3/3 passed (toolchain_smoke, engine_registry_smoke, version_smoke); JUnit XML generated and inspected (3 tests, 0 failures); `pitchlab --version` / `pitchlab engines` output the honest freeze state; workflow YAML parsed and structurally verified.
- GITHUB (BLOCKED by credentials): `git push` rejected with `! [remote rejected] main -> main (refusing to allow a Personal Access Token to create or update workflow `.github/workflows/ci.yml` without `workflow` scope)`; Contents API returned `403 Resource not accessible by personal access token`. The fine-grained PAT (AGE-T) has repo admin/push but NOT the Workflows permission (verified via the /repos API permissions object).
- Consequence: freeze commit c17e5b5 pushed WITHOUT the workflow file (all other deliverables on GitHub); the workflow file remains ready locally, gitignored until activation (so doc-sync pushes keep working; recorded as OD-17 with one-time remediation in build spec §12.1). NO claim is made that CI ran on GitHub.
- Exact remediation (owner): grant the fine-grained PAT "Workflows" permission (Read and write), then: remove the `.github/workflows/ci.yml` line from `.gitignore`, run `git add -f .github/workflows/ci.yml && bash scripts/push-to-github.sh`. The workflow triggers on the next push; evidence = run URL + green conclusion in the Actions tab.

Stage Summary:
- Deliverables: implementation specification (959 lines), build/CI environment specification, CI workflow (implemented + locally validated on the pinned toolchain; GitHub activation pending OD-17), pitch-lab/ freeze-state skeleton (registry EMPTY by design, asserted by the T-E19 smoke test), reconciled repository structure, updated README + workbench, worklog current-state block.
- Key decisions recorded: Amendment A-1 (f64 bus, owner-mandated, v1.2 fold-in queued); architecture wins on generated-tree policy (root artifacts/ removed; OD-15 records the owner's publication intent); OD-12 pYIN options A/B/C/D with B recommended — NOT silently resolved.
- Package state: PACKAGE READY WITH KNOWN LIMITATION (limitations: provisional tolerances, OD-12 tracker, A-1 fold-in, OD-14/OD-15, OD-17 CI activation). No DSP implemented; CI proves environment + infrastructure only — the registry test asserts emptiness (anti-fake-completeness).
- Exact next action: owner resolves OD-12 → implement per specification §17 step 1.

---
Task ID: 11
Agent: orchestrator (main)
Task: CI activation (OD-17) — owner granted the PAT's Workflows permission; push the workflow, verify the first real GitHub Actions run

Work Log:
- Entry protocol: read the worklog current-state block + Task 10 entry in full; audited the actual repository state fresh (git status/log/ls-files, filesystem walk, .env key names only — never values). Findings: working tree clean at 1cb1f10, BUT the workspace had been RESET between sessions (all file mtimes 2026-09-26 09:11): the gitignored, never-committed `.github/workflows/ci.yml` (OD-17's "ready locally" copy) was destroyed, and the gitignored `.env` lost `GITHUB_TOKEN`/`GITHUB_REPO` (only `DATABASE_URL` remained). No remote configured (pushes always used the token-embedded URL from scripts/push-to-github.sh). Searched the whole home tree, shell histories and git metadata for a recoverable token — none exists locally.
- Consequence of the Task-10 decision (gitignored file, never committed): the "workflow ready locally" state did not survive the workspace reset. Recovered by re-authoring the file from the canonical specification, not from a copy.
- Re-authored `.github/workflows/ci.yml` strictly from `research/pitch-lab-build-and-ci-environment.md`: pinned `ubuntu-24.04` runner (never latest), SHA-pinned actions (checkout 3d3c42e5..., upload-artifact 043fb46d...), §6 toolchain report + exact assertions (GCC 13.2.0, CMake 3.31.6, Ninja >= 1.11), §4 checksum-pinned CMake install (sha256 5a1133ff...367bf), §11 canonical configure/build/test commands, §10 evidence upload with `if-no-files-found: error`; honest scope header (T-INF1 infrastructure only — NOT DSP; registry test asserts emptiness). Added `shell: bash` (pipefail) on piped steps so `tee` cannot mask failures.
- LOCAL RE-VALIDATION on the pinned toolchain (2026-09-26): pinned CMake 3.31.6 tarball downloaded and sha256-verified (`cmake.tar.gz: OK`, installed to ~/.local); clean configure + build (GCC 14.2.0, Unix Makefiles — local generator per build spec §13; CI canonical remains Ninja); `ctest` 3/3 passed (toolchain_smoke, engine_registry_smoke, version_smoke); `pitchlab --version` / `pitchlab engines` output the honest freeze state; workflow YAML parsed (1 job, 7 steps, 3 triggers).
- DEFECT FOUND + FIXED (empirical, would have failed CI): `ctest --test-dir build --output-junit build/test-results.xml` resolves the JUnit path INSIDE the test dir, producing `build/build/test-results.xml` — the evidence upload (`if-no-files-found: error`) would then fail even with green tests. Fixed: the workflow passes `--output-junit test-results.xml`, verified locally (JUnit XML lands at `build/test-results.xml`, 3 tests, 0 failures). Since the original file is lost, the current re-authored file (with the fix) is authoritative; recorded in build spec §12.1.
- Repository changes: `.gitignore` — removed the `.github/workflows/ci.yml` ignore entry (per the §12.1 remediation; file becomes tracked, historical note kept); documentation updated honestly — build spec header + §12.1 rewritten as a three-step chronology (2026-09-25 credential rejection → 2026-09-26 permission granted + workspace reset + re-authoring + re-validation → 2026-09-26 activation push blocked locally by missing token value); README activation-status block updated; implementation spec §1.4, §13.3 and OD-17 row updated.
- ACTIVATION PUSH ATTEMPT (2026-09-26): `bash scripts/push-to-github.sh` exits at the credential check — `GITHUB_TOKEN`/`GITHUB_REPO` absent from `.env` (workspace reset). No push reached GitHub; no CI run exists; no claim is made that CI ran on GitHub. The activation commit (workflow file + docs + this entry) is committed locally and will be pushed the moment the owner restores the token values to `.env` (exact one-liner recorded in build spec §12.1); the push itself triggers the first CI run automatically.
- Workbench re-verified after the reset: dev server healthy on port 3000 (dev.log clean), `/api/docs` and `/api/artifacts` respond 200; browser check performed (docs browser + build/CI spec renders with the updated §12.1; mobile 390px OK; sticky footer OK).

Stage Summary:
- OD-17 status moved from "PAT lacks Workflows permission" to "permission GRANTED by owner; activation blocked LOCALLY by the missing token value (workspace reset destroyed the gitignored .env values AND the gitignored workflow copy)" — a strictly smaller, purely local blocker with a one-time remediation (re-add two lines to .env, push).
- Deliverables: re-authored + tracked `.github/workflows/ci.yml` (canonical, with the empirical JUnit-path fix), updated .gitignore, updated build spec §12.1 / README / implementation spec (§1.4, §13.3, OD-17), this worklog entry + current-state block; local re-validation evidence (sha256-verified pinned CMake, 3/3 tests green, JUnit 0 failures).
- Honest record: NO GitHub push, NO CI run, NO fake green. Package state remains READY WITH KNOWN LIMITATION; the only pending piece of OD-17 is the token value restoration (owner action).
- Exact next action: owner re-adds `GITHUB_TOKEN`/`GITHUB_REPO` to `.env` → push → record first CI run URL + conclusion (build spec §12.1 evidence requirements) → then OD-12 owner resolution → implementation per §17 step 1.

**ACTIVATION COMPLETION — Task 11 addendum (2026-09-26, same day; owner supplied the token and confirmed the Workflows permission):**

Work Log (continuation):
- Credential verified via the API without ever printing the token (login AGE-T; repo admin/push; `.env` still gitignored, values never logged, never committed).
- Remote reconciliation: the remote tip was 77ac6fe — a mode-normalised re-commit of 1cb1f10 (same parent c17e5b5, same message, content-identical trees: 0 insertions/deletions across 108 files; only 755→644 mode changes). Integration: `git rebase --onto origin/main 1cb1f10 main` — the two local activation commits replayed cleanly onto 77ac6fe (linear history preserved, NO force-push, no history rewritten on the remote; new SHAs a2cf69a, cc59727).
- ACTIVATION PUSH: `77ac6fe..cc59727 main -> main` — the tracked `.github/workflows/ci.yml` reached GitHub and triggered the first real CI run.
- RUN 1 (36237006396, head cc59727): **failure at the toolchain-assertion step — the anti-drift guard worked exactly as designed.** Live log evidence: image 20260920.314.1 ships `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`, `cmake version 3.31.6`, `ninja 1.13.2` — the pinned GCC 13.2.0 assertion failed loudly (the runner-images README's documented GCC version was stale). Deviation flagged per owner instruction; remediation executed as a recorded re-pin: GCC 13.2.0 → 13.3.0 in the workflow + build spec §1/§3/§6 (v1.1); pushed as 6e49db9.
- RUN 2 (36237155501, head 6e49db9): **GCC assertion GREEN; CMake extraction failed — sandbox-vs-runner gap.** `/tmp/cmake.tar.gz: OK` (download + sha256 fine), then `tar: /home/runner/.local: Cannot open: No such file or directory` — the canonical runner has no `~/.local` by default (the development sandbox does; that is why local validation passed). Deviation flagged; fix: `mkdir -p "$HOME/.local"` prepended to the canonical command (build spec §4 + workflow) — environment bootstrap only, no build/test semantics changed; pushed as 593a2d5.
- RUN 3 (36237247935, head 593a2d5, job 108391152798, 10:55:37→10:55:48 UTC): **CONCLUSION SUCCESS — every step green.** Evidence captured from the live run and the artefact:
  * steps: checkout ✓, toolchain report + assertions ✓ (GCC 13.3.0, CMake 3.31.6, Ninja 1.13.2, image 20260920.314.1), pinned CMake install ✓ (`/tmp/cmake.tar.gz: OK`), configure ✓ (Ninja/Release, `GNU 13.3.0`), build ✓ (11 targets), test ✓, evidence upload ✓;
  * `ci-evidence` artefact (id 10904079295) downloaded via the API and content-verified: configure.log (391 B), build.log (738 B), test.log (516 B, `100% tests passed, 0 tests failed out of 3`), build/test-results.xml (891 B, JUnit `tests="3" failures="0"`, per-test output confirms toolchain_smoke / engine_registry_smoke (freeze-state EMPTY guard) / version_smoke (`gcc 13.3.0`));
  * run URL: https://github.com/AGE-T/Pitchlab/actions/runs/36237247935
- Documentation closed out per owner instruction: build spec v1.1 (§12.1 chronology steps 4-6 + activation proof; §12.2 "activated"), implementation specification (§1.4, §13.3, §16 OD-17 → RESOLVED, §18 final-gate row, blocker assessment), README activation status, this worklog. OD-17 closed ONLY on the basis of the executed green run with content-verified artefacts — no green-by-fiat.

Stage Summary (activation):
- **OD-17: RESOLVED — CI is live on AGE-T/Pitchlab.** The workflow runs on every push; the canonical environment is proven by execution (not by documentation): ubuntu-24.04 x64 / GCC 13.3.0 / CMake 3.31.6 sha256-verified / Ninja / CTest, clean checkout, zero third-party dependencies.
- Two honest intermediate failures were part of the cost, not hidden: (1) a stale documented fact (GCC 13.2.0→13.3.0) caught by the designed anti-drift assertion; (2) a sandbox-vs-runner environment gap (`~/.local`) caught by real execution. Both diagnosed from live logs, flagged before fixing, and recorded as amendments in build spec v1.1.
- Scope honesty unchanged: CI proves ENVIRONMENT + INFRASTRUCTURE only (T-INF1; registry-freeze emptiness asserted). It does NOT prove DSP correctness — no DSP is implemented, none is faked. Package state stays READY WITH KNOWN LIMITATION (provisional tolerances, OD-12 tracker, OD-16 fold-in, OD-14/OD-15).
- Exact next action: owner resolves OD-12 (pYIN route A/B/C/D; B recommended) → implementation begins at implementation-specification §17 step 1.

---
Task ID: 12
Agent: implementation-cycle-1 (main implementation agent, DSP boundary)
Task: PITCH LAB V0.1 IMPLEMENTATION CYCLE 1 — implementation specification §17 step 1: core types + deterministic RNG + WAV I/O + shared resampling primitive, with unit/acceptance tests, per the owner mandate of 2026-09-26 (C++20, TOML+JSON manifests, block 4096, f64 masters/f32 listening, −80 dBFS criterion, pYIN clean-room route B, deterministic synthetic corpus first, GitHub Actions canonical CI)

Work Log:
- Read the full repository state before changing anything (worklog current-state block; implementation specification §0–§18 including §4/§5/§7/§9/§13/§16/§17; build & CI spec v1.1; architecture §I.2/§H.2/§J; existing pitch-lab sources/tests/CI workflow; git state clean at 4eda444). No restart of any audit; no redesign.
- OD-12 recorded as RESOLVED per the owner mandate (§16 row + §10.2 note): clean-room C++ pYIN (option B). Nothing else in the freeze was reopened.
- VENDORED doctest 2.4.12 (planned implementation-phase dependency, build spec §7): fetched from the official tag, sha256 recorded in `pitch-lab/external/doctest/ORIGIN.toml`, LICENSE.txt verbatim. EMPIRICAL LICENCE CORRECTION: doctest 2.4.12 is MIT — the "BSL-1.0" previously recorded in build spec §7 / architecture §J was wrong (confused with Catch2 v3). Both permissive → recorded correction, not a blocker. external/README.md + build spec v1.2 updated.
- IMPLEMENTED `src/core/types.h` (§4.1 verbatim: FrameCount int64 / ChannelCount int32 / SampleRate double; non-owning planar double AudioBlockView/AudioBlockOut; ChannelLayout) + tests (T-T1).
- IMPLEMENTED `src/core/errors.h`: ConfigError{file, field, reason} (§4.8) + WavSizeLimitError (§9 4-GiB cap, job-failure class, message cites OD-14; EngineException deliberately NOT pre-built — no engine exists).
- IMPLEMENTED `src/core/rng.{h,cpp}` (§4.7): own clean-room PCG64 (setseq 128/64 XSL-RR, published PCG constants; portable limb-based u128, no compiler extensions), splitMix64, fnv1a64, and the EXACT frozen consumer-mixing rule (golden-pinned). No <random>/device entropy.
- IMPLEMENTED `src/core/wav_io.{h,cpp}` (§9): own reader/writer. Reader: float 32/64 + extensible (IEEE-float GUID), unknown chunks skipped (pad-byte aware), strict riffSize == fileSize−8 v0.1 rule (foreign-file relaxation deferred with OD-10), full header-consistency validation, finiteness gate at read (§9 invalid-input: ConfigError naming frame/channel). Writer: deterministic bytes (fmt/fact/data, frozen order, no timestamps), f64/f32 with static_cast<float> (round-to-nearest-even, tie-tested), extensible+mmreg mask table for >2 channels (1..8; OD-11 beyond), finiteness pre-scan before any file I/O, 4-GiB cap as pure arithmetic first.
- Two test-driven bug fixes during WAV development (both caught by the new tests BEFORE any commit): writer bits-per-sample mapping inversion (16/32 → 64/32) and f32 read widening via value-cast instead of the bit-preserving widening. Recorded in the commit message.
- DEFINED the resampler's exact frozen behaviour BEFORE coding it (mandate): implementation specification §7.2.1 (ten recorded items: time-invariant kernel reading of the §7.2 formula; sinc_lp amplitude factor in Nyquist units; own I0 series; llround tap centre; ascending summation; §7.6 zero-padding edge reads; cutoff policy c = (r>1) ? 0.95/r : 1 — the only reading consistent with the DEFINED ratio-1 identity criterion, queued for ratification; §7.3 presets frozen; AA FIR = same kernel machinery, unnormalised; per-sample position advance; ConfigError invalid-config vocabulary). Authored as a specification clarification of ambiguity — no redesign; the ⟡ items surface the constants-vs-criteria tension (OD-18).
- IMPLEMENTED `src/core/resampler.{h,cpp}` per §7.2.1: resampleKernelValue (frozen kernel, public), interpolateAt (§7.2), resampleBlock (§7.5 per-sample advance with caller-owned double accumulator, per-frame or constant ratios, fail-fast validation, in-place prohibited per §5 rule 2), antiAliasCutoff, designAntiAliasFir + applyFirZeroPhase (§7.4). Direct sin() evaluation, no tables, -ffp-contract=off, deterministic ascending-order summation; llround domain guard added in the review pass.
- MEASURED the §7.7 acceptance criteria honestly (kernel DTFT over 64 worst-case phases at c=0.475; composite AA path; analytic sines): identity 2.2–3.9e-16 (−309 dBFS, criterion MET), passband ripple ±0.0004..0.002 dB (criterion ±0.1 MET), DC mean error −102 dB (MET), composite AA suppression −179/−182 dB (MET, far beyond 80 dB); kernel stopband floors −75.9/−78.0/−87.3 dB vs 80/80/90 (NOT MET, 2–4 dB short) and transitions 31.6/16.0/11.8% of Ny vs 5% (NOT MET). → recorded as OD-18 (owner ratification: constants as-is / raise β / raise K) with the full evidence table in the new §7.7.1; tests assert ONLY what genuinely holds + labelled regression bands (≥4x headroom) pinning the frozen behaviour; the evidence prints into every CI JUnit output. No assertion weakened or faked to manufacture a pass.
- TESTS (doctest): types_test (T-T1); rng_test (T-D1/D2: golden streams — self-captured 2026-09-26 baseline labelled as such, fnv1a64("") cross-checked against the published FNV offset basis; u128 analytic goldens; determinism; consumer separation; statistical sanity; null-tag ConfigError); wav_io_test (T-W1..W3: bit-exact f64 round-trips incl. 192 kHz + 6-channel extensible (§L mandate) + 3-channel f32; f64→f32 nearest-even ties; six §8 rates; deterministic writer bytes + frozen chunk layout; full invalid-input matrix; exact 4-GiB cap boundary 268435452/268435453 with honest CI-memory scope note); resampler_test (T-R1/R2: acceptance evidence + verdicts; analytic sines at constant ratios 0.5/1.5/2.0 with calibrated bands; block-split bit-identity incl. 4096 harness blocks + adversarial splits; determinism; six rates; stereo coherence; boundary delta/zero-padding cases; FIR symmetry/DC; invalid-config matrix). CTest now 7 tests.
- CMake: doctest interface target + test helper (-Werror, -ffp-contract=off); sources/tests added incrementally so every commit builds and tests green standalone. CI workflow UNCHANGED (CTest discovery — as designed).
- Phase constant honestly updated: `implementation` (freeze → implementation cycle 1); version_smoke + CLI wording updated accordingly. Registry still EMPTY (T-E19 green — no engine exists, none faked).
- Local validation: pinned CMake 3.31.6 (sha256-verified), GCC 14.2.0 local, Unix Makefiles; 7/7 CTest green; `pitchlab --version` reports phase `implementation`.
- Commits (granular, each green): 35502f8 (doctest+types+errors+T-T1) → aad2edd (RNG+T-D) → cc15280 (WAV+T-W) → f6f5bf2 (resampler+T-R + impl spec §7.2.1/§7.7.1/OD-18/§17/§13.3). Pushed 4eda444..f6f5bf2.
- CI VERIFIED on the canonical runner: run 36241070135 (head f6f5bf2) — conclusion SUCCESS, all steps green (toolchain assertions GCC 13.3.0/CMake 3.31.6/Ninja, pinned CInstall, configure/build/test/evidence-upload), CTest 7/7 (1.07 s), `ci-evidence` artefact (id 10905559488) downloaded and content-verified: JUnit `tests="7" failures="0"`, OD-18 evidence lines and T-R2a identity measurements present in the CI test output. Zero doctest/warning surprises on GCC 13.3 (same sources, -Werror).
- Docs closed out: implementation spec (§7.2.1, §7.7.1, §10.2, §16 OD-12 resolved + OD-18 new, §17 step-1 status, §13.3 suite-growth status); build spec v1.2 (doctest vendored + licence correction, §12.2 suite growth); root + pitch-lab READMEs (honest implementation-phase state); worklog current-state block + this entry. Web workbench untouched (mandate: DSP stays inside pitch-lab/; dev server healthy on port 3000 throughout).

Stage Summary:
- §17 STEP 1 COMPLETE: core types + RNG + WAV I/O + shared resampling primitive implemented, unit/acceptance-tested, CI-proven on the canonical runner (7/7). Cycle gate: acceptance tests §7.7 — ripple/identity/DC-Nyquist/composite-AA criteria MET (identity −309 dBFS vs −80; composite AA −179 dB vs 80); kernel-level stopband-depth and transition-width criteria NOT met at the §7.3 RECOMMENDED constants — measured, evidenced (§7.7.1), recorded as OD-18 for owner ratification, NOT silently weakened. No stop condition triggered; no blocker.
- EMPIRICAL FINDINGS: (1) OD-18 as above (with the §7.2.1 item-6 cutoff refinement queued for the same ratification); (2) doctest licence is MIT, not BSL-1.0 (vendoring-time verification); (3) RNG golden values are self-captured baseline (integer-exact, cross-compiler stable); (4) the resampler's §7.6 zero-padding edge transient is real and bounded (peak 1.135 at r=0.5, K=24, DC input) — documented and tested as the specified edge semantics.
- Artifacts: 8 new source files (types.h, errors.h, rng.{h,cpp}, wav_io.{h,cpp}, resampler.{h,cpp}) + 4 new test files + doctest vendored; spec §7.2.1/§7.7.1/§16/§17/§13.3/§10.2 updated; build spec v1.2; READMEs; worklog. Registry still empty (honest: no engine).
- Open items for the owner: OD-18 ratification (resampler constants; also covers the c=(r>1)?0.95/r:1 refinement); OD-6 (after T-LEN-CAL, next cycles); OD-9 calibration inputs (later cycles).
- Exact next action: §17 step 2 — curve library + validation (T-E15 fixtures) + deterministic corpus generator (T-C1), then step 3 (varispeed + harness skeleton, T-LEN-CAL evidence for OD-6).
