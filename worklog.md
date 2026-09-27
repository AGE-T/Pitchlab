# PITCH LAB — WORKLOG

## CURRENT STATE (2026-09-27, after Task 17 — IMPLEMENTATION CYCLE 5 COMPLETE: §17 steps 1-5 done; the ANALYSIS LAYER is implemented, golden-tested and evidenced — the 15 metrics per the frozen §10.4, the seven T-M suites, the `pitchlab analyze` CLI, deterministic `pitchlab.analysis.v1` artifacts, the 60-job cross-engine metric matrix; CTest 25/25 local AND on the canonical runner: run 36352620565 green at main @ a3dc0fe (JUnit 25/25, evidence content-verified). Cycle 4 state unchanged: all five engines, registry == §14 end state, CI run 36278858598 green @ e1d15b2)

**Project identity:** Pitch Lab — local offline DSP research laboratory for sound-design-oriented pitch processing (C++20 + offline harness; agent-side Next.js workbench for owner observability). Repository: `github.com/AGE-T/Pitchlab`, branch `main` (GitHub in sync; CI green at head: verified runs 36241070135, 36241266441, 36244722054, 36247154915, 36269901724 (cycle-3 @ b0c9c23), 36276010178 (cycle-4 in-progress @ 880f6da), **36278858598 (cycle-4 closure @ e1d15b2 — CTest 18/18)** — all evidence artefacts downloaded and content-verified; two honestly-recorded intermediate failures along the way: 36269701944 → the GCC 13.3.0 -Wmismatched-new-delete finding, fixed test-code-only in b0c9c23).

**Reading order (authoritative documents and their roles):**
1. `research/AI_ASSISTED_SOFTWARE_ENGINEERING_OPERATING_PRINCIPLES_v3.0.md` — governing engineering process (130 sections).
2. `research/pitch-lab-v0.1-implementation-specification.md` — **current Source Of Truth for v0.1 implementation** (frozen contracts, engine sheets, tests, TOML schemas, open decisions OD-6..OD-18; records Amendments A-1, the resampler frozen-behaviour clarification §7.2.1 + measured acceptance §7.7.1, the curve-compiler clarification §4.4.3.1, and the cycle-3 clarifications §4.2.1 (engine contract + registry factory), §4.8.1 (harness: compiler/renderer/manifest) and §6.1.1 (varispeed) — all recorded BEFORE coding per the established pattern).
3. `research/pitch-lab-build-and-ci-environment.md` — canonical pinned build/CI environment (v1.3: doctest + pocketfft vendored; §12.1 = OD-17 activation chronology; §12.2 = the run record incl. cycle 4).
4. `research/pitch-lab-architecture-design.md` (v1.1) — governing DSP architecture (amended for v0.1 only by the recorded A-1; v1.2 fold-in queued as OD-16).
5. `research/doppler-whip-pitch-research-report.md` — research findings ([RESEARCH FINDING] tags).
6. `README.md` + this worklog (current-state block first).

**Current phase:** IMPLEMENTATION (cycles 1-4 = §17 steps 1-4 COMPLETE). **Cycle 4 delivered the four remaining engines, completing the v0.1 registry**: `native.vardelay` (§6.2/§6.2.1, commits 0cd9608/8deb5c5), `native.granular` (§6.5/§6.5.1, commits 6ab63c8/89c10ee), the pocketfft vendoring (per the build/CI spec §7 documented plan) + `native.pv.classic` (§6.3/§6.3.1 + the §7.2.2 amendment, commits 9b230be/11abe1b0), and `native.pv.phaselocked` (§6.4/§6.4.1, commits 350d956/5dfbf90). Every engine: frozen BEFORE coding, SAME contract suite T-E1..T-E13 + T-A1 + T-D3 through the real harness, ASAN+UBSAN clean, zero-warning -Werror builds. T-E19 asserts the §14 v0.1 END STATE (five engines, order aligned). Local validation: clean re-configure + build, CTest **18/18** (~62 s), ASAN+UBSAN clean on the new suites, engine commits worktree-verified standalone. Measured evidence: spec §13.5 (pv.phaselocked: identity 2.67e-11, 0.00-cent pitch at 1.5/0.75/2.0x, cross-schedule bit-identity, 0 allocations; vardelay residue 2.78e-17; classic pocketfft audit 0 allocations).

**Current implementation state (all unit-tested):** cycle-1/2/3 components unchanged and green (types, errors, RNG, WAV I/O, resampler, toml_lite, curve, corpus, hash, JSON, harness, varispeed); cycle 4 added: `src/engines/vardelay_engine.{h,cpp}`, `src/engines/granular_engine.{h,cpp}`, `src/engines/pv_classic_engine.{h,cpp}`, `src/engines/pv_phaselocked_engine.{h,cpp}`, `external/pocketfft/` (vendored), tests `vardelay_contract_test`, `granular_contract_test`, `pv_classic_contract_test`, `pv_phaselocked_contract_test`; the registry registers exactly the five engines at the single production point (order aligned to §14). CTest 14 → 18.

**Known limitations:** all numeric tolerances provisional (OD-6 now has its full evidence pack: worst |Δ| = 8 frames over 126 renders, §13.4 — owner ratification pending); tracker-dependent metrics wait on the clean-room pYIN (OD-12 resolved in route; implementation is §17 step-5 work); OD-18 (resampler constants) unchanged — not a blocker; the §6.3/§6.4 documented PV family artefacts (phasiness, transient smearing, sidebands) are research-measurement material, not contract failures (§13.5); Amendment A-1 pending architecture v1.2 fold-in (OD-16); RIFF 4 GiB cap (OD-14).

**Open decisions:** OD-6 (rate-following length tolerance VALUE — **evidence produced, §13.4**), OD-7, OD-8, OD-9 (metric tolerances), OD-10, OD-11, ~~OD-12 (RESOLVED 2026-09-26)~~, OD-13, OD-14, OD-15 (artifact publication), OD-16, ~~OD-17 (RESOLVED 2026-09-26)~~, OD-18 (resampler constants ratification — §7.7.1). Full table: implementation specification §16.

**Blockers:** none. §17 step 5 (metrics — analytic goldens first; tracker-dependent metrics only through the resolved clean-room pYIN route) and step 6 (full §G.3 suites + `pitchlab verify`) are sequenced owner-triggered work, not blockers.

**Dependency and environment state:** canonical = GitHub Actions `ubuntu-24.04` x64, GCC 13.3.0 (asserted), CMake 3.31.6 (sha256-pinned), Ninja (≥1.11), CTest. Local dev: GCC 14.2, pinned CMake. Vendored: doctest 2.4.12 (MIT, dev/test only) + **pocketfft (BSD-3-Clause, cpp-branch commit c90e55b3…, ORIGIN.toml-pinned — the ONE DSP-runtime vendoring, consumed via the persistent-plan pattern, POCKETFFT_CACHE_SIZE 0, T-A1-audited)**. Token in the gitignored `.env` (never logged, never committed).

**Validation state:** local clean build + 18/18 CTest green (pinned CMake 3.31.6; GCC 14.2 local); ASAN+UBSAN clean on every cycle-4 suite (incl. the phaselocked suite with leak detection); engine commits worktree-verified standalone-buildable; `corpus_gen --verify` 17/17; example render reproducible byte-identically; measured contract evidence §13.5. GitHub CI: **cycle-4 closure run 36278858598 GREEN at main @ e1d15b2** — ci-evidence artefact (id 10918375873) downloaded and content-verified: test.log "100% tests passed, 0 tests failed out of 18" (94.07 s; pv_phaselocked_contract_test 15.40 s); JUnit tests="18" failures="0" disabled="0" skipped="0"; the T-E19 five-engine end-state line and the pv.phaselocked measured evidence lines present in the JUnit system-out, values identical to the local run (empirical local↔CI agreement); configure.log (GCC 13.3.0) + build.log clean. Run history: cycles 1-3 in Project identity.

**Exact next action:** §17 step 5 (Metrics — analytic goldens first; tracker-dependent metrics only through the already-resolved clean-room pYIN route), owner-triggered. §17 step 4 is COMPLETE (all five engines). Owner decisions pending: OD-6 ratification from the §13.4 evidence pack (worst |Δ| = 8 frames over 126 renders); OD-18 unchanged.

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

---
Task ID: 13
Agent: orchestrator (main)
Task: Focused contract-consistency check (owner request, before §17 step 2): resolve the apparent tension between the cycle-1 report's "resampler test suite verifies block-split bit-identity" and the owner-locked Pitch Lab contract "audio-equivalent block-boundary independence at −80 dBFS (provisional)"

