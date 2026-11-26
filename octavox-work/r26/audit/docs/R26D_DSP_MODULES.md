# R26 Appendix D — DSP MODULES (post-processing catalogue)

Sources (only these): `notes/CLAIM_LEDGER_INPUT.md` (C-xxx + §7 post-processing, §2 module
inventory, §5 random, §6 delay/feedback), `notes/R26_FRESH_FINDINGS.md` (F2.4, F7, F9, F10),
main audit §7. Static-only discipline: the target (`Ox.vst3`) was **never executed**; no
runtime claim exists in the corpus. Every row carries (classification, id, VA when the
sources give one); classifications copied exactly — no UNK upgraded. mimo indices
throughout; miso/siso: "indices ≥357 shift by 1" (F2.5); add_fn `—` = not carried by the
sources. Engine-side modules (dtonicii9 internals) are Appendix B; control plumbing is
Appendix C; signal flow is Appendix A.

## D.1 Audio-path post modules (input conditioning → engine → output)

| module (mimo idx) | add_fn / VA | record (verbatim key fields) | equation / topology | class |
|---|---|---|---|---|
| igan [no idx] | — | −96..+12 dB, default 0 (F8.7) | input gain before left/right in; dB converted internally (§7.13) | OBS |
| left in [247] / right in [250] | — | — (top-level input modules, C-034) | feed the patch input channels (host-bus wiring UNK, Appendix A.3) | OBS |
| mix [253] | — | (L+R)/2, tc=20 (C-034) | mono-fold level-slewed summer | OBS |
| iswitch4 [254] | — | inch fold: c_subtract1 [no idx] = inch−1 stereo flag (C-034) | mono-fold vs stereo select at record level | OBS |
| live mode delay comp [no idx] | add 0x1800CB200 | value=40 (raw constant module; F9.1) | constant source for the live-mode compensation | OBS |
| delay comp 1 [248] / 2 [251] | add_fn 0x180138D30 (sub-ctors 0x1800D2B10/0x1800D2840; F9.2) | maxdelay=51; delayamt=live mode delay comp-out (the raw 40) (F9.1) | dry-path live-mode delay; different module class from moddelay (0x180137930) and delay_xf (0x180137630); unit of the 40: DER (samples, medium confidence), formally UNK at the conversion site (F9.3) | OBS / DER / UNK |
| iswitch6 [249] / iswitch7 [252] | — | select=live-out; in1=delay comp 1/2-out; in2=left/right-in-out (F9.1) | dry path = live ? delayed(40) : direct | OBS |
| modiirq [271] | — | n_sections=2; qfactor=0.7; type=0 (LP); modfreq = bound1-out (C-038) | anti-alias 2-section IIR; cutoff law = D.3; switched in/out by the AntiAliasOnOff instrument table via iswitch8 [272] — not a user switch | OBS / DER(units) |
| softclip [273] / softclip2 [340] | — | select=1, drive=0, order=1 (C-034 / §7.2) | input conditioning / feedback-bus saturating by construction; transfer curve UNK (ledger §9.1, R22S) | OBS / UNK(curve) |
| modfilter8..15 ([276][284][292][300][308][316][324][332]) | — | freq=20; q=1; output "-high"; input = dtonicii9-outN (C-035/§7.3) | per-voice 20 Hz high-pass feeding the EQ | OBS |
| eqn..eqn7 ([277][285][293][301][309][317][325][333]) | — | neqs=1; freq1=CutFreq; qfactor1=0.7; boost1=c_boundN; type1=2 (C-035/§7.4) | 1-band cut EQ: c_boundN = clamp(c_multiply 8N, MaxCut, 0) dB; c_multiply 8N = c_multiply N × CutMultiplier; boost driver = the voice's interval, not shtN | OBS |
| moddelay7..moddelay ([279][287][295][303][311][319][327][335]) | add 0x180137930 (F9.2) | maxdelay=2000; declick=1; delayamt = raw dlyN (C-035/§6.8) | per-voice echo (ms domain); delay publisher FUN_1800D4710 (vtable +0x58), 16 call sites = 8 voices × 2 trees (F2.3) | OBS |
| multiply4..multiply11 ([270][283][291][299][307][315][323][331]) | — | in1=moddelayN-out; in2=fb-gain smoother-out (C-035) | out = moddelayN × 10^(fbkN/20) (smoothed); feeds addN (per-voice feedback loop) | OBS |
| c_fltr_a12/a23/a25/a27/a29/a31/a33/a35 | add 0x18013AAC0 | in = c_power9..16-out; tc=20 (C-037/§7.12) | slew smoother of 10^(fbkN/20) (per-voice fb gain) | OBS |
| stereomixer3 [338] | add_fn 0x180137720 (F7.1) | ninputs=8; tc=25; inN=addN-out; gainN=m switch N-out (ognN or −100); panN=panN-out; processoff=0; pan1..pan8 (§7.6) | composite: clip sub FUN_1800D8230 @ wrapper+0x260 (module id 0x152; int16-range pair 0xC7000000/0x46FFFE00, 1.0f, −1 qwords) + mix sub FUN_1800D7400 @ wrapper+0xF0; mix sub vtable 0x180409E20 = {deleting dtor 0x1800D9E80 + dsp_shared base thunks 0x1800C5730/40/50/70/90/B0} — no audio-process override in slots 1–6; pan = trigonometric/equal-power family (D.5, F7.3); constructed 3× (0x1800F8834/0x1801124D4/0x18012B594, one per profile) | OBS |
| feedback [345] / feedback1 [339] | — | in1=stereomixer3-left/right; in2=subtract/subtract1-out; amp1=1; amp2=mfbk; tc=20 (F10.3/§7.7) | level-slewed summer out = slew(a·in1 + b·in2), time_constant=20; loop node of the global wet loop (D.4) | OBS |
| delay_xf [347] / delay_xf1 [341] | add 0x180137630 (F9.2) | maxdelay=2400; xfadetime=20; tapdls1=c_minmax-maximum; in = softclip/softclip2-out (F10.3) | crossfaded variable delay; tap = max(fbkd,50) (ms), bounded by maxdelay 2400; 20 ms crossfade on tap changes; c_minmax unit semantics UNK (R22S) | OBS / UNK(unit) |
| "just for the filter" [348] / "just for the filter2" [342] | add 0x1801374D0 (thunks 0x1801374A0/0x1801374D0 → shared ctor 0x1800DC7A0 → binder FUN_1800DBEF0 → obj+0xD0; process FUN_1800DC430..0x1800DC761; C-068) | maxdelay=1; delayamt=0 (F10.3) | fractional-delay readers = second consumer of the 256×6 kernel @0x1803B6170; phase = int(frac·255) — row 255 unreachable; instantiated twice in the stereo output chain (L chain+0x1D2B8, R chain+0x1E240, between delay_xf* and highcut*, feeding LWetDryMix/RWetDryMix); mode flag [obj+0x8C] = record's `modsw` field — absent from the shipped patch ⇒ mode 0 only (internal phase [obj+0x90] gliding toward table[0], K=0.25 @0x180412260) | OBS |
| highcut [349] / highcut1 [343] | — | freq=5 (F10.3/§7.9) | one-pole LP at 5 Hz | OBS |
| subtract [350] / subtract1 [344] | — | in1=filter-out; in2=highcut-out (F10.3) | in1 − in2 ⇒ 5 Hz DC blocker H(z) ≈ (1−z⁻¹)/(1−az⁻¹) | OBS / DER(transfer) |
| monoSumWetChans [357] | add 0x180138710; slot 0x1E990 | amp1=amp2=0.707; in1=feedback-out; in2=feedback1-out (§2/F10.4) | 0.707·(feedback + feedback1) equal-power mono fold | OBS |
| iswitch9 [360] (mimo only) / iswitch11 [362] | — | select=c_subtract3 (=ouch−1); in1=monoSumWetChans-out; in2=feedback1/feedback-out (C-042/F10.4) | ouch=1 → mono fold; ouch=2 → per-channel stereo | OBS |
| mmix [207] | — | 0..100 %, default 100 (F8.7) | wet/dry mix: wet → c_fltr_a1/a2/a3 (tc 10); dry → c_fltr_a + c_subtract2 = 1−mmix (tc 10) (F10.4) | OBS |
| multiply [356] / multiply1 [363] / multiply2 [361] / multiply3 [359] | — | output-stage multiplies in the wet/dry region (ledger §2 index list) | the mmix wet/dry products of the Lout/Rout law slew10(dry·(1−mmix)) + slew10(wet·mmix) (§7.11); exact operand wiring per module not itemised in the sources | OBS |
| LWetDryMix [364] / RWetDryMix [365] | — | dry+wet records (C-042/F10.4) | wet = iswitch9/11-out × smoothed mmix; dry = iswitch6/7-out (live-compensated) × smoothed 1−mmix | OBS |
| ogan [210] | — | −96..+24 dB, default 0 (F8.7) | output gain, dB converted internally (§7.13) | OBS |
| left out [366] / right out [367] | — | — (output modules, §2) | terminal output stages after ogan | OBS |
| bypa [211] | — | normal/bypass (F8.7) | hard output-domain switch (iswitch [369] / iswitch1 [370]); sample-accurate, no ramp declared (§7.11); iswitch2 [368] role not described; C-033 lists the top-level outputs iswitch/iswitch1 among the dead (sink) nets — both statements carried verbatim | OBS |
| meters [371]..[395] | — | peak1/peak2 on outputs → LevMetrAtoC → LevLintodB → lopD/ropD (ganred:); input peaks peak4/peak5 → lipD/ripD; overload LEDs thresh −0.1 → lopL/ropL/lipL/ripL (monitor:); pitM/tonM display dtonicii9 pitch/tonality (C-043) | meter/LED sinks (terminal; C-033) | OBS |

