# OCTAVOX — ORIGINAL DSP CHAIN (complete, R26 revision)
**Eventide Octavox VST3 — the original's complete static DSP chain.**
Revision: R26 (2026-11-23). Supersedes the R25 revision (lost with the R25 archive — see
`OCTAVOX_R26_COMPLETE_STATIC_AUDIT.md` §1; this document is rebuilt from the binary + the
committed r22 corpus, with every R26 re-verification marked **[R26]**).

Target: `upload/Ox.vst3` — SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
(PE32+ x64, 7 sections). **Never executed; static reconstruction only.** Class grammar:
OBS = observed in code/bytes; DER = derived; UNK = not established.

## 1. Host → patch selection
- (inCh,outCh) (1,1)→mimo 395 modules (ctor FUN_1800F00A0), (1,2)→miso 394 (FUN_180109D40),
  (2,2)→siso 394 (FUN_180122E00); runtime selector FUN_18030C6A0 (dispatched via the stored
  pointer @.rdata 0x18040A2F8 **[R26]**). `inch`/`ouch` (1..2 each) steer the mono/stereo fold
  inside the chosen patch; they do not select it.
- Host: JuceVST3Component → VoxAudioProcessor ("Octavox"/"Ocxv"); 73-record parameter surface
  + JUCE 30-family table; no MIDI/tempo/snapshot modules in the DSP records (negatives);
  latency: structural only (§8). **[R26]** LatencySamples = a DebugReport diagnostic attribute,
  not a host report.

## 2. Input stage
- igan (−96..+12 dB) gains the input(s) (left in [247]/right in [250] records:
  `in=adc-inN; gain=igan-out` **[R26]**).
- Live Mode: dry path via delay comp 1/2 (maxdelay=51, delayamt=40 — the `live mode delay comp`
  constant module; iswitch6/7 select live?comp:direct) **[R26]**; engine delayamtN += c_switch
  (=40 when live) via c_adder8..15.
- softclip [273] (select=1, drive=0, order=1) conditions the engine input.

## 3. Pitch engine (dtonicii9 [275], wrapper+0x430)
Class modpp::modules::dtonicii_t::dsp_t; vtable 0x180409F60 (m01 0x1800E10B0 / m02 0x1800E1520);
tracker at engine+0x4518. Record: maxdelay=2400; nvoices=8; steps=12; hysteresis=10;
correctrate=1; mincorrect=0; intervalglide=1; tuning=0; quantization=0; gatelevel=−55;
xfadetime=xfad; mod1..8=rdm slew 1..8; shift1..8=c_multiply 1..8; pcent1..8=sht1..8;
modamt1..8=rdep; delayamt1..8=c_adder8..15; userscale1..12=0 (unwired) **[R26: full 55-entry
record re-extracted]**.

### 3.1 Tracker (one instance; consumes p[1], p[3] of every 4-sample block)
64-float raw ring → symmetric FIR (taps 5/6 | 10/11 | 20/21 by SR class <50k/50–100k/≥100k;
coefficient blocks at engine+0xA3C8.. **[R26: m01 id 5]**) → adaptive gate (peak decay 0.99219,
multiplier T+0x5F28 = m01 id 4 target **[R26]**) → 512-float detection ring @ ≈12 kHz cadence
{2,4,8} → 10-state FSM: arm(81) → 243-lag correlation (48-tap window; v·243 / 1/v from m01 id 6
**[R26]**) → peak + parabolic refine (FUN_18013E160: f = ((p0−p2)·0.5)/(p0−2p1+p2) clamped ±1 ×8;
2^11-scale when >1) → verify → accept/reject (tolerance 3/32 → 1/32 below ~228 Hz @48k;
Minimum Pitch = m01 id 1 pair (v, v+2) **[R26]**; 48 choices C0..B3, default 24) → confidence
(0.203125, 210-test) → period scaling (×2/×4 while < 102400) → state 7 (ω = 2π·2048/T̂
**[R26: reconfirmed]**, stability counter ≤ 0x7FFFFF, hard reset of the running gain on
|Δω| ≥ 4e-4, hysteresis 1.6e-4/4e-4, correction-ratio walk) → state 8 (record writer) → state 9
AGC (0.99/0.7 attack, gain (1/128)/sqrt(level) ≤ 32).

