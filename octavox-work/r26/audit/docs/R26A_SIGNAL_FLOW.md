# R26 Appendix A — SIGNAL FLOW (three IO profiles)

Sources (only these; no outside knowledge, no invention): `notes/CLAIM_LEDGER_INPUT.md`
(consolidated R5–R24 corpus; cited as C-xxx / §n), `notes/R26_FRESH_FINDINGS.md` (F1–F10,
fresh machine-verified this round), `OCTAVOX_R26_COMPLETE_STATIC_AUDIT.md` §2, and
`docs/R26C_CONTROL_DATAFLOW.md` (control plane — cross-referenced, not duplicated).
Static-only discipline: the target (`Ox.vst3`) was **never executed** in any round; no
runtime claim exists anywhere in the corpus. Every non-trivial statement carries
(classification, claim/findings id, VA when the sources give one); classifications are
copied exactly — no UNK is upgraded. mimo indices throughout (miso/siso shift: A.1);
`add_fn` only where the ledger carries one (`—` = not in the sources); `[no idx]` =
index-less module (F1.4).

## A.1 Profile selection

| (inCh,outCh) | profile | modules | profile ctor VA | ctor alloc | dtonicii9 factory call site |
|---|---|---|---|---|---|
| (1,1) | mimo | 395 | FUN_1800F00A0 | 0x20C30 | 0x1800F6F1A |
| (1,2) | miso | 394 | FUN_180109D40 | 0x20B10 | 0x180110BBA |
| (2,2) | siso | 394 | FUN_180122E00 | 0x20B10 | 0x180129C7A |
| other | NullAlgorithm | — | — | — | — |

All rows [OBS, C-030 / F1.4 / F2.1]; factory = FUN_180137E20, called from exactly the 3
profile ctors above. Runtime selector `FUN_18030C6A0(parent,cfg,inCh,outCh)` [OBS, C-030]:
zero direct call refs; stored as a function pointer at .rdata `0x18040A2F8` (dispatch
table) — alive via indirection [OBS, F8.3]. Alloc sizes match module counts (extra slot =
iswitch9) [OBS, C-030]. Engine ctor `FUN_1800E0360` 1× (0x180137E6A); engine dtor
`FUN_1800E7DF0` 4 refs (0x1800E7DBF + 3 profile cleanups) [OBS, F2.1].

- mimo-only module: iswitch9 [360]; miso/siso 394 each; corpus verbatim: "indices ≥357
  shift by 1 in miso/siso" [OBS, F2.5 / C-042]. 9 index-less controls identical in all
  profiles: igan, inch, c_subtract1, live, live mode delay comp, c_switch, mfbk, fbkd,
  c_minmax [OBS, F1.4].
- inch/ouch steer the mono/stereo fold *inside* the chosen patch — they do not select
  the profile; patch presets' inch/ouch defaults equal the key [OBS, C-030 / §8.4].
  stereomixer3 composite (add_fn 0x180137720) constructed once per profile:
  0x1800F8834 / 0x1801124D4 / 0x18012B594 [OBS, F7.1].

## A.2 Master signal chain (mimo indices) [OBS, C-034 / C-040 / C-042 / C-043 / F9 / F10]