## D.2 Control-side modules feeding the post chain

| module (mimo idx) | add_fn | record (verbatim key fields) | equation / topology | class |
|---|---|---|---|---|
| c_switch [no idx] | — | select=live-out; in1=live mode delay comp-out; in2=0 (F9.1) | output = live ? 40 : 0 | OBS |
| c_minmax [no idx] | — | in1=fbkd; in2=50 (F10.3/C-041) | maximum = max(fbkd,50) → delay_xf.tapdls1; unit of the 50 floor = ms per the fbkd statement (DER orientation; R22S: exact unit semantics UNK from records alone) | OBS / DER / UNK |
| c_minmax2 [263] | — | in = c_multiply 1..8 (8 voices) (C-038) | max of the 8 per-voice degree shifts → c_divide2 (÷7) — the anti-alias law input (D.3) | OBS |
| c_adder8..15 ([30][44]…) | add FUN_1800C7460 | in1=dlyN-out; in2=c_switch-out (F9.1) | engine delayamtN = dlyN + (live ? 40 : 0); clamp ±2^31 (C-020) | OBS |
| c_multiply 1..8 ([35][49]…) | add FUN_18013A250 | in = int table switch N (C-038) | = −(int table switch N − 14) → shiftN (engine record shift1..8 = c_multiply 1..8-out; F2.4); clamp [−32768, 32767] (C-020) | OBS |
| sht1..8 | — | sht1: −2400..+2400, res 1; sht2..8: −1..+1, res 0.001; default 0 (F8.6/F8.7) | pcent = cents: clamped value added directly to the cents-domain argument before exp((…+pcent)·0.00057762265 = ln2/1200); one unit for all 8 ⇒ sht1 = ±2400 cents (±2 oct), sht2..8 = ±1 cent; range CONFLICT is authoring, not DSP (Q-10/C-056) | OBS / CONFLICT(authoring) |
| dly1..8 | — | "delay" knob 0..1250 ms, default 0 (§6.1/F8.7) | two consumers: moddelay(8−n).delayamt = raw dlyN; engine via c_adderN = dlyN + (live?40:0) (C-036) | OBS |
| fbk1..8 | — | "fback" −100..0 dB, default −100 (F8.7) | fback switch N (select=mswN mute; in1=−100 const, in2=fbkN) → c_divide18..25 (÷20, add 0x18013A110) → c_power9..16 (10^x) → c_fltr_aN ⇒ linear gain 10^(dB/20); mute ⇒ 10^(−5) = 3.162e-3 (C-037) | OBS |
| pan1..8 ([28][42]…) | — | −1..+1, res 0.01, default 0 (F8.7) | feeds stereomixer3 panN; mix law = trigonometric/equal-power family (D.5) | OBS |
| m switch N ([25][39]…) | — | gainN select for stereomixer3 (§7.6) | ognN or −100 dB by mswN (mute) | OBS |
| fback switch N ([27][41]…) | — | select=mswN; in1=−100; in2=fbkN (C-037) | mute/arm selector of the per-voice fb-gain chain | OBS |
| int table N / int table switch N / int sub N ([45..47]) | add 0x18013A8D0 | nentries=29; in1..in29 = 0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,25,26,28,28 (§4.4) | mux rule v = 14+s+δ, δ=1 iff s mod 7 ∈ {2,6} (reproduces all values; R18); interval path intN → int table N → int table switch N → int sub N (−14) → c_multiply N → shiftN (§4.5) | OBS(values) / DER(rule) |
| rdm noise [212] | add_fn 0x180139C50; process FUN_180139B40 | record EMPTY (F10.1/C-057) | lagged-Fibonacci additive generator: state pair seeds 0xD00/0x1100, mask 0xFFFFFF00 (built 0x180139C13–C30), shift 8, scale 0x34000001 = 2⁻²³·(1+2⁻²³); noise = (sar state>>8)·scale; recurrence (a,b)→(17a+12b, 24a+17b) mod 2²⁴, emissions {2a+b, 4a+3b, 10a+7b, 24a+17b}; period 2²²; arithmetic range [−(1+2⁻²³), +1.0], orbit-actual [−1.0, +1.0] (C-057) | OBS |
| rdm abs [213] | — | |noise| inline in the audio worker (andps 0x7FFFFFFF @0x180412C60, sites 0x180103F16–78; §5.4) | absolute-value front of the random chain | OBS |
| rdm sh N / rdm slew N / rdm comp N / rdm mult N | FUN_180139970 / FUN_180139790 / FUN_180139480 / — | rdm sh = S&H triggered by rdm comp N; rdm slew walks toward the held target with pslew/nslew, clamped AT target; rdm comp = 4-sample comparator re-arm; rdm mult N = 2000·(rdm a-c ?/1.5 + 0.5) (§5.4) | shared-source random chain: rdm noise → rdm abs → rdm sh N → rdm slew N → rdm comp N → rdm a-c N → rdm mult N; name rotation: rdm mult 2..8 take input from rdm add 1..8, rdm mult 1 takes rdm add 8 (§5.4); slew coefficient FUN_1800CA260 = clamp(1/(sr·ms·0.001), ≤1, ≥1.192093e-07); config applier FUN_180139650 publishes slew rates (bipolar pair) | OBS |
| rdep [10] | — | "rnd dpth" 0..200, default 50 (F8.7/C-058) | → modamt1..8 → page[10] = clamp(modamt·1/2400, ±1) (double 1/2400 @0x1804120B0); dead authoring duplicate: c_divide26 [11] = rdep/2400 → c_fltr_a36 [12] → multiply12 [244] mirrors the internal product | OBS |
| dead post nets (C-033) | — | add8 [274]; c_fltr_a15..a21; bound; c_fltr_a13 (feeds only add8); top-level outputs iswitch/iswitch1; meter/LED sinks | no downstream consumer (dead/sink nets, all 3 profiles); Q-13 on add8 closed as a truncation artifact (§9.0) | OBS |

