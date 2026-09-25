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