```
 igan [no idx]   (input gain -96..+12 dB; F8.7)                    add_fn: —
   |
   v
 left in [247]  +  right in [250]
   |                 |
   |                 +--> mix [253] = (L+R)/2, tc=20 (mono fold; C-034);
   |                       c_subtract1 [no idx] = inch-1 (stereo flag)
   v
 iswitch4 [254]   (inch fold select; C-034)                         add_fn: —
   |  live-mode dry-path compensation (F9.1, OBS): live mode delay comp
   |    [no idx] value=40, add 0x1800CB200 -> delay comp 1 [248] / 2
   |    [251]  maxdelay=51, delayamt=40, add_fn 0x180138D30 (sub-ctors
   |    0x1800D2B10/0x1800D2840; F9.2)
   v
 iswitch6 [249] / iswitch7 [252]   (select=live-out; in1=delay comp 1/2-out,
   |   in2=left/right-in-out; F9.1)  == the live-compensated dry path
   v
 iswitch8 [272]  (anti-alias switch; AntiAliasOnOff [262] instrument table
   |   -> modiirq [271]; C-038; law in Appendix D.3)
   v
 softclip [273]   (select=1, drive=0, order=1; C-034)               add_fn: —
   |
   v
 +=====================================================================+
 | dtonicii9 [275]   factory FUN_180137E20; engine dsp_t @ wrapper     |
 | +0x430; vtable 0x180409F60; prepare FUN_1800E2030 (C-001)           |
 | input ring: engine+0x41A0 (write idx +0x41B8, -4/block; mask       |
 |   +0x41C0); 4 input samples/prepare, [r10+0..0xC] -> W..W-3;       |
 |   C-004/C-005, F3.5)                                                |
 |   v                                                                 |
 | tracker: engine+0x4518, FUN_18013BBC0(engine+0x4518, input+1, 1);  |
 |   consumes 2 of every 4 samples (p[1], p[3]) (C-003)                |
 |   v                                                                 |
 | record ring: engine+0x98 (4096 floats, mask 0xFFF, write ptr        |
 |   engine+0xA4FC); writer descending {correction, i4, i5,           |
 |   period = raw bits of T+0x5FD4} (C-048, F3.1)                      |
 |   v                                                                 |
 | 8 voices: work ring engine+0x40A0 (stride +0x409C = 4096/8192/     |
 |   16384 by SR class, mask +0x40B8, write ptrs +0x40C0; C-006);     |
 |   kernel 256x6 @0x1803B6170 -> engine+0x40F0 (0x1800E1FB4;          |
 |   F10.2); voice pages A/B +0x4280/+0x42A8 (44 floats/voice),       |
 |   selector +0x42D0, 1 -> page B (C-052)                             |
 |   v                                                                 |
 | voice outputs: per-voice ports @ *(engine+0x80), stride 0x18        |
 +=====================================================================+
   |
   v  (8 voice outputs)
 [ 8 x per-voice chain -- expanded in A.4 ]
   v
 stereomixer3 [338]  add_fn 0x180137720 (ninputs=8, tc=25; clip sub
   |  FUN_1800D8230 @ +0x260 + mix sub FUN_1800D7400 @ +0xF0; gainN = m
   |  switch N-out; panN = panN-out; pan = trigonometric/equal-power
   |  family, F7.3 -- detail in Appendix D.5)
   |  stereomixer3-left / -right  (post-pan taps; R22-3)
   v
 feedback [345] / feedback1 [339]  (in1 = stereomixer3-left/right;
   |  in2 = loop return; amp1=1, amp2=mfbk 0..100%; tc=20; F10.3)
   |  global wet loop, per-channel (post-pan, not a mono sum; R22-3):
   |    softclip [346] / softclip2 [340] (select=1, drive=0, order=1)
   |    -> delay_xf [347] / delay_xf1 [341] (maxdelay=2400, xfadetime=20,
   |       tapdls1 = c_minmax-maximum = max(fbkd,50); add_fn 0x180137630)
   |    -> "just for the filter" [348] / 2 [342] (fractional-delay kernel
   |       readers, add_fn 0x1801374D0, phase int(frac*255); C-068)
   |    -> highcut [349] / highcut1 [343] (freq=5)
   |    -> subtract [350] / subtract1 [344] (in1 - highcut) = 5 Hz DC
   |       blocker H(z) ~ (1-z^-1)/(1-a*z^-1) (C-040)
   |    --> back into feedback.in2 / feedback1.in2
   v
 iswitch11 [362]  (ouch select: c_subtract3 = ouch-1; in1 =
   |  monoSumWetChans [357] (add_fn 0x180138710, slot 0x1E990, amp1=amp2=
   |  0.707 = 0.707*(feedback+feedback1) equal-power mono fold); in2 =
   |  feedback / feedback1 per-channel) => ouch=1 mono fold / ouch=2
   |  stereo (F10.4); mimo also carries iswitch9 [360] (F2.5/C-042)
   v
 LWetDryMix [364] / RWetDryMix [365]
   |  wet = iswitch9/11-out x c_fltr_a1/a2/a3 (tc 10) x mmix [207];
   |  dry = iswitch6/7-out (live-compensated) x smoothed 1-mmix
   |  (c_fltr_a tc 10 + c_subtract2 = 1-mmix; F10.4); out =
   |  slew10(dry*(1-mmix)) + slew10(wet*mmix) (ledger §7.11); output-
   |  stage multiplies: multiply [356]/multiply1 [363]/multiply2 [361]/
   |  multiply3 [359] (ledger §2 index list)
   v
 left out [366] / right out [367]   (ogan [210], -96..+24 dB; F8.7)
   |
   v
 bypa [211]   (normal/bypass hard output-domain switch; iswitch [369] / iswitch1
   |   [370]; C-042; C-033 lists iswitch/iswitch1 among the dead (sink) nets --
   |   both verbatim, no reconciliation; iswitch2 [368] role not described)
   v
 meters [371]..[395]  (peak1/peak2 -> LevMetrAtoC -> LevLintodB -> lopD/ropD (ganred:);
   input peaks peak4/peak5 -> lipD/ripD; overload LEDs thresh -0.1 -> lopL/ropL/
   lipL/ripL (monitor:); pitM/tonM display dtonicii9 pitch/tonality; C-043)
```