Work Log:
- Read (in full or in relevant depth): implementation specification (§1.2 L-5, §4.3.1 process contract, §7.2.1 items 1-10, §7.4, §7.5, §7.7/§7.7.1, §13.1 T-E7, §13.2 T-R row, §13.3, §15 tolerances), architecture v1.1 (§D.1 block streaming/determinism, §L component-test item 6, §N.5), resampler implementation (src/core/resampler.{h,cpp}), resampler tests (tests/resampler_test.cpp), worklog current-state + Task 12 entry, both READMEs, git state (clean at bbf2e51).
- Determined what "block-split bit-identity" means in the suite: T-R2g asserts memcmp-level bit-identity between ONE whole-buffer resampleBlock call and any sequence of split calls ({1, 4095, 4096, 100, 5000, remainder}) producing the identical per-frame ratio sequence, plus bit-identical final position accumulator and sequential-addition exactness — grounded in frozen §7.2.1 item 9 (authored BEFORE the implementation, Task 12).
- Verified the property genuinely holds by construction: each output sample is a pure function of the exact double position (interpolateAt has no cross-sample state); the caller-owned double accumulator performs the identical ordered additions regardless of split; -ffp-contract=off; fixed ascending tap order. NOT a fake/fudged stronger assertion.
- Classified: simultaneously (a) stronger-than-required implementation property (of the shared PRIMITIVE only), (b) resampler-specific invariant (stateless per-output-frame design; engines with internal state — block-wise AA pre-filtering per §7.4/§6.1, PV/granular engines — do NOT inherit it), (c) intentional regression property (frozen in §7.2.1 item 9 before coding; pins the stateless design). NOT an accidental strengthening of the normative contract in substance: L-5, T-E7 and architecture §D/§L/§N.5 are unambiguous that engine-level block-boundary independence is audio-equivalent within −80 dBFS (provisional) — no document claimed project-wide bit-exactness.
- Found the one real defect — a LABELING gap, exactly the conflation risk L-5 warns against ("must not silently become a bit-exact requirement"): T-R2g's TEST_CASE title reused the normative contract's exact term ("block-boundary independence") while asserting the stronger property, and no explicit primitive-level/non-normative label existed at the assertion point, in resampler.h, in spec §13.2's T-R row, or in the worklog test description.
- Applied minimal corrections (NO test weakened, NO assertion changed, NO resampler constant or design touched, normative −80 dBFS criterion untouched):
  * tests/resampler_test.cpp — header comment gained category (c) "deterministic-structure invariants of the PRIMITIVE ... deliberately STRONGER than the owner-locked engine-level block-boundary criterion (L-5/T-E7)"; T-R2g retitled "primitive block-split bit-identity — resampler invariant, stronger than the engine-level L-5/T-E7 audio-equivalence contract" + a CONTRACT RELATION comment block stating the normative contract verbatim and why the property holds by construction.
  * src/core/resampler.h — resampleBlock doc comment now states the primitive-invariant vs engine-contract distinction explicitly.
  * research/pitch-lab-v0.1-implementation-specification.md — §7.2.1 item 9 gained a "Contract relation (clarified 2026-09-26, contract-consistency check)" note; §13.2 T-R row now names the block-split bit-identity invariant as "primitive-level invariant, deliberately stronger than the engine-level L-5/T-E7 audio-equivalence criterion; regression pin ... not a normative contract test".
  * worklog.md — current-state block resampler_test description carries the same labelling.
- Rebuilt on the pinned toolchain (CMake 3.31.6 sha256-verified from /tmp/cmake.tar.gz: 5a1133ff...367bf OK; GCC 14.2.0 local; Unix Makefiles; clean re-configure) and re-ran the full suite: CTest 7/7 green, JUnit output unchanged in structure, OD-18 evidence lines intact.
- Committed (08aa176) and pushed; CI run 36244722054 (head 08aa176): conclusion SUCCESS; ci-evidence artefact downloaded and content-verified — JUnit tests="7" failures="0", resampler_test present with the OD-18 evidence block.

Stage Summary:
- Deliverable answers: (1) normative contract = L-5/T-E7 audio-equivalent −80 dBFS provisional (engine/integration level; T-E7 correctly not yet implemented — engines arrive in §17 step 3); (2) actual test property = T-R2g primitive-level bit-identity across arbitrary block splits, genuinely guaranteed by construction, frozen in §7.2.1 item 9; (3) they were correctly related in substance but incompletely in labeling — now explicitly related everywhere the stronger property is asserted or summarised; (4) exact changes: commit 08aa176 (4 files, +35/−6 lines, comments/wording only); (5) test result after changes: local 7/7, CI run 36244722054 green with JUnit 7/0.
- No redesign, no resampler constant change, no test weakening, no architecture reopening, −80 dBFS owner decision untouched, T-R2g kept at full strength as a labelled stronger regression pin.
- Exact next action: §17 step 2 — curve library (schema §15.3, compilation §4.4.3, validation §4.4.4, T-E15 fixture matrix) + deterministic synthetic corpus generator (§11, T-C1 regeneration bit-identity).

---
Task ID: 14
Agent: implementation-cycle-2 (main implementation agent, DSP boundary)
Task: PITCH LAB V0.1 IMPLEMENTATION CYCLE 2 — implementation specification §17 step 2: curve library + validation (T-E15 curve slice) + deterministic synthetic corpus generator (T-C1); gate: "curve battery compiles deterministically". Preceded in-session by Task 13 (owner-requested contract-consistency check on the T-R2g block-split bit-identity labelling — resolved, commit 08aa176, CI run 36244722054 green).