## D.3 Anti-alias cutoff law (instrument tables) [OBS/DER(units), C-038 / C-039]

Chain (verbatim topology, C-038): `c_multiply N = −(int table switch N − 14)` (scale
degrees, 7/octave) → `c_minmax2 [263] = max(8 voices)` → `c_divide2 = /7` →
`c_multiply6 = −` → `c_power1 = 2^(−max/7)` → `c_fltr_a14 (tc 20)` →
`bound1 [268] = clamp(0,1)` → `modiirq [271].modfreq` (n_sections=2, q=0.7, type=0 LP).
Cutoff opens to the top once the largest down-shift ≥ 0; switched by the AntiAliasOnOff
instrument table via iswitch8 [272] — not a user switch; "semitones shifted down" wording
retracted (degree domain). EQ interplay: eqnN freq1=CutFreq, boost1 = c_boundN =
clamp(c_multiply 8N, MaxCut, 0) dB (§7.4). Instrument tables (inst 0..12, 16-entry,
13 reachable; in14..in16 dead) — verbatim from C-039 [OBS]:

| table | entries (0..15) |
|---|---|
| MaxCut [259] | 0, 0, −14, −14, −14, −14, −14, −18, −14, −7, −14, −14, −7, −18, −7, −7 |
| CutFreq [260] | 20000, 20000, 8000, 8000, 8000, 8000, 8000, 2000, 3500, 12000, 8000, 8000, 3500, 1200, 4000, 10000 |
| CutMultiplier [261] | 0, 0, −1, −1, −1, −1, −1, −1.2857, −1, −0.5, −1, −1, −0.5, −1.2857, −0.5, −0.5 |
| AntiAliasOnOff [262] | 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0 |