### 3.2 Record ring (engine+0x98, 4096 floats, mask 0xFFF) — **[R26: both sides disassembled]**
Writer (FUN_1800E0190), gated by scale-mask bit (i4+1) of int[12]@T+0x6038, writes descending:
(W−1)=correction (new running gain), (W−2)=i4=int(port-26 matched-row field5), (W−3)=i5=int(matched-row
field4)=**interval**, (W−4)=period (raw bits of T+0x5FD4).
Consumer (prepare 0x1800E25DB–0x1800E269F), idx=(recidx[v]+wdelta)&0xFFF:
ring[idx+0]=period (×2⁻¹¹, min 48, fallback 480 if silent); ring[idx+1]=interval → page[2+i];
ring[idx+2]=**dead field** (never read); ring[idx+3]=correction (rate-slot = page[0]·corr; 1.0 when
tracking off).

### 3.3 Harmony path (update publisher FUN_1800E5160, per block)
keys(17 choices)→keymap 0x1803BB610 (A=9) → keyRatio = exp(±temperament[|k−9|]·ln2/1200) (symmetric
law); scal(16)→ scal-table mux (irregular authored shifts; "hungarian" exceeds the table) →
19-row pattern table 0x1803BB780 → grid fill → 8-row walk (labels inverted: "+" walks DOWN);
temperaments 5×12 @0x1803BB690 (Equal/Just_Major/Just_Minor/Pythagorean/Meantone **[R26: byte-exact
re-dump]**); interval mux v = 14+s+δ (δ=1 iff s mod 7 ∈ {2,6}) over 29 choices; ratio_v =
keyRatio·exp((voiceCents+shtN+tune)·ln2/1200); pages hold ratio/16.
userscale map 0x1803BB60C = keymap rotated right by one **[R26 new]**; record userscale1..12 = 0.