## A.3 Per-profile IO frontiers [OBS, C-030 / F2.5]

```
mimo (1 in -> 1 out), 395 modules:
  in (1)  -> igan -> left in [247]  (right in [250] + mix [253] + iswitch4
             [254] fold; inch/ouch steer the fold inside the patch, C-030)
  out (1) <- bypa [211] <- left out [366]/right out [367] (ogan [210])
  mimo-only: iswitch9 [360]; "indices >=357 shift by 1 in miso/siso" (F2.5/C-042)

miso (1 in -> 2 out), 394 modules:
  in (1)  -> igan -> left in [247]  (input fold per inch, A.2)
  out (2) <- bypa [211] <- left out [366] + right out [367]
  no iswitch9; >=357 index-shift statement applies (F2.5)

siso (2 in -> 2 out), 394 modules:
  in (2)  -> igan -> left in [247] + right in [250]  (mix [253]/iswitch4 [254]
             fold still available via inch)
  out (2) <- bypa [211] <- left out [366] + right out [367]
  no iswitch9; >=357 index-shift statement applies (F2.5)
```

Note [UNK, bounded]: the sources document the module graph, the (inCh,outCh) key and the
inch/ouch record-level fold; the host-bus -> module feed wiring (which channel feeds
left in [247]/right in [250], and how the outCh bus is fed) is not stated in the sources
— UNK. Patch presets' inch/ouch defaults equal the key [OBS, C-030].

## A.4 Per-voice chains (×8) [OBS, C-035 / C-036 / C-037 / C-056 / C-058]

```
 voice N output (per-voice port @ *(engine+0x80)+0x18*(N-1); §7.14)
   |
   v
 modfilterN  (freq=20, q=1, output "-high"; in = dtonicii9-outN; §7.3)
   |
   v
 eqnN  (neqs=1; freq1=CutFreq; qfactor1=0.7; boost1=c_boundN; type1=2;
   |   c_boundN = clamp(c_multiply 8N, MaxCut, 0) dB; c_multiply 8N =
   |   c_multiply N x CutMultiplier; boost driver = the voice's
   |   interval, not shtN; §7.4)
   v
 addN  <--------------------------------------------+
   |                                             |
   v                                             |
 stereomixer3 [338] in N  (panN; gainN = m switch |
   N-out = ognN or -100 dB; §7.6)                 |
                                                 |
 moddelayN  (echo: maxdelay=2000, declick=1,     |
   delayamt = raw dlyN; add_fn 0x180137930;     |
   delay publisher FUN_1800D4710 (vtable        |
   +0x58), 16 call sites = 8 voices x 2 trees;  |
   F2.3/F9.2)                                   |
   |                                             |
   v                                             |
 multiplyN = moddelayN x fb gain ---------------+
   (mimo: multiply4..multiply11; C-035)

 fb gain chain (C-037, OBS): fback switch N (select=mswN mute;
   in1=-100 const, in2=fbkN) -> c_divide18..25 (/20, add 0x18013A110)
   -> c_power9..16 (10^x) -> c_fltr_a12..a35 (add 0x18013AAC0, tc 20)
   => linear gain 10^(dB/20); mute => 10^-5 = 3.162e-3

 engine-side inputs of voice N (C-036 / F9.1, OBS):
   c_adder8..15 (add FUN_1800C7460) = dlyN + c_switch (= live ? 40 : 0)
   -> dtonicii9.delayamtN; c_multiply 1..8 (add FUN_18013A250) ->
   shiftN; shtN -> pcentN (cents added to the ratio argument; C-056);
   rdm slew N-out -> dtonicii9.modN; rdep -> modamtN (C-058)
```

| v | modfilter | eqn | add | moddelay (echo) | fb multiply | fb-gain smoother | mixer in |
|---|---|---|---|---|---|---|---|
| 1 | [276] modfilter8 | [277] eqn | [278] add | [279] moddelay7 | [270] multiply4 | [269] c_fltr_a12 | in 1 |
| 2 | [284] modfilter9 | [285] eqn1 | [286] add1 | [287] moddelay6 | [283] multiply5 | c_fltr_a23 | in 2 |
| 3 | [292] modfilter10 | [293] eqn2 | [294] add2 | [295] moddelay5 | [291] multiply6 | c_fltr_a25 | in 3 |
| 4 | [300] modfilter11 | [301] eqn3 | [302] add3 | [303] moddelay4 | [299] multiply7 | c_fltr_a27 | in 4 |
| 5 | [308] modfilter12 | [309] eqn4 | [310] add4 | [311] moddelay3 | [307] multiply8 | c_fltr_a29 | in 5 |
| 6 | [316] modfilter13 | [317] eqn5 | [318] add5 | [319] moddelay2 | [315] multiply9 | c_fltr_a31 | in 6 |
| 7 | [324] modfilter14 | [325] eqn6 | [326] add6 | [327] moddelay1 | [323] multiply10 | c_fltr_a33 | in 7 |
| 8 | [332] modfilter15 | [333] eqn7 | [334] add7 | [335] moddelay | [331] multiply11 | c_fltr_a35 | in 8 |