## D.4 Global feedback loop topology [OBS, C-040 / R22-3 / F10.3]

```
 stereomixer3 [338]-left ---> feedback [345]   (in1; amp1=1, amp2=mfbk, tc=20)
 stereomixer3 [338]-right --> feedback1 [339]  (in1)
        ^                                       |
        | in2 = loop return                     v
        |                            softclip [346] / softclip2 [340]
        |                            (select=1, drive=0, order=1)
        |                                       |
        |                                       v
        |                            delay_xf [347] / delay_xf1 [341]
        |                            (maxdelay=2400, xfadetime=20,
        |                             tapdls1 = c_minmax-maximum
        |                             = max(fbkd,50); add 0x180137630)
        |                                       |
        |                                       v
        |                            "just for the filter" [348] / 2 [342]
        |                            (fractional-delay kernel readers,
        |                             add 0x1801374D0, phase int(frac*255),
        |                             maxdelay=1, delayamt=0)
        |                                       |
        |                                       v
        |                            highcut [349] / highcut1 [343] (freq=5)
        |                                       |
        |                                       v
        |                            subtract [350] / subtract1 [344]
        |                            (in1 - highcut)
        |                                       |
        +------ back into feedback*.in2 <-------+
```

- Taps are POST-PAN per-channel (stereomixer3-left/right), not a mono sum; mono sum only
  at the output stage [OBS, R22-3 / F10.3].