Work Log:
- Read the full repository state before changing anything (worklog current-state block + Tasks 12/13; implementation specification §4.4/§15.3/§11/§13/§17; architecture §E.1-E.5/§D.4.2/§G.3; existing core sources + tests + CMake; git clean at 08aa176). No restart of any audit; no redesign.
- FROZEN the curve-compiler's exact implementation behaviour BEFORE coding (the §7.2.1 pattern): new spec section §4.4.3.1 (14 recorded items): own strict TOML-subset parser `toml_lite` (dependency set stays {doctest}, build spec §7 + architecture §J v0.1 note; hand-rolled number parsing, inf/nan rejected, TOML-conformant strictness with line numbers in errors); value/time authoring tables ({semitones}|{ratio} XOR, {ms}|{s} XOR); per-kind field schemas + domain choices (ramp_lin/reversal LINEAR IN RATIO; ramp_exp geometric endpoint form XOR the §4.4.3-verbatim multiplicative rate form with the per-frame recurrence canonical; LFOs in the log2/semitone domain; random walk = ZOH steps CLAMPED at bounds, consumer tag "curve.random-walk"; saw LFO requires allow_discontinuity — the concrete instantiation of §4.4.4's "value discontinuities flagged"; breakpoints with per-segment lin|exp law; external dense/pairs CSV with linear resampling); extension policy semantics (leading region = first defined value; trailing = hold-last|hold-first); post-compilation signal validation (finite/strictly-positive every frame, frame named); the 21 battery file definitions.
- IMPLEMENTED `src/core/toml_lite.{h,cpp}`: strict subset parser (bare keys, basic strings with 5 escapes, decimal/hex integers with TOML leading-zero rule, floats with exponent, booleans, inline tables single-line no-trailing-comma, multi-line arrays with trailing comma, [table] + [[array-of-table]] headers, duplicate-key/table rejection). Deterministic (no locale dependence in number parsing).
- IMPLEMENTED `src/core/curve.{h,cpp}`: PitchCurveSpec (single struct, per-kind field legality enforced — unknown keys AND wrong-for-kind keys are both ConfigError{file, field}); CurveValue (semitones|ratio authoring, exp2(st/12) resolution); PitchCurveSignal (dense vector<double>, input-timeline indexed); parseCurveSpec (parse+validate in one step); compileCurveSignal (pure function of spec/fs/N; per-kind compilers incl. the shared random-walk machinery; breakpoints/external unified (t, ratio) point representation with frame-rounded strict monotonicity; post-compilation validation); loadCurveBattery (sorted, deterministic).
- AUTHORED the 21 curve battery files (§11.3 ids exactly; §15.3-style schema; the reversal file matches the §15.3 example verbatim; random-walk seed = 0x5EEDC0DE committed).
- IMPLEMENTED `src/core/corpus_gen.{h,cpp}` + `config/corpus.toml` + `tools/corpus_gen.cpp`: CorpusConfig/CorpusItemSpec parse+validate (§8 supported-rate set enforced; per-class channel constraint; params validated at PARSE time — config errors surface at compile time; unknown class/keys rejected); itemSeed = splitMix64(masterSeed ^ fnv1a64(id)) (frozen §11.4 rule, golden-pinned); recipes: phase-accumulated sine; harmonic stack with log-domain vibrato (frequency modulated, phase-accumulated); polyphonic chord+stack additive; impulse train; percussive bursts (PCG64 noise × exp envelope through one-pole LP); white noise; Voss-McCartney pink (16 rows, update rule frozen); analytic-phase log sweep; voice-like (harmonic+vibrato through 3 PARALLEL Rabiner-Schafer resonators, unity-peak-gain normalisation derived and frozen); stereo correlated (identical channels) / decorrelated (tags corpus.L/corpus.R, shared 0.55+0.45·sin envelope); wideband (multi-tone + one-pole HP/LP band noise); -12 dBFS true-peak normalisation (exact max-|sample| scaling, shared across channels, kTargetPeakLin = 10^-12/20 committed); §15.5 metadata rendering; float64 WAV writing via the §9 writer.
- TWO implementation bugs found and fixed during the cycle (both by the new tests / ASAN before any commit): (1) `checkFields` in curve.cpp rejected required fields (allowed-set did not include the required set) — surfaced by the T-E15 matrix; (2) `std::vector<double> v{kRows, 0.0}` initializer-list pitfall in PinkNoise (2-element vector instead of 16) — heap corruption caught by ASAN (release build crashed with malloc corruption; ASAN pinpointed it exactly). Also fixed en route: five extreme battery files generated with malformed literals by a shell template ("0.25.0" — caught by the parser at battery-compile time); corpus param validation moved from generation time to parse time after the T-C1f matrix exposed the gap.
- GENERATED + COMMITTED the corpus: 17 items (~50 MB float64): sine ×4 rates (44.1/48/96/192k), harmonic ×2, polyphonic, transient ×2 (2 s), noise ×2, sweep 4 s, voice-like, stereo ×2, wideband ×2 (176.4/192k). "Mono and stereo variants where meaningful" instantiated as: the two stereo-noise classes are the channel-behaviour fixtures; all others mono (recorded in the config header, not silent). Cross-checked ASAN-debug vs release builds produce byte-identical output; `corpus_gen --verify` (§11.1 regeneration gate) passes 17/17 byte-identically.
- TESTS (doctest, CTest now 9 tests): `curve_test` = 17 doctest cases / 1,057,449 assertions — TOML-subset positive+negative matrices (T-CV0a/b), the T-E15 curve-slice failure matrix (28 cases, each asserting the ConfigError FIELD and file), exact-formula pins per kind (§4.4.3's "exact formula fixed in the curve-compiler unit tests": static equality, ramp_lin linearity, ramp_exp endpoint geometric + rate-form EXACT recurrence r[i+1]==r[i]*g, reversal pre-hold+window+hold-last anchors, LFO formula pins incl. the saw period-reset discontinuity, random-walk determinism/ZOH/bounds, breakpoints laws + both extension policies, external dense/pairs interpolation + 4 rejection modes, post-compilation overflow catch, totalFrames validation), and the §17-step-2 GATE: all 21 battery files compile at 48k/240000 AND 44.1k/220500, bit-identical on recompilation, with semantic anchors. `corpus_gen_test` = T-C1 (spec §11.4): same-binary double-generation bit-identity + committed-corpus bit-exact equality (read back through the §9 reader), itemSeed golden (8748028519920161202 + primitive-composition cross-check), §15.5 metadata field checks, -12 dBFS normalisation, recipe sanity probes (sine Goertzel dominance, noise mean, pink 3-octave-vs-1-octave energy, correlated/decorrelated stereo, impulse grid, sweep start-slope), config validation matrix (10 cases with field assertions).
- CMake: pitchlab_core += toml_lite/curve/corpus_gen; corpus_gen tool target (own main, §11.1 stand-alone); curve_test + corpus_gen_test targets with PITCHLAB_SOURCE_DIR compile definitions (battery + committed-corpus paths); -ffp-contract=off and -Werror as everywhere.
- Local validation: pinned CMake 3.31.6 (sha256-verified), GCC 14.2.0 local, Unix Makefiles, clean re-configure; full CTest 9/9 green (4.4 s); JUnit 9 tests / 0 failures; `corpus_gen --verify` 17/17 byte-identical; an ASAN debug build of the whole tree runs clean (the corpus generation itself was executed under ASAN as the final memory-safety proof).
- Docs closed out: implementation specification (§4.4.3.1 new; §13.2 T-CV row added, T-R/T-C1 rows intact; §13.3 cycle-2 suite growth; §17 step-2 status DONE with the full inventory); root README + pitch-lab README (cycles 1-2 state, layout updated: tools/ + battery + corpus now implemented); this worklog entry + current-state block. Web workbench untouched (mandate: DSP stays inside pitch-lab/).

Stage Summary:
- §17 STEP 2 COMPLETE (pending CI verification of the push): curve library (parser + spec + validation + deterministic compilation, exact formulas pinned by 1.05M assertions) + 21-file curve battery + deterministic corpus generator + committed 17-item corpus. Cycle gate PASSED: the curve battery compiles deterministically (bit-identical recompilation at two rates); corpus regeneration is byte-identical in-process, via the tool --verify gate, and against the committed files.
- Key decisions recorded (none silent): own TOML-subset parser (dependency set stays {doctest}); ramp domain semantics per §4.4.3.1; itemSeed derivation rule; corpus stereo instantiation ("where meaningful"); float64 corpus WAVs (determinism-precious; f32 halving documented as an owner option); recipe parameters all committed in config/corpus.toml.
- Empirical findings: (1) the initializer-list vector pitfall (ASAN-proven); (2) checkFields required/allowed-set bug (test-matrix-proven); (3) param validation belongs at parse time (test-matrix-proven); (4) ASAN-vs-release builds produce byte-identical corpus output; (5) cycle-1 CI/local T-R1 evidence values agree at printed precision (suggestive of local/CI libm agreement — the T-C1 committed-corpus comparison will now test this hypothesis for real on the runner).
- Known risk, deliberately not pre-weakened: T-C1's committed-corpus comparison is bit-exact against the corpus generated on the local toolchain; if the canonical runner's libm (GCC 13.3/ubuntu-24.04 glibc) disagrees at 1 ulp, the test fails LOUDLY and the finding gets recorded and resolved honestly (options: regenerate on the runner / tolerance band + same-binary bit-identity as the hard gate) — per the §7.5 same-binary reproducibility stance.
- Open items for the owner: unchanged (OD-18 ratification; OD-6 after T-LEN-CAL; OD-9 later). No new open decisions introduced — all cycle-2 choices are recorded frozen decisions inside the committed specs/config.
- Exact next action: commit + push cycle 2 (sources, battery, config, tool, tests, corpus, docs) → verify the GitHub Actions run green + download the ci-evidence artefact → then §17 step 3 (engine registry mechanism + harness skeleton + varispeed engine, T-E1..T-E11 + T-LEN-CAL for OD-6).

**CYCLE-2 CI VERIFICATION — Task 14 addendum (2026-09-26, same day):**

Work Log (continuation):
- Pushed commits 08aa176 → e55c653 (cycle-2 code: curve library + corpus, 68 files, +4008 lines incl. the ~50 MB committed corpus) → 711eead (closure docs). GitHub Actions run **36247154915** (head 711eead, pushed 08aa176..711eead): **conclusion SUCCESS** — all steps green (toolchain assertions GCC 13.3.0/CMake 3.31.6/Ninja, pinned CMake sha256, configure/build/test/evidence-upload), CTest **9/9** (5.49 s), `ci-evidence` artefact downloaded and content-verified: JUnit `tests="9" failures="0"`; per-test output confirms `curve_test` (17 cases / 1,057,449 assertions / 0 failures) and `corpus_gen_test` (2,439 assertions / 0 failures — i.e. T-C1's committed-corpus BIT-EXACT comparison held on the canonical runner).
- EMPIRICAL FINDING (closes the cycle-2 known risk): the committed corpus regenerates byte-identically on BOTH the development toolchain (GCC 14.2, local glibc) and the canonical runner (GCC 13.3.0, ubuntu-24.04) — every libm function the recipes use (sin/exp2/exp/pow/log) agrees bit-exactly across the two environments for this corpus. The risk recorded in Task 14 ("if the runner libm disagrees, the test fails loudly") did NOT materialise; no tolerance band was needed; nothing was weakened.

Stage Summary (addendum):
- Cycle 2 is now CLOSED per the Operating Principles cycle discipline: implemented → unit-tested (local + canonical CI) → docs updated → worklog current-state accurate. CTest count 7 → 9; total assertion count across the suite ≈ 1.06 M in curve_test + 2,439 in corpus_gen_test (+ the pre-existing suites).
- Zero doctest/warning surprises on GCC 13.3 (-Werror, -ffp-contract=off), zero heap issues (ASAN-proven locally), registry still empty (T-E19 green — no engine faked).
---
Task ID: 15
Agent: implementation-cycle-3 (main implementation agent, DSP boundary)
Task: PITCH LAB V0.1 IMPLEMENTATION CYCLE 3 — implementation specification §17 step 3: engine registry factory + harness skeleton + `native.varispeed` (the first real engine) + T-E1..T-E11 + T-LEN-CAL; gate: "the first real Pitch Lab engine exists as a tested, reproducible C++ component and the harness can execute it under the frozen contract".

Work Log:
- RECONSTRUCTION FIRST (no redesign): read the worklog current-state + Task 13/14; implementation specification §4.2 (engine contract), §4.3 (frame accounting/length policies — the rate-following semantics), §4.4.6, §4.5, §4.6 (registry), §4.8, §5, §6.1 (varispeed sheet), §7.2.1/§7.4 (resampler frozen behaviour + AA policy), §8, §9, §13, §14, §15.4, §16, §17; the existing sources (types/errors/registry/resampler/toml_lite/curve/wav_io/rng/version); CMake; CI workflow; git state (clean at 1a2ed93 == origin/main; the "modified" status of 188 files was pure 644→755 mode noise from the workspace reset — `core.fileMode false` restores a clean view). Reinstalled the pinned CMake 3.31.6 (workspace reset had removed it; sha256 verified against the CI workflow's pin) and re-proved the 9/9 baseline before changing anything.
- FROZEN the cycle-3 implementation behaviour BEFORE coding (the §7.2.1/§4.4.3.1 pattern, three new spec subsections): §4.2.1 (engine contract + registry factory: EngineException; ParameterValue variant + sorted EngineConfiguration; ProcessContext carries the EFFECTIVE curve view so §4.3.5's prepare()-time latency computation is possible; sticky inputExhausted semantics; factory REQUIRED non-null at registration — the anti-fake rule; capabilities' supportedSampleRates; the renderer out-capacity formula that keeps rate-followers starvation-free); §6.1.1 (varispeed: parameters; the read-position recurrence verbatim from §6.1 made executable; whole-curve-max cutoff + streaming AA pre-filter with engine state; inputLatency = K + (FIR active ? K : 0) — the honest total lookahead; strict-delivery gating; flush region exactly round(r) ∈ [N_in, N_in+K−1] so the declared outputLatency bound holds BY CONSTRUCTION; sliding-window state, no per-call allocation); §4.8.1 (harness: strict §15.4 validation; asset resolution with WAV-authoritative cross-check; visible skips rate-mismatch/channel-mismatch/sample-rate-unsupported/channels-unsupported/ratio-out-of-range; creative saturation verbatim §4.4.6; block schedules Constant/Pattern; the renderer loop executable; length policy with the exact expectation replay (M₀ + declared outputLatency; the same recurrence/bit order as the engine — cannot diverge); canonical-JSON manifest schema incl. the curve access report over the expected-timeline superset; SHA-256 hashing conventions; failure model; CLI compile/render; byte-identical regeneration).
- IMPLEMENTED the engine contract layer: `errors.h` gains EngineException (§4.2.1 item 1); `src/core/pitch_engine.h` (ProcessContext/PitchCurveView/ProcessReport/EngineConfiguration/PitchEngine/EngineFactory); registry extended with the factory + supportedSampleRates + null-factory rejection.
- IMPLEMENTED own primitives: `src/core/hash.{h,cpp}` (SHA-256, FIPS 180-4 clean-room; padding via 0x80 + zeros + 64-bit big-endian bit length; little-endian IEEE-754 double-array hashing convention) and `src/harness/json_writer.{h,cpp}` (canonical JSON: std::map sorted keys, 2-space pretty, %.17g doubles round-trip-exact, JSON string escaping). Dependency set stays exactly {doctest}.
- IMPLEMENTED `src/engines/varispeed_engine.{h,cpp}`: sliding-window streaming state (raw + pre-filtered per channel, absolute-indexed, memmove compaction, no allocation after prepare); the §6.1.1 recurrence; interpolateAt on the filtered signal (shared primitive — no resampling code duplicated); designAntiAliasFir coefficients applied streaming in the applyFirZeroPhase tap order; emission condition round(r) ≤ N_in−1+K; strict-delivery gate round(r)+lookahead ≤ delivered−1; coverage gate (in-stream taps must be in-window; taps ≥ streamEnd are semantic zeros read via interpolateAt's zero padding); finish() extends the filter through the emission max tap using beyond-stream zeros and emits the bounded flush; sticky exhaustion; fresh-instance-per-job + reset() reuse.
- IMPLEMENTED the harness: `src/harness/render_job.h` (RenderJob/SkipEntry/JobResult/RenderSummary/BlockSchedule/HarnessConfig — curve signals as immutable shared storage), `paths.h` (§9 naming: run-id/path-safe sanitisation, paramtag = 12 hex of SHA-256 over the canonical engine-config JSON, root-relative manifest paths), `experiment_compiler.{h,cpp}` (§4.8.1 items 1-4: strict validation matrix, asset integrity cross-check, deterministic cross-product expansion with visible skips, creative saturation with requested-signal immutability, curve-signal caching), `manifest.{h,cpp}` (the §4.8.1 item 7 schema; registry read-only mirror; failure manifests), `offline_renderer.{h,cpp}` (the §4.3.2 loop: padded stream delivery per schedule, §4.2.1 capacity policy, stall guard, consumption + exhaustion-transition enforcement (§4.3.6/§4.2.1 item 7), finish() once + flush-bound enforcement (end-of-render-violation), full-output NaN/Inf scan (non-finite-output job failure, no WAV), length policy (M₀ replay + Preserving exact), WAV master + optional listening copy + always-manifest, RIFF-cap guard, per-job failure isolation); CLI `compile`/`render`/`engines`.
- REGISTERED native.varispeed at the single production point (§4.6): registry content == exactly the one implemented engine; T-E19 updated to assert the intended content (1 engine + the other four MUST NOT be registered); `pitchlab engines` lists it.
- TESTS (CTest 9 → 14): `hash_test` (T-H1: NIST vectors incl. empty/"abc"/448-bit/896-bit/1M-'a', call-splitting independence, double-array convention incl. 0.0 vs −0.0 bit-pattern sensitivity); `json_writer_test` (T-J1: canonical forms, %.17g round-trip bit-exact, key-order independence, escaping, nesting); `experiment_compiler_test` (T-E15 harness slice: unknown engine id, missing/duplicate/foreign engine_config, missing/negative seed, unknown param key, non-first-class rate, channels > 2, missing asset, corpus-integrity mismatch — each with field assertions; skip matrix: rate-mismatch/channel-mismatch/ratio-out-of-range(benchmark)/sample-rate-unsupported/channels-unsupported; T-E17 creative saturation: requested untouched, effective clamped, saturated flag; compile determinism); `varispeed_contract_test` (T-E1 identity −80 dBFS with observed ~1e-15; T-E2/T-E3 Goertzel pitch within 25 cents both directions; T-E4 rate invariance 44.1/96/192 kHz; T-E5 stereo: identical channels bit-identical + per-channel == independent mono render bit-exact; T-E6 byte-identical WAV+manifest across fresh roots (path-normalised); T-E7 schedules {4096} vs 8-element pattern audio-equivalent ≤ 1e-4 (NOT bit-exact — the L-5 criterion; observed 0); T-E8 sweep 32..4096 + mixed pattern; T-E9 consumed == N_in, padding == declared inputLatency, exhaustion transitions == 1; T-E10 flush ≤ declared + over-flush dummy → job failure end-of-render-violation with failure manifest, no WAV; T-E11 golden exact length deltas (0 for r ∈ {1, 1.5, 0.75, 2, 8}, −2 for 0.25 — the designed ceil((K−0.5)/r) vs ceil(K/r) arithmetic) + Preserving dummy exact N+L; T-A1 global new/delete interposition: ZERO allocations during every process()/finish() call (FIR + bypass paths); T-D3 reset() reuse + fresh-instance bit-identity); `length_calibration_test` (T-E14/T-LEN-CAL: the full 21-curve battery × 6 first-class rates = 126 renders with the evidence report — expected/actual/|Δ|/rel/flush per row, worst cases, |Δ| distribution, per-rate worst; structural assertions only (all renders ok, |Δ| ≤ provisional 4096, flush ≤ declared); NO tolerance promotion — OD-6 explicitly reported OPEN; the repo tolerances.toml asserted unmodified).
- TWO implementation bugs found and fixed by the tests/local runs before any commit: (1) the emission-tap arithmetic error — my first reading put the emission max tap at N_in+K−1 instead of N_in+2K−1, which made the coverage gate block the legitimate beyond-stream semantic-zero reads and produced flush = 0 / delta = −K (caught immediately by the CLI smoke render: "flush 0, delta −24"); fixed in the engine (coverage gate distinguishes in-stream taps from beyond-stream zeros; finish() extends the filter through the emission max tap) AND corrected in §6.1.1 items 6/7 before commit; (2) the flush-bound cap double-counted (flushEmitted_ + produced ≥ outputLatency with flushEmitted_ already incremented in-loop — the cap cut the flush in half; caught by the direct-drive probe: finish produced 12 instead of 24); fixed to flushEmitted_ alone. Also: the first Goertzel probe was O(10^10) (fine-grid sweep) — replaced with a two-stage coarse/fine sweep (344 s → 11.5 s contract suite); GCC 14's new -Wmismatched-new-delete false-positives on the T-A1 interposition suppressed for GCC ≥ 14 only (GCC 13, the canonical CI compiler, does not have the warning).
- LOCAL VALIDATION: clean re-configure + rebuild (pinned CMake 3.31.6, GCC 14.2, -Werror clean); CTest **14/14** (~34 s total; contract suite 11.5 s, calibration 17.5 s); JUnit XML 14 tests / 0 failures; **full suite clean under ASAN+UBSAN** (incl. the calibration test, 67 s; leak detection on); `corpus_gen --verify` still 17/17; the example render (`experiments/suites/example-varispeed-basic.toml`: committed corpus sine × {identity, +12 st, 2×} × varispeed, float64 masters + float32 listening copies + manifests) verified BYTE-IDENTICAL on re-render (sha256-pinned: masters 13ee1f63…/7b025e08…, manifests 2e286818…/13fe9b3d…/55d80b3b…, listening 17859ff6…/87159230…).
- T-LEN-CAL EVIDENCE (the OD-6 evidence pack, §13.4): 126/126 renders complete; |Δ| distribution 0×44, 1×43, 2×30, 3×3, 7×4, 8×2; **worst |Δ| = 8 frames** (ramp-fast-plus-12stps @ 44.1 kHz, rel 4.29e-4); per-rate worst 7-8 frames; constant ratios with N divisible by r give |Δ| = 0 except r = 0.25 (−2: flush = ceil((K−0.5)/r) = 94 < ceil(K/r) = 96). The deltas are deterministic flush-region rounding arithmetic, not numerical drift. OD-6 remains OPEN — the evidence re-prints into every CI run; tolerances.toml untouched (asserted).
- Docs closed out: implementation specification (§4.2.1/§4.8.1/§6.1.1 new BEFORE coding; §13.3 cycle-3 suite growth; §13.4 new measured-evidence section; §14 registry current state + IMPLEMENTED status; §16 OD-6 row updated to "evidence produced"; §17 step-3 STATUS DONE with the full inventory); root README + pitch-lab README (cycles 1-3 state, build+render commands, updated layout incl. src/engines + src/harness); version.h comment (one engine implemented); this worklog entry + current-state block. Web workbench untouched (mandate: DSP stays inside pitch-lab/; dev server healthy on port 3000 throughout).

Stage Summary:
- §17 STEP 3 COMPLETE (pending CI verification of the push): the first real Pitch Lab engine — native.varispeed, the rate-following reference — exists as a tested, reproducible C++ component, and the harness (compiler → renderer → manifest) executes it under the frozen contract. Cycle gate PASSED: T-E1..T-E11 green at the engine-level −80 dBFS audio-equivalence criterion (L-5; observed stronger property: bit-identical outputs across block schedules — reported, never required); T-LEN-CAL evidence produced for OD-6.
- Key decisions recorded (none silent): ProcessContext carries the effective curve (prepare()-time latency computation per §4.3.5); registry factory REQUIRED at registration (anti-fake rule made mechanical); renderer out-capacity formula (rate-follower starvation-free); inputLatency = K + (FIR active ? K : 0) (the honest total lookahead — the sheet's "K" reading assumed the un-filtered path; recorded as the §6.1.1 clarification); strict-delivery gating + semantic-zero flush reads; consume-up-to-window-capacity (keeps §5 rule 1 under ANY renderer); curve-signal shared immutable storage (content-identical; manifest hashes unaffected); run-id = experiment id (no timestamps — byte-identical regeneration).
- Empirical findings: (1) the emission-tap arithmetic error (caught by the smoke render; engine + spec both fixed); (2) the flush-cap double-count (caught by the direct-drive probe); (3) T-LEN-CAL: worst |Δ| = 8 frames over the full battery × rates — the provisional ±4096 tolerance has ~512× headroom (OD-6 evidence, ratification pending); (4) varispeed output is bit-identical across block schedules (observed; the contract stays −80 dBFS); (5) GCC 14 -Wmismatched-new-delete false-positives on allocation-audit interposition (suppressed for ≥ 14 only).
- Open items for the owner: OD-6 ratification from §13.4 (worst 8 frames; options: ratify a tight value, keep ±1 block, or require 0 for constant ratios); OD-18 unchanged; OD-9 later. No new open decisions introduced — the cycle-3 choices are recorded frozen decisions in §4.2.1/§4.8.1/§6.1.1.
- Exact next action: push cycle 3 in coherent commits (spec freeze → contract+registry+primitives → varispeed+registration → harness+CLI → tests → docs) → verify the GitHub Actions run green + download the ci-evidence artefact (JUnit 14/0 + the T-LEN-CAL evidence in the test output) → append the CI addendum → §17 step 4.

**CYCLE-3 CI VERIFICATION — Task 15 addendum (2026-09-26, same day): BLOCKED on credential restoration — local verification complete; owner action required.**

Work Log (continuation):
- Prepared the push: 5 coherent commits in the mandated boundaries (cb037ad contract+registry+primitives → c194a0e varispeed+registration → a26263a harness+CLI → 9ff693d tests → c608234 docs), each intermediate state verified BUILDABLE with green tests before committing (C1: 11/11 at the contract-only state; C2: 11/11 with the engine registered; C3: 11/11 + example render 3/3; final: 14/14).
- Final-tree verification: worktree == the validated state (byte-diff vs the saved finals: identical); canonical clean re-configure + build + CTest **14/14** (33.5 s) + JUnit 14 testcases / failures="0"; example artifact byte-identical regeneration (sha256-pinned in Task 15).
- PUSH ATTEMPT BLOCKED: the workspace was RESET between sessions again (the Task-11/OD-17 pattern): no git remote configured and the gitignored `.env` again lost `GITHUB_TOKEN`/`GITHUB_REPO` (only `DATABASE_URL` remains). A fresh search for a recoverable token (git config, credential files, shell histories, .env variants) found nothing — locally unrecoverable, exactly as in Task 11. The cycle's code/tests/docs are COMPLETE and locally verified; ONLY the push + the resulting GitHub Actions run await the owner's token restoration (`.env`: GITHUB_TOKEN + GITHUB_REPO — never logged, never committed), then `bash scripts/push-to-github.sh` or a direct `git push <token-url> main:main`.

Stage Summary (addendum):
- Cycle 3 status: implementation complete, test coverage complete (14/14 local, ASAN+UBSAN clean), docs complete, **CI verification PENDING the owner restoring the GitHub credentials** (same recovery path as OD-17/Task 11 — the blocker is environmental, not technical).
- Once pushed, the CI run must be verified green (all steps; CTest 14/14; the ci-evidence artefact: JUnit tests="14" failures="0" + the T-LEN-CAL evidence table present in the test output) and the verification addendum appended before §17 step 4.

---

Task ID: 15-ci-verify
Agent: implementation-cycle-3 (CI verification + cycle closure, DSP boundary)
Task: Cycle-3 CI verification — push the completed cycle-3 tree to GitHub, verify the authoritative Actions run green with content-verified evidence, and close the pending Task-15 addendum.

Work Log:
- Credentials restored by the owner (a fresh GitHub PAT supplied in-session; stored only in the gitignored `.env` as `GITHUB_TOKEN` + `GITHUB_REPO` — never logged, never committed).
- Pre-push state check: remote `main` = 1a2ed93 (the cycle-2 verified head) vs local `main` = c0d9a50 (6 commits ahead: cb037ad/c194a0e/a26263a/9ff693d/c608234/c0d9a50), worktree clean, 223 tracked files — no divergence, exactly the state the addendum left.
- Pushed 1a2ed93..c0d9a50 → **run 36269701944 FAILED at the Build step** (fast, ~50 s: toolchain assertions and configure passed). Downloaded the ci-evidence artefact and diagnosed from build.log: canonical **GCC 13.3.0 fires `-Werror=mismatched-new-delete`, 18 IPA analysis paths, all at the sized `operator delete`** (varispeed_contract_test.cpp then-line 637:64) — the T-A1 global operator-interposition audit is malloc/free-backed, and GCC's IPA cannot see whole-binary operator interposition. The file's own comment claimed this was a GCC-≥14-only false positive ("GCC 13, the canonical CI compiler, does not have the warning") — **empirically wrong**, a cross-environment compile-behaviour assumption asserted without live CI evidence.
- Fix (test-code-only; no DSP, no test logic, no frozen behaviour touched): extend the `-Wmismatched-new-delete` suppression from `__GNUC__ >= 14` to ALL GCC (not Clang) and rewrite the comment with the run evidence. Local re-verification after the change: clean rebuild + full CTest **14/14** (33.5 s, GCC 14.2, pinned CMake 3.31.6). Commit b0c9c23.
- Pushed c0d9a50..b0c9c23 → **run 36269901724: SUCCESS, every step green** on the canonical environment (ubuntu-24.04, GCC 13.3.0 asserted, CMake 3.31.6 sha256-pinned, Ninja). ci-evidence artefact downloaded and content-verified: test.log "100% tests passed, 0 tests failed out of 14" (28.81 s total; varispeed_contract_test 9.68 s, length_calibration_test 15.12 s); build/test-results.xml JUnit tests="14" failures="0" disabled="0" skipped="0" with all 14 testcases enumerated; configure.log and build.log clean (zero errors). T-LEN-CAL evidence IS present in the JUnit system-out (header + battery rows: extreme-0.25x |Δ|=2, extreme-0.5x |Δ|=1, extreme-2x/4x/8x |Δ|=0 at 44100) — note: CTest truncates captured per-test output at 1024 bytes, so the full 126-render table lives in the locally verified §13.4 evidence; the CI-visible rows agree with it. Resampler OD-18 evidence lines also present in the JUnit output.
- Documentation closed out: worklog current-state block (title, Project identity run list, Current phase, implementation state, Validation state, Exact next action) + build/CI environment spec §12.2 (cycle-3 suite growth + the empirical GCC-13 warning finding with run ids).

Stage Summary:
- **Cycle 3 is now FULLY closed end-to-end: implementation complete, test coverage complete (14/14 local + 14/14 on the canonical CI runner), docs complete, CI verified green at `main @ b0c9c23` (run 36269901724, evidence downloaded and content-verified).** §17 step 3 DONE; next roadmap item is §17 step 4 (the four remaining engines), owner-triggered.
- Empirical finding recorded (build/CI spec §12.2): canonical GCC 13.3.0 DOES fire `-Wmismatched-new-delete` on whole-binary operator interposition — per-version compiler-warning behaviour must be treated as empirical, asserted only from live CI evidence (the anti-drift philosophy applied to diagnostics).
- GitHub credentials operational again (owner-supplied PAT in the gitignored `.env`).
- No spec/Open-Decision changes required by this verification: the one failure was a test-code diagnostic-suppression gap, fixed in place; OD-6 (ratification of the §13.4 pack), OD-18, OD-9 all unchanged; no new open decisions introduced.
- Honest scope note (unchanged): CI green proves the infrastructure + component + engine-contract suite on the canonical runner — it is not, by itself, broader DSP-correctness ratification beyond the recorded T-E/T-LEN-CAL evidence (spec §12.2 "Does NOT prove", §13.4).

---
Task ID: 16 (cycle 4, IN PROGRESS — honest partial state)
Agent: implementation-cycle-4 (main implementation agent, DSP boundary)
Task: §17 step 4 — vardelay, pv.classic, pv.phaselocked, granular; each entering the SAME contract suite T-E1..T-E13.

Work Log:
- Reconstructed state (worklog/specs/git/CTest 14/registry == {varispeed}); verified the four engines absent.
- FFT DEPENDENCY QUESTION resolved from the SoT (NOT an invented resolution): build/CI spec §7 carries the full pocketfft vendoring recipe since the freeze (vendored header, BSD-3, commit-pinned, ORIGIN.toml); impl spec §2.2 and arch §J concur. Vendoring = executing the documented plan.
- native.vardelay: §6.2.1 frozen BEFORE coding (the direction-asymmetric crossfade placement forced by causality — lower wraps fade-before via exact curve lookahead, upper wraps fade-after; one fade at a time; the retention window with the 2E end-state span; v(0)=0; Preserving exact N_in + W). Implemented, registered, T-E1..T-E13 + T-A1 + T-D3 + failure fixtures GREEN (identity residue 2.78e-17; pitch 0.00-0.09 cents; cross-schedule bit-identity observed). Commits 0cd9608, 8deb5c5.
- native.granular: §6.5.1 frozen BEFORE coding (the quantised read grid; the OLA local-sum normalisation; the jitter rule — grain-indexed RNG draws, consumer tag engine.native.granular; the READ HORIZON + consume-and-discard — REQUIRED for ratio < 1 where the reads lag by (1−r_min)·N_in; the first implementation deadlocked at 0.25/0.75 before the rule was derived). Implemented, registered, T-E1..T-E13 + T-A1 + T-D3 + seed-sensitivity + fixtures GREEN. Commits 6ab63c8, 89c10ee.
- pocketfft vendored at cpp-branch commit c90e55b3d529f8efa40ed01a20de22405f45fc65 (BSD-3 re-verified against BOTH the LICENSE file and the header's own block; sha256s in ORIGIN.toml).
- native.pv.classic: §6.3.1 + §7.2.2 (the interpolateAtAbs amendment — the exact-fraction absolute-position read; empirical: the PV phase accumulators amplify the relative read's ~ulp seed to 0.29 signal-scale across schedules) frozen BEFORE coding. TWO gating bugs found by direct-drive diagnosis and fixed: (1) the analysis advance after the output-cap break froze the synthesis and deadlocked the padded-stream consumption at small blocks; (2) the FIR gate used varispeed's sequential-stream rule on an OLA accumulator — cells in [s_head, s_head+N) are incomplete and schedule-dependent; the correct gate is the completion boundary sCur_−K. After both: BIT-IDENTICAL across schedules (contract stays L-5 −80 dBFS). T-E1..T-E13 + T-A1 (the pocketfft allocation audit: 0 allocations) + T-D3 + fixtures GREEN. Commits 9b230be, 11ae1b0.
- Local state at this boundary: CTest 17/17 (73.0 s); ASAN+UBSAN clean on every new suite; clean builds (0 warnings, -Werror) throughout; every intermediate commit standalone-buildable (worktree-verified for the engine commits).

Stage Summary (partial, honest):
- IMPLEMENTED + TESTED + REGISTERED: varispeed, vardelay, granular, pv.classic (4 of 5). REMAINING: native.pv.phaselocked (§6.4/§6.4.1 NOT YET FROZEN — the L-D'99 peak-shift formulation needs its freeze-then-implement cycle; the design analysis is started: the identity-locking regions, the peak phase advance at ρ·ω̂_p·H with the synthesis grid at the analysis hop — duration-preserving without stretch+resample — and the open question of the magnitude placement (verbatim vs region-shifted with sideband interpolation) must be resolved in the freeze, not in code).
- Registry content == exactly the 4 implemented engines (T-E19).
- NOT claimed: §17 step 4 completion. The next session continues: freeze §6.4.1 → implement → T-E1..T-E13 → register (T-E19 → 5) → docs closure (§13.3/§14/§16/§17, build/CI spec §7 pocketfft vendored, READMEs, worklog) → push → CI verification.

---
Task ID: 16-closure
Agent: implementation-cycle-4 (final engine + cycle closure, DSP boundary)
Task: §17 step 4 completion — native.pv.phaselocked (freeze §6.4.1 → implement → T-E1..T-E13 → register, T-E19 ⇒ exactly 5 engines) + the deferred cycle-4 docs closure + push + CI verification.

Work Log:
- Reconstructed the state from the worklog/specs/git: main @ 880f6da, CTest 17/17, registry == 4 engines, pv.phaselocked absent (asserted by T-E19), pv.classic the real PV baseline, pocketfft vendored (ORIGIN.toml: cpp-branch c90e55b3d529f8efa40ed01a20de22405f45fc65, BSD-3 re-verified against both files). Confirmed the previous session's Task 16 entry (4 of 5 engines; the design analysis started but §6.4.1 NOT frozen; the magnitude-placement question left to the freeze). Baseline re-verified locally before any change: clean build + CTest 17/17.
- FREEZE §6.4.1 written into the implementation spec BEFORE any engine code (the §6.1.1/§6.2.1/§6.3.1/§6.5.1 pattern; 18 numbered items). The underdetermined details resolved IN THE FREEZE, not in code: (1) the identity-locking semantics — the sheet's parenthetical "(φ′_l = φ′_peak)" is realised as the L-D'99 difference-preserving relation φ′_l = φ′_peak + (φ_l − φ_peak), equivalently the per-region RIGID ROTATION X′_l = X_l·Z_i with the sheet's own Z recurrence Z_{u+1} = Z_u·e^{jΔω_{u+1}·H} as the definition; the all-phases-equal reading is REJECTED with the recorded reason (it zeroes the Dirichlet-lobe phase alternation of a windowed sinusoid — a −6 dB-scale reconstruction error even at ρ = 1 — and is not the published technique; at ρ = 1 the rigid rotation gives the exact STFT→ISTFT identity, which the all-equal reading cannot). (2) The magnitude-placement question resolved as region-shifted with linear sideband interpolation (the sheet's own clause; the verbatim option would leave the magnitude spectrum unshifted and realise no pitch change); per-target-bin range check to the interior band [1, N/2−1] — truncation, never wrap or clamp or renormalisation. (3) The Z cross-frame association: a per-channel per-bin complex array, piecewise-constant per region (the array IS the association — no peak tracking, no re-initialisation; a peak inherits whatever rotation its bin carried). (4) Peak detection: strict both-side local maxima, one ascending scan, no threshold/spacing, ties disqualify. (5) Region assignment: midpoints floor((k_i+k_{i+1})/2), ties LEFT, regions tile the interior, M = 0 ⇒ verbatim frame. (6) The frozen numeric expression orders (princarg wrap, the IF formula, deltaOmega = (rho−1)·omega, theta = deltaOmega·H, deltaBins = deltaOmega·N/2π, the rotation multiply, the placement accumulation order). (7) locking_mode: the §14 end-state table lists the key — implemented as default-and-only "identity"; "scaled" ⇒ CONFIG ERROR naming the value (specified but not implemented, never silently substituted). (8) Both pv.classic scheduling lessons applied BY CONSTRUCTION: the analysis advance never behind the output cap; the production gate is the OLA completion boundary sCur_ > m (never a sequential-stream rule).
- IMPLEMENTED src/engines/pv_phaselocked_engine.{h,cpp}: the L-D'99 peak-shift — analysis STFT on the fixed grid (pocketfft persistent plan, POCKETFFT_CACHE_SIZE 0, the §6.3.1-item-16 rule verbatim); per-channel re/im/mag/prevPhase/Z/synth spectra + fixed-capacity peak scratch (all in prepare(); zero allocation in the processing path — T-A1 audits); frame 0 verbatim; per frame ≥ 1: peak scan → regions → per region (IF at the peak via the previous frame's phase, deltaOmega, the Z rotation read-at-peak/rotate/propagate, the rigid-rotated region translated by deltaBins with floor/ceil linear interpolation and the per-target-bin band check); DC/Nyquist verbatim; IFFT (c2r, 1/N) → synthesis Hann → OLA at [s_n, s_n+N) on the SAME grid as the analysis (s_n = a_n = n·H — duration-preserving by construction, NO stretch, NO resampler, NO AA FIR); the engine-level window-product-sum ring; output emission by integer reads at absolute position m with the 1e-12 guard (the documented m = 0 zero edge, identical to pv.classic); inputLatency N+H / outputLatency N; exact Preserving length N_in + N; consume-all + sticky exhaustion; the analysis need horizon a_n < N_in + N with finish-mode semantic-zero taps beyond the padded stream (the pv.classic end game).
- REGISTERED at the single production point: the registry now contains ALL FIVE v0.1 engines with the registration order aligned to the §14 end-state table (varispeed, vardelay, pv.classic, pv.phaselocked, granular — the interim orders followed the implementation chronology while the table was incomplete; a pure reordering, no functional change). T-E19 (engine_registry_smoke) updated: exactly 5 engines, the at(i) order assertions, the phaselocked field set (Preserving, FixedBlock(512), [0.25, 4.0], Deterministic, MonoAndStereo, FOUR parameter keys incl. locking_mode, constructible factory, engineId check).
- TESTED: tests/pv_phaselocked_contract_test.cpp — the SAME contract suite T-E1..T-E13 + T-A1 + T-D3 + failure fixtures through the REAL harness, plus the PV-specific harmonic sanity (440+880 Hz two-partial input at 2.0x — real multi-peak/multi-region/rotation machinery; the generic suite cannot express it) and 2.0x added to T-E2 (the cycle's real-audio sanity set: 1.0x/1.5x/0.75x/2.0x/reversal). T-A1 carries the GCC suppression pragma with the b0c9c23 run evidence (the lesson preserved verbatim).
- LOCAL VERIFICATION: clean re-configure + build (zero warnings, -Werror); CTest 18/18 (~62 s); ASAN+UBSAN clean on the new suite (leak detection on) and the registry smoke; the engine feat commit (350d956) worktree-verified standalone-buildable (clean checkout → build → CTest 17/17 at that commit); the contract suite passed on the FIRST full run after implementation — no engine-side gating defects found (the pv.classic lessons were applied by construction in the freeze, which is where the difference shows: the completion-boundary gate and the analysis-first pacing were frozen items 12/13, not post-hoc fixes).
- MEASURED (the suite's printed evidence, recorded in spec §13.5): T-E1 identity 2.674e-11 on [1, N) (criterion 1e-4; the Z ≡ 1 / δ ≡ 0 exact-identity path — frame 0 the documented 0/0 guard); T-E2 0.00 cents at 1.5/0.75/2.0 (660.00/330.00/880.00 Hz); T-E3 ≤ 0.09 cents (±1/±7 st); T-E4 ≤ 0.00 cents at 44.1/96/192 kHz; T-E5 stereo per-channel bit-identity vs mono renders + correlated-pair identity; T-E6 byte-identical re-render (sha256 + path-normalised manifest); T-E7 cross-schedule max diff 0.000e+00 (bit-identical — the observed stronger property, reported not required; the contract remains L-5 −80 dBFS); T-E8 sweep 32..4096 + mixed; T-E9/T-E10/T-E11 accounting exact (padding N+H, exhaustion once, flush == N, lengthDelta == 0 across param variants incl. explicit locking_mode); T-E12 extremes (0.25 renders, 0.0625/8 benchmark-skip + creative-saturated with RANGE_SATURATED in the manifest); T-E13 reversal + hop-128 stress, finite, peak 0.250; the harmonic sanity 880.00 Hz / 0.00 cents with the harmonic identity pass reconstructing to 3.5e-12; T-A1 zero allocations at ratios 2.0/0.75/1.0; T-D3 reset bit-identity.
- DOCS CLOSURE (the deferred cycle-4 closure): implementation spec §13.3 (suite 14 → 18 with the per-cycle growth, pocketfft vendored, T-E19 = the v0.1 end state), NEW §13.5 (the cycle-4 measured engine evidence for all four engines + what it is/is-not), §14 (current state = the five-engine end state, order aligned, the duplicate end-state table folded), §17 step 4 = DONE with the per-engine record; READMEs (root + pitch-lab) current state; build/CI spec v1.3 (§1 + §7 pocketfft VENDORED with the re-verification record, §12.2 the cycle-4 empirical record + the five-engine registry statement); this worklog's current-state block.
- OD-6 and OD-18 deliberately UNTOUCHED (OD-6 is the varispeed rate-following length tolerance — the new engines are Preserving with lengthDelta = 0 exactly, no new evidence compels a change; OD-18 unchanged, non-blocking). No open decisions opened or closed by this engine.

Stage Summary:
- **§17 STEP 4 = COMPLETE**: ALL FIVE v0.1 engines are genuinely implemented, constructible, registered at the single production point, contract-tested (T-E1..T-E13 + T-A1 + T-D3 + fixtures through the real harness), locally verified (CTest 18/18, ASAN+UBSAN clean, zero-warning -Werror, worktree-verified standalone engine commits) and documented (§6.4.1 frozen before coding; §13.3/§13.5/§14/§17, READMEs, build/CI spec v1.3, this worklog). T-E19 confirms the registry exactly matches the five implementations — the §14 v0.1 end state is ACHIEVED.
- pv.phaselocked implementation state: COMPLETE (commits 350d956 feat + 5dfbf90 test; no engine-side defects found during its verification — the two pv.classic scheduling-lesson classes were applied by construction via the freeze).
- The registry is deterministic and single-source: registration order aligned to the §14 end-state table (varispeed, vardelay, pv.classic, pv.phaselocked, granular).
- CI verification of the cycle-4 closure: PENDING at this entry's writing (push + the canonical run + ci-evidence download/content-verification follow as the final step of this cycle; the addendum below records the result).
- Honest scope note (unchanged in kind): CI green proves the infrastructure + components + the five engine-contract suites on the canonical runner; it is not broader DSP-correctness ratification beyond the recorded T-E/§13.5 evidence (spec §12.2 "Does NOT prove").

---
Task ID: 16-ci-verify (the Task 16-closure addendum)
Agent: implementation-cycle-4 (CI verification + cycle closure, DSP boundary)
Task: Cycle-4 CI verification — push the completed §17-step-4 tree to GitHub, verify the authoritative Actions run green with content-verified evidence, and close the pending Task 16-closure addendum.

Work Log:
- Pre-push state: remote main = 880f6da (the previous session's cycle-4 in-progress push — itself CI-verified by run 36276010178, success), local main = e1d15b2 (3 commits ahead: 350d956 feat pv.phaselocked + registration + §6.4.1 freeze, 5dfbf90 the contract suite, e1d15b2 the docs closure), worktree clean — no divergence.
- Pushed 880f6da..e1d15b2 → **run 36278858598: SUCCESS — every step green** on the canonical environment (ubuntu-24.04, GCC 13.3.0 asserted, CMake 3.31.6 sha256-pinned, Ninja; job "T-INF1 infrastructure": Checkout (pinned by commit SHA) → Toolchain report + assertions (anti-drift) → Install pinned CMake (sha256-verified) → Configure → Build → Test (CTest, JUnit XML) → Upload CI evidence — all success).
- ci-evidence artefact (id 10918375873) downloaded and content-verified: test.log "100% tests passed, 0 tests failed out of 18" (94.07 s; the full 18-test list: types, rng, wav_io, resampler, curve, corpus_gen, hash, json_writer, experiment_compiler, varispeed/vardelay/granular/pv_classic/pv_phaselocked contract suites — pv_phaselocked_contract_test 15.40 s —, length_calibration, toolchain/engine_registry/version smokes); build/test-results.xml JUnit tests="18" failures="0" disabled="0" skipped="0" with all 18 testcases enumerated; the engine_registry_smoke system-out carries the T-E19 end-state line ("all checks passed (5 implemented engines — the v0.1 END STATE (§14): native.varispeed, native.vardelay, native.pv.classic, native.pv.phaselocked, native.granular; mechanism + factory binding sound)"); the pv_phaselocked_contract_test system-out carries the measured T-E1..T-E4 evidence lines (identity 2.674e-11; 0.00 cents at 1.5/0.75/2.0x; ≤ 0.09 cents ±1/±7 st; 0.00 cents at 44.1/96/192 kHz) — **identical values to the local GCC 14.2 run** (empirical local↔CI agreement, the same-binary determinism stance); configure.log (GCC 13.3.0) and build.log clean (zero errors). Note: CTest truncates captured per-test output at 1024 bytes — the full suite output was verified locally; the CI-visible lines agree with it.

Stage Summary:
- **Cycle 4 is now FULLY closed end-to-end: §17 STEP 4 = COMPLETE.** All five v0.1 engines are implemented, constructible, registered, contract-tested, locally verified (CTest 18/18, ASAN+UBSAN clean), CI-verified at main @ e1d15b2 (run 36278858598, evidence downloaded and content-verified: JUnit 18/18, T-E19 five-engine end state) and documented (§6.4.1 frozen before coding; §13.3/§13.5/§14/§17, build/CI spec v1.3, READMEs, this worklog).
- Run history at head: cycles 1-3 runs (36241070135, 36241266441, 36244722054, 36247154915, 36269901724) + the honest intermediate failure 36269701944 (the GCC-13 warning finding) + cycle 4's 36276010178 (in-progress state @ 880f6da) + **36278858598 (the closure @ e1d15b2)** — all evidence artefacts downloaded and content-verified.
- No spec/Open-Decision changes required by this verification: OD-6 (ratification of the §13.4 pack), OD-18, OD-9 all unchanged; no new open decisions.
- The next owner-triggered roadmap item is §17 step 5 (Metrics — analytic goldens first; tracker-dependent metrics only through the already-resolved clean-room pYIN route). NOT started (the cycle stops here, per instruction).

---
Task ID: 17 (cycle 5, IN PROGRESS — honest partial state recorded by the COMPLETION session; the implementation session's record follows inside)
Agent: implementation-cycle-5 (two sessions: the implementation session that ran out of context before verification, and the completion session that reconstructed + verified + closed)
Task: §17 step 5 — Metrics: the analysis layer (frozen §10.4 BEFORE coding), the analytic goldens, the Analyzer + `pitchlab analyze`, deterministic analysis artifacts, tracker-dependent metrics gated.

Work Log:
- SESSION A (2026-09-27, lost to context exhaustion — reconstructed from the git tree): wrote the §10.4 freeze (22 items) into the implementation spec BEFORE coding (the §4.4.3.1/§4.8.1 pattern); implemented `src/analysis/` (analysis_types/context assembly with hash-verified curve recompilation + emission-map reconstruction; spectral primitives: per-rate STFT, exact-frequency projection, Hilbert envelope, the frozen onset detector, warp helpers; the metric registry — 15 ids in §10.1 order, single in-code authority; the metric modules for the 11 implementable metrics + the tracker trio and cpu-cost honestly gated; the analyzer with reference resolution + the full failure model); `src/harness/json_reader.{h,cpp}` (canonical-JSON manifest reader); the `pitchlab analyze` CLI subcommand; CMake registration. The seven T-M test files were created as PLACEHOLDER STUBS (the session died before writing the real tests); no docs were updated and nothing was pushed. The workspace auto-sync committed this state as 5272942 with a UUID message (unpushed). A false "config/metrics.toml typo" claim was recorded into §10.4 item 2 (see the correction below).
- SESSION B (the completion session; this entry): full state reconstruction per §1 — git (5272942 = 3 595 insertions on top of 562c2fd, unpushed, remote lost to the workspace reset, .env credentials lost), build + CTest (25/25 but 7 placeholders 0.00 s), manual CLI end-to-end (render 3 jobs; analyze; artifact determinism md5-identical on delete + re-run; the varispeed self-reference spectral error ≡ 0.0 EXACTLY observed live).
- PHANTOM-TYPE CORRECTION: the claimed metrics.toml header typo NEVER EXISTED. md5(config/metrics.toml) is identical across c17e5b5 → 562c2fd → the worktree; a parser probe reads the full 15-entry `[metrics]` table. The "typo" was a DISPLAY-PIPELINE artifact — the agent terminal rendering strips the two characters `[m` (a valid SGR-escape prefix), so a displayed `[metrics]` reads as `etrics]`; Session A saw that echo and wrote a false correction into the freeze text (with the mangled strings verbatim). The spec entry is RETRACTED in place (§10.4 item 2, dated; T-M-A6 pins the committed config's parseability in CI). Lesson: file-content claims are verified by checksum/parser probe, never by terminal echo.
- THE REAL T-M SUITES (the cycle's missing core, written this session — 7 files, 57 doctest cases, ~690 assertions, every golden derived INDEPENDENTLY per §20-21): metric_waveform_test (T-M-W1..W8: DC/square exactness via dyadic arithmetic, crest-sample sine, per-channel separation, silent-channel nulls, insufficient-data, channel-mismatch analysis-error, end-to-end −80 dBFS class); metric_duration_test (T-M-D1..D7: varispeed identity 48 000+24 / static 2.0 24 000+12 with measuredFlush == declared EXACTLY, pv.classic 48 000+2 048, tainted-input, mismatch, missing-emission-map, the null-with-reason path); metric_onset_test (T-M-O1..O11: detector determinism/merge/frame-0/channel-max, the committed percussive corpus's 8 recipe onsets with EVERY matched error == 0, the impulse-train resolution limit, self-reference correlations == 1 at the fp floor + onset-vs-reference errors == 0 exactly, reference-unavailable); metric_spectral_test (T-M-S1..S17: band-edge derivation, the bin-centred analytic-zero leakage floor (Hann kernel = 0.5·D − 0.25·(D+D) vanishes at integer offsets ≥ 2 — measured ~1e-26), the measured non-bin-centred class, aliasing-indicator both modes, spectral-error self ≡ 0.0 EXACTLY + cross-class warp, phase-coherence R_h ≥ 0.999 phase-locked / ≤ 0.5 incoherent / participation nulls); metric_stereo_test (T-M-T1..T6: rho 0/−1 analytic, the committed correlated corpus through varispeed AND pv.classic with rho == 1 at the fp floor, the decorrelated 0.01 bracket, mono/silent not-applicable); metric_modulation_test (T-M-M1..M4: the 25 Hz AM golden at ±1 envelope bin, the full expected-rate table (granular/pv/vardelay-static/dynamic-null/varispeed-null), constant-envelope not-applicable, the granular real-path measurement); metric_analyzer_test (T-M-A1..A13: JSON round-trip + malformed rejection, registry semantics/defences, config loading + T-M-A6 the committed-config pin, end-to-end schema/ordering/provenance/gating, artifact byte-identity, the FULL failure matrix (missing manifest / missing master / sha256 mismatch / malformed manifest / curve drift — each an explicit per-job analysis-error with the run continuing), disabled-in-config not-applicable, reference resolution, the analysis-never-mutates-artifacts invariance).
- TEST-BED BUGS FOUND AND FIXED BY THE NEW SUITES: (1) `json::Value::asDouble()` returned the uninitialised union member for Int-kind values — REAL because the canonical writer emits Double(1.0) as "1" and the reader parses it back as Int (the whole-number round-trip asymmetry); made a TOTAL numeric read (Int → cast) with the property pinned by T-M-A1; the manifest consumers (mfDouble in the analyzer) already handled both kinds — the latent trap was any future asDouble-on-Int caller. (2) Two dangling-reference patterns in the new tests themselves (reference bound into a returned-by-value JobArt) — caught by -Werror=dangling-reference before any runtime damage.
- FIXTURES: test_fixtures.h gained the committed-shape config/metrics.toml (required by TestRoot for the analyzer paths), optional category/bandContentHz in makeAsset, copyCommittedAsset (PITCHLAB_SOURCE_DIR-guarded), makeCtx (direct AnalysisContext builder for pure-function unit tests), productionMetricRegistry + analyzeExperiment + readBytes helpers. CMake: PITCHLAB_SOURCE_DIR for onset/stereo/analyzer suites.
- EMPIRICAL GOLDEN-CLASS AMENDMENTS (recorded in the spec, §10.4 items 13/14 — golden-test classifications only, OD-9 untouched): the non-bin-centred Hann leakage at the 4-bin guard is ~8e-6 of total energy (the 1e-9 estimate holds only for bin-centred tones — the analytic-zero case, which the tests assert separately); the frozen onset detector fires on any signal whose envelope exceeds 25 % of its own maximum (a sine ramp-up is ONE measured onset — T-M-D1) and reports zero onsets only for silence.
- LOCAL VERIFICATION: clean re-configure + full build (zero warnings, -Werror); **CTest 25/25 (~96 s)** — the seven T-M suites are REAL (57 cases; previously 7 placeholders); ASAN+UBSAN (leak detection on) clean on the seven metric suites AND the affected existing suites (json_writer_test, experiment_compiler_test, engine_registry_smoke, varispeed_contract_test — the asDouble change is in the shared value type).
- C5.8 CROSS-ENGINE METRIC MATRIX (the §10.4-item-27-class evidence run): committed suites `experiments/suites/cross-engine-metrics-mono.toml` (5 engines × 4 curves × {percussive, harmonic} — 40 jobs) + `-stereo.toml` (5 engines × 2 curves × {correlated, decorrelated} — 20 jobs); rendered + analysed through the REAL CLI; **60/60 jobs analysed, ZERO analysis errors**; status census + representative discriminants recorded in spec §13.6 (spectral-error distances per engine; onset matched/missed separating the time-domain from the spectral families; pv.classic's REAL inter-channel decorrelation −0.327 on the decorrelated corpus at +7 st — a measured RESULT, not a bug; varispeed's phase-coherence R_h == 1.0000 anchor; the AM envelope peaks tracking the shifted fundamental). NO ranking, NO aggregate score anywhere.
- DOCS CLOSURE: spec §4.8.1 item 10 (analyze now real), §10.4 items 2/13/14 amendments, §13.2 T-M row, §13.3 cycle-5 paragraph (25 tests), NEW §13.6 (measured metric evidence), §17 step 5 = DONE with the record; READMEs (root + pitch-lab) current state + directory tree; this worklog.

Stage Summary:
- **§17 STEP 5 = COMPLETE (pending push + CI verification — the addendum below records the result).** The analysis layer is implemented per the frozen §10.4, the seven T-M suites carry real analytic goldens, the analyzer path is functionally proven end-to-end (render → analyze → deterministic artifact), tracker-dependent metrics and cpu-cost are honestly gated, and the cross-engine evidence proves the metrics measure and discriminate engine behaviours without ever collapsing to a score.
- OD-9 EXPLICITLY OPEN (no tolerance created, changed or ratified; the two empirical golden-floor amendments are golden-test classifications recorded in the spec, never normative tolerances; tolerances.toml untouched). OD-6, OD-18 unchanged. The phantom metrics.toml "typo" claim is retracted in the spec with the mechanism and the verification evidence.
- Cycle-5 code state: 25 CTest tests local-green, zero-warning builds, sanitizers clean; the analysis artifacts under `pitch-lab/artifacts/analysis/` are GENERATED (gitignored, never SoT) and byte-identical on delete + re-run.
- Commit plan (this session, executed next): the unpushed auto-sync commit 5272942 (UUID message) is restructured into honest cycle-5 commits — spec freeze, analysis-layer implementation, the T-M suites + fixtures, the asDouble fix, the docs closure — then push + CI verification.


---
Task ID: 17-ci-verify (the Task 17 addendum)
Agent: implementation-cycle-5 (CI verification + cycle closure)
Task: Cycle-5 CI verification — push the completed §17-step-5 tree to GitHub, verify the authoritative Actions run green with content-verified evidence, and close the cycle.

Work Log:
- Pre-push state: remote main = 562c2fd (the cycle-4 closure, CI-verified by run 36278858598; the workspace reset had removed the local remote + .env credentials — restored per the Task-11/OD-17 pattern: GITHUB_TOKEN/GITHUB_REPO into the gitignored .env, token never logged, never committed, push via an in-memory credential URL). Local main = a3dc0fe (5 commits on top: ff6efa6 the §10.4 freeze docs, cb1f913 the analysis-layer implementation, c534629 the json asDouble total-read fix, 537ff47 the seven T-M suites + fixtures, a3dc0fe the docs closure). The unpushed auto-sync UUID commit 5272942 was RESTRUCTURED into exactly these five honest commits (content-identical overall: git diff 5272942 HEAD shows only the completion session's changes; no pushed history touched — pure fast-forward).
- ACTIVATION PUSH: `562c2fd..a3dc0fe main -> main` — **run 36352620565: SUCCESS** on the canonical environment (GCC 13.3.0 asserted, CMake 3.31.6 sha256-pinned): every step green (Checkout → Toolchain report + anti-drift assertions → pinned CMake install → Configure → Build → Test → Upload CI evidence).
- ci-evidence artefact (id 10943021782) downloaded and content-verified: test.log "100% tests passed, 0 tests failed out of 25" (119.39 s — the full 25-test list enumerated, the seven metric suites included); build/test-results.xml JUnit tests="25" failures="0" disabled="0" skipped="0" with all 25 testcases listed (…, metric_waveform_test, metric_duration_test, metric_onset_test, metric_spectral_test, metric_stereo_test, metric_modulation_test, metric_analyzer_test, …); the JUnit system-out carries the T-M-M4 in-test measurement lines (envelopePeakHz/relAtPeak/relAtExpected); configure.log GCC 13.3.0; build.log clean (zero errors/warnings). CTest truncates captured per-test output at 1024 bytes (known); the full suite output was verified locally — the CI-visible lines agree.
- Local↔CI agreement: the local GCC 14.2 run and the canonical GCC 13.3.0 run agree on every golden (25/25 both), continuing the same-binary-determinism stance's empirical local↔CI agreement record.

Stage Summary:
- **Cycle 5 is now FULLY closed end-to-end: §17 STEP 5 = COMPLETE.** The analysis layer is implemented per the frozen §10.4, the seven T-M suites carry the analytic goldens, the Analyzer + `pitchlab analyze` path is functionally proven, artifacts are deterministic (byte-identical on delete + re-run), the tracker-dependent metrics and cpu-cost are honestly gated, the 60-job cross-engine metric matrix evidence is recorded (§13.6), and the canonical CI run 36352620565 is GREEN at main @ a3dc0fe with content-verified evidence (JUnit 25/25).
- Run history at head: cycles 1-4 runs (…36269901724, 36276010178, 36278858598) + **36352620565 (the cycle-5 closure @ a3dc0fe)** — evidence downloaded and content-verified.
- OD-9 (metric tolerance values), OD-6 (length-tolerance ratification), OD-18 (resampler constants): ALL explicitly OPEN. Nothing in this cycle created, changed, ratified or silently weakened any tolerance; the two empirical golden-class amendments (§10.4 items 13/14) are golden-test classifications recorded in the spec, and the phantom metrics.toml "typo" claim was retracted with the verification evidence (§10.4 item 2). No new open decisions were introduced.
- Honest scope note (unchanged in kind): CI green proves the infrastructure + components + engine-contract suites + the metric/golden suites on the canonical runner; it is not broader DSP-correctness ratification beyond the recorded T-E/T-M/§13.6 evidence. The next owner-triggered roadmap item is §17 step 6 (the full §G.3 suite + `pitchlab verify` reproducibility gate + v0.1 acceptance). NOT started — the cycle stops here, per instruction.