### 3.4 Eight voices
Per voice: work ring (4096/8192/16384 floats by SR class) written −(g_v·input[D_v+k']);
read position advanced by **step = 4·P(f)·ratio·correction** (f = F·page[10];
F = rdm-slew walk element [0]; P = quartic c0..c4 @ engine+0x40DC..E8 **[R26: Horner chain
re-disassembled]**; ratio = page[0]→page[1] glide; correction = ring[idx+3]).
Interpolation: 256×6 kernel @0x1803B6170, phase floor(frac·256)&255; settled = single
interpolated tap; crossfade state +0x4158 (init 0, settles −1), re-arm compare
`pos−delay < 8.000000000000007`; crossfade length = 1/(page[12+idx]·2⁻²³) output samples
(idx = clamp(int((v−48)/16), 0, 31)); engine crossfade = xfad (0..100 ms, default 10).
**[R26 kernel characterisation]**: bit-exact palindrome (K[r][j]=K[255−r][5−j]; 608 distinct
values); DC gain 1+ripple ≤ 1.22e-4; Nyquist gain 0.0039 (half-phase) → 0.641 (integer phase);
Farrow / LS-fractional-delay / windowed-sinc / Gaussian generators all numerically eliminated;
**generator UNK** (offline-designed table). Third table consumer found: accessor thunk
0x18032B720 **[R26]**.
Output-stage twin readers ("just for the filter"/2, maxdelay=1) use int(frac·255).

### 3.5 Random modulation (randomize)
rdm noise [212] (empty record): lagged-Fibonacci additive RNG (seeds 0xD00/0x1100, mask
0xFFFFFF00, shift 8, scale 0x34000001 = 2⁻²³·(1+2⁻²³)); period 2²²; |noise| → S&H (rdm sh N) →
slew (rdm slew N) → comp re-arm (rdm comp N); F = rdm slew N[0]; rdep (0..200, def 50) →
modamt1..8 → page[10] = clamp(modamt/2400, ±1); modulation one-sided upward, cents = walk·depth
∈ [0, +200¢] ⇒ ratio multiplier 4·P(f) ∈ [1, +12.24%].

## 4. Per-voice output chains (8×)
dtonicii9-outN → modfilterN (20 Hz HP, q=1) → eqnN (1-band cut EQ; boost = c_boundN =
clamp(interval·CutMultiplier, MaxCut, 0) dB; freq = CutFreq by instrument table) → addN
(echo sum node) ← multiplyN = moddelayN·10^(fbkN/20) (fbkN −100..0 dB; mute ⇒ 10⁻⁵);
moddelayN (maxdelay 2000, declick=1, delayamt = raw dlyN ms);
anti-alias modiirq [271] on the engine input side (2-section IIR, q=0.7 LP, cutoff =
2^(−max_downshift/7) degrees, instrument-table switched).

## 5. Mixer + global feedback loop
stereomixer3 [338]: composite (clip sub = module id 0x152 int16-range pair; mix sub
ninputs=8, tc=25) — 8 voice inputs (addN), per-voice gain m switch N (ognN or −100/mute),
per-voice pan panN (−1..+1). **[R26] PAN LAW: trigonometric/equal-power family** — the mix
family computes per-input gains via 1/√2 (0.70710677 @0x1804120A4 → magic-static 0x1804CCD9C),
0.5/g, double sqrt, atan2f (0x180180200) ×2 and a trig call (0x180350400) with a
1/(ninputs/2) normalisation (FUN_18013DD34..0x18013DEAB). **The linear 0.5·(1±p)·g law is
refuted.** Mix-sub vtable = 0x180409E20 (dtor + base thunks) **[R26]**.
Global wet loop (post-pan, per channel): stereomixer3-left/right → feedback/feedback1
(amp1=1, amp2=mfbk 0..100%, tc=20) → softclip/2 → delay_xf/1 (maxdelay 2400 ms, xfadetime
20 ms fixed, tapdls1 = max(fbkd, 50) ms via c_minmax) → fractional-delay readers
("just for the filter"/2) → highcut/1 (5 Hz LP) → subtract/1 (= reader − highcut; DC blocker
H(z) ≈ (1−z⁻¹)/(1−az⁻¹)) → back into feedback*.in2.
Wet/dry: mmix 0..100% (def 100); wet = iswitch11 (select = ouch−1): monoSumWetChans
(0.707·(feedback+feedback1), tc=20) when ouch=1, else per-channel feedback; dry =
live-compensated input × (1−mmix) (c_fltr_a/c_subtract2, tc=10); LWetDryMix/RWetDryMix →
left/right out (ogan −96..+24 dB) → bypa (normal/bypass hard switch) → meters/LEDs.

## 6. Latency & structure (static)
Structural voice-path latency ≈ 288 samples (int_delay 280 + work-ring write depth 3 +
interpolation group delay ≤5) ≈ 6.5 ms @44.1 kHz; tracker control latency ≥ 81 cadence ticks
(≈7.3 ms) + ~1.5 detection-ring periods. **Host-reported latency: UNK** (no host-facing
setLatencySamples binding found; the LatencySamples string belongs to a DebugReport
`<PluginEnvironment>` diagnostic — **[R26 correction]**).

## 7. Original vs PitchLab (disambiguation)
This document describes the ORIGINAL Octavox only. Our PitchLab reimplementation's own choices
(38-parameter wrapper, 2304-sample reported latency, 256+32·v delays, √2-cos/sin pan,
OBSKERNEL6/LAGRANGE6 variants) are documented separately in the PITCHLAB_ROUND* findings and
are NOT claims about the original.

## 8. Version history
- R25 revision: LOST (archive + branch absent from the sandbox; see R26 audit §1). Its SHA-256
  can no longer be verified; do not cite R25 as evidence.
- R26 revision (this file): rebuilt + re-verified; every **[R26]** mark = re-derived this round
  from the raw binary (see `octavox-work/r26/audit/`).