- subtract = 5 Hz DC blocker H(z) ≈ (1−z⁻¹)/(1−az⁻¹) [OBS/DER, C-040].
- fbkd [no idx] "fback dly" 0..5000 ms default 0 → c_minmax (in1=fbkd, in2=50) →
  tapdls1 = max(fbkd, 50), bounded by maxdelay 2400; 20 ms crossfade on tap changes
  [OBS, C-041/Q-11 CLOSED].
- softclip transfer curve and c_minmax unit semantics UNK [UNK, R22S / §9.1].
- Two nested feedback systems exist: per-voice echo loop (addN ← multiplyN, A.4) and this
  global wet loop, both with explicit DC blocking / soft clipping [OBS, §6.6].

## D.5 stereomixer3 pan law — R26 finding [OBS, F7.3]

- The mix sub's pan computation is **TRIGONOMETRIC** — FUN_18013DD34..0x18013DEAB (mix
  family region): magic-static **0.70710677 (1/√2)** (constant @0x1804120A4, the only
  DSP-side .text reference; lazy-initialised into the .data static @0x1804CCD9C by the
  guard helper 0x18013E070; read at 0x18013DD77), the constant 0.5, a `0.5/g` division,
  a >1 comparison, a conditional ×0.70710677, a double-precision sqrt, **atan2f
  (0x180180200) called TWICE** (0x18013DE2E, 0x18013DE51), a trig call (0x180350400,
  sin/cos family) at 0x18013DE7A, plus a 1/(ninputs/2) = 1/4 pair normalisation and
  sqrtf (0x180351690); per-input pan results stored at [r15+0x20/0x24/0x34].
- ⇒ **equal-power/trigonometric pan family is OBS-established; the linear
  0.5·(1±p)·g law is REFUTED for the original** (no such arithmetic exists at these
  sites; the whole-pool constant scan shows no linear-law constant cluster).
- monoSumWetChans' 0.707/0.707 amps (record-level) are consistent with the same
  equal-power family.
- Exact closed form of the two-branch construction: DER-partial (branch algebra not
  fully re-derived); family = OBS [F7.3].

## D.6 De-zipper (c_fltr_a) classes [OBS, §7.12]

c_fltr_a family add 0x18013AAC0, tc classes: tc 0 = no smoothing (sht normaliser — dead);
tc 10 = dry/wet gains (mmix path); tc 20 = per-voice fb gain, rnd depth, anti-alias freq,
mix/feedback/monoSumWetChans; tc 25 = stereomixer3. The graph's mix/feedback nodes are
level-slewed summers. Per-sample slew arithmetic beyond the tc classes: UNK.