Table source: ledger §2 per-voice chain map (`voice_chain_map.txt`, mimo indices), OBS.
"The internal names are offset from the voice index; the wiring is authoritative —
`dlyN`, `fbkN` select the voice." (D1 §6.1, verbatim.) Engine-side counterparts (voice 1
→ dly1/c_adder8/c_multiply 1/sht1 … voice 8 → dly8/c_adder15/c_multiply 8/sht8;
mod1..8 ← rdm slew 1..8; modamt1..8 ← rdep) [OBS, C-032 / F2.4].

## A.5 Worker execution [OBS/DER, C-004 / C-031 / C-055 / F2.1–F2.3]

| tree | audio worker (lines) | engine prepare call | control worker (lines) | update publisher call |
|---|---|---|---|---|
| miso | FUN_180103A10 (475) | 0x1801043DB | FUN_1801061A0 (307) | 0x180106B2F |
| siso | FUN_18011D5F0 (474) | 0x18011DFBB | FUN_18011FD60 (306) | 0x1801206EF |

- The two worker trees are per-IO-profile clones of the same graph executor — NOT channel
  branches; operand order + module base offsets differ; one references `param_1+0x1E990`
  = monoSumWetChans, mimo-only [OBS/DER(resolved), C-031]. Update publisher
  `FUN_1800E5160` is called from exactly 2 sites: 0x180106B2F (miso) + 0x1801206EF
  (siso), `rcx = kernel+0xBFB8` — matches corpus C-055 bit-exact [OBS, F2.2].
- 4-sample block framing: engine processes 4 input samples per prepare call, 4 output
  samples per voice; host buffer chunked to 4-sample sub-blocks (SSE-width stores)
  [OBS/DER, C-004].
- Fresh call-site xrefs (all [OBS], F2.1/F2.2/F2.3): engine ctor FUN_1800E0360 1×
  (0x180137E6A); factory FUN_180137E20 3× (0x1800F6F1A/0x180110BBA/0x180129C7A); engine
  prepare FUN_1800E2030 2× (0x1801043DB, 0x18011DFBB = the two trees); engine dtor
  FUN_1800E7DF0 4 refs (0x1800E7DBF + 3 profile cleanups); update FUN_1800E5160 2×
  (above); moddelay publisher 0x1800D4710 16× (8 voices × 2 trees); per-voice SVF
  FUN_1800E8B50 16×; rdm noise FUN_180139B40 2× (one per tree); tracker FUN_18013BBC0
  1× (engine prepare 0x1800E20CE); record writer FUN_1800E0190 1× (tracker state 8 =
  0x18013D142); state-7 FUN_1800DFE80 1× (0x18013D12A); parabolic refine FUN_18013E160
  2× (0x18013D047, 0x18013D056).
- m01/m02 + control-plane plumbing: see Appendix C (`docs/R26C_CONTROL_DATAFLOW.md`).

## A.6 Summing laws UNK (record-level only; no per-sample arithmetic documented)

1. Exact per-sample mix accumulator order (stereomixer3 mix sub, feedback/feedback1
   summers, LWetDryMix/RWetDryMix) — **UNK** (main audit §2).
2. Executor module evaluation order — index-order (DER) with the graph offsets OBS
   (main audit §2).
3. stereomixer3 pan law: family = trigonometric/equal-power [OBS, F7.3]; exact closed
   form of the two-branch construction — DER-partial (branch algebra not fully
   re-derived); the linear 0.5·(1±p)·g law is REFUTED (F7.3).
4. softclip / softclip2 transfer curve — **UNK** (records carry select/drive/order only;
   ledger §9.1, R22S). c_minmax unit semantics — **UNK** (R22S; F10.3 record-level only).
5. The graph's mix/feedback nodes are level-slewed summers (tc 0/10/20/25 classes, add
   0x18013AAC0) [OBS records, ledger §7.12]; per-sample slew arithmetic beyond the tc
   classes is undocumented; runtime behaviour of the above — **UNK by construction**.
