# OCTAVOX R26 — COMPLETE STATIC RECONSTRUCTION AUDIT
**Eventide Octavox VST3 — deepest static reconstruction, contradiction resolution round.**
Round: R26. Date: 2026-11-23. Auditor: PitchLab RE agent (R26 static-audit session).

## 0. Scope, target, method, and rules honoured
- Target binary: `upload/Ox.vst3` — PE32+ x64 DLL (single-file VST3 bundle), 5,307,392 bytes.
  SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` — **verified
  before AND after every analysis pass** (all passes read-only).
- Method: self-contained PE parser + Capstone 5.0.9 (scripts in `octavox-work/r26/audit/scripts/`),
  `.pdata` function map (13,116 unwind scopes), whole-`.text` cross-reference scan
  (1,065,880 instructions, 0x180001000–0x18037D000), targeted disassembly, numeric analysis
  (numpy) of extracted tables. **The target was never executed.** No DAW, no audio, no new
  engine code, no VST build, no CI. `main` untouched (@ ac6612c); no merge.
- Claim grammar (per statement): **OBS** = observed in code/bytes this round; **DER** = derived
  from static reasoning (chain given); **UNK** = not established; **CONFLICT** = contradicted
  (with the resolution). Prior-corpus claims that this round re-derived are marked
  "reconfirmed"; new results are marked "NEW". The prior corpus = the r22 evidence tree
  (committed, hash-intact) — see Appendix F and `notes/CLAIM_LEDGER_INPUT.md`.
- Evidence files (this round): `octavox-work/r26/audit/evidence/` (module extraction JSON,
  tables JSON, kernel npy, xref JSON, pdata maps) + `notes/R26_FRESH_FINDINGS.md` (F1..F10).

## 1. Evidence integrity — REQUIRED FIRST
| item | status | note |
|---|---|---|
| Ox.vst3 SHA-256 | **PASS** (pre+post every script) | matches spec exactly (F1.1) |
| R25 package `PitchLab_R25_DSP_Chain_Research_Package.zip` | **FAIL — MISSING** | Not present anywhere in this sandbox (public/, octavox-work/, git objects). Only `main`@ac6612c exists; the R25 branch and its archive were lost across sandbox resets (worklog records ≥6 resets). Its SHA-256 (`46e891c7…962244`) is therefore NOT re-verifiable, and `analysis_r25/` is not on disk. **Consequence:** every R25-derived statement was re-derived in R26 from the binary + the surviving r22 corpus; nothing in this audit cites the R25 zip as evidence. |
| r22 evidence corpus (committed) | PASS | The r22 tree (`octavox-work/r22/…`, docs + decompiles + records) is intact in git (lineage fb01b3f/37f5386); the claim ledger consolidated from it is `notes/CLAIM_LEDGER_INPUT.md` (75 canonical claims + contradiction register). |
| Prior-session leftovers | documented | Untracked `octavox-work/r26/source/` + `public/PitchLab_R26_SIMD_ReadKernel_Package.zip` = the superseded "old R26" SIMD-read-kernel round; left untouched, excluded from this audit (F1.3). |

## 2. Complete module map and IO-profile signal flow
- [OBS, reconfirmed] Three kernel profiles by (inCh,outCh): **mimo** (1,1) 395 modules /
  **miso** (1,2) 394 / **siso** (2,2) 394; runtime selector FUN_18030C6A0 → ctors
  FUN_1800F00A0 / FUN_180109D40 / FUN_180122E00. `inch`/`ouch` steer the mono/stereo fold
  *inside* the chosen patch (record level), they do not select the profile.
- [OBS, NEW] Worker-tree xrefs: dtonicii9 factory FUN_180137E20 called from exactly the 3
  profile ctors (0x1800F6F1A / 0x180110BBA / 0x180129C7A); engine prepare called 2× (0x1801043DB,
  0x18011DFBB); engine dtor 4 refs; update publisher FUN_1800E5160 exactly 2 call sites
  (0x180106B2F miso, 0x1801206EF siso) — the "two parallel worker trees = per-profile clones"
  verdict (C6) is machine-reconfirmed.
- [OBS] Index-less global controls (9, all profiles): igan, inch, c_subtract1, live,
  live mode delay comp, c_switch, mfbk, fbkd, c_minmax.
- [OBS] mimo-only module: iswitch9 [360] → all indices ≥357 shift by −1 in miso/siso.
- Per-voice chain (mimo indices; wiring authoritative — `dlyN`,`fbkN` select the voice):
  modfilter8..15 → eqn..eqn7 → add..add7 → moddelay7..moddelay (echo, maxdelay=2000,
  declick=1, delayamt=raw dlyN) → multiply4..11 (= moddelay·fb-gain) → c_fltr_a12/a23/a25/…/
  a35 (tc-20 smoothers of 10^(fbk/20)) → stereomixer3 in1..in8; engine-side: c_adder8..15 =
  dlyN + c_switch(40·live) → dtonicii9 delayamtN; c_multiply1..8 → shiftN; shtN → pcentN.
- Stereo output chain (post-mixer): stereomixer3 (composite: clip sub module-id 0x152 +
  mix sub, ninputs=8, tc=25) → feedback/feedback1 → softclip/2 → delay_xf/1 →
  "just for the filter"/2 → highcut/1 → subtract/1 → back into feedback*.in2 (global loop);
  wet/dry: mmix → c_fltr_a1/a2/a3 + c_subtract2(1−mmix); iswitch11 (select=ouch−1):
  in1 = monoSumWetChans (0.707/0.707) | in2 = feedback (per-channel); LWetDryMix/RWetDryMix →
  left/right out (ogan dB) → bypa switch → meters.
- Static summing laws that are not determinable from records alone are marked UNK in
  Appendix A (e.g. the exact per-sample mix accumulator order; the executor's module
  evaluation order is index-order DER with the graph offsets OBS).

## 3. dtonicii9 engine internals (complete)
Class `modpp::modules::dtonicii_t::dsp_t`, wrapper+0x430, vtable 0x180409F60
({dtor 0x1800E7DB0, m01 0x1800E10B0, m02 0x1800E1520, base thunks}), base dsp_shared 0x180409F20.
- **Work ring** [OBS]: engine+0x40A0 data, +0x409C stride (4096/8192/16384 floats by SR class),
  +0x40B8 mask, +0x40C0 per-voice write-ptr array; written `−(g_v·input_ring[(D_v+k')&M])`
  4 descending writes per prepare (fresh: 0x1800E226B–0x1800E231F with the −0.0 xorps).
- **Input history ring** [OBS]: +0x41A0 data, +0x41B8 write index (−4/block), +0x41C0 mask;
  size = pow2 ≥ sr·0.001·maxdelay_ms (FUN_18013E200); 4 input samples per prepare call
  (fresh: [r10+0..0xC] → slots W..W−3, 0x1800E204A–0x1800E20C0).
- **Pitch tracker** [OBS, reconfirmed] at engine+0x4518: 2-of-4 input samples per call
  (p[1], p[3]); 64-float raw ring T+0x5DB0 → symmetric FIR (T+0x5EB0, taps 5/6 | 10/11 | 20/21
  by SR class — **fresh: m01 id 5 writes the 6/10/20-coefficient blocks at +0xA3C8..+0xA3F0
  and cadence {2,4,8} at +0xA42C with thresholds 50000/100000**) → adaptive gate (peak decay
  T+0x5F20, multiplier T+0x5F28 = m01 id 4 target) → 512-float detection ring T+0x800 (≈12 kHz
  cadence) → 10-state FSM T+0x5FF8 (arm 81; correlate 48-tap window over 243 lags with
  v·243 / 1/v from m01 id 6; peak+parabolic refine FUN_18013E160; verify; accept/reject; confidence
  0.203125/210-test; period scaling ×2/×4 while < 102400; state 7 = FUN_1800DFE80 ω/hysteresis/
  correction — fresh: ω = 2π·2048/T̂, stability ≤ 0x7FFFFF, hard reset on |Δω| ≥ 4e-4; state 8 =
  FUN_1800E0190 record writer; state 9 AGC).
- **Record ring — FULLY RESOLVED (Section 9 item closed)** [OBS, NEW full disasm both sides]:
  4096 floats at engine+0x98, mask 0xFFF, write index engine+0xA4FC (= T+0x5FE4), gated by
  scale-mask bit (i4+1) of int[12]@T+0x6038 (port 27), masked-mode flag T+0x5FE8=1 (set by the
  engine's tracker call, 0x1800E20CB).
  - Writer (FUN_1800E0190), descending slots: **(W−1) = correction** (new running gain after the
    ratio-limiter walk, clamped 2^(±1/(12·correctrate))); **(W−2) = i4 = int(T6058[0]) = int(port-26
    matched-row field5)**; **(W−3) = i5 = int(T6058[−4]) = int(matched-row field4) = the interval
    index**; **(W−4) = period = raw bits of T+0x5FD4** (mov-copied, 0x1800E0341/0x1800E034B).
  - Consumer (prepare 0x1800E25DB–0x1800E269F), ascending from idx = (recidx[v]+wdelta)&0xFFF:
    **ring[idx+0] = period** (×2⁻¹¹, min-clamp 48, degenerate fallback 480 — *480 is the
    silent-record fallback, not an upper clamp*); **ring[idx+1] = interval** (int → page[2+i]
    target-ratio column); **ring[idx+2] = dead field** (written, never read — only idx+0/+1/+3
    reads exist); **ring[idx+3] = correction** (rate-slot = page[0]·corr; replaced by 1.0 when
    pitchtracking=0; fallback page[0] when ≈0).
  ⇒ the round-9 order {period, i5, i4(dead), correction} and the round-15 dead-field verdict are
  BOTH reconfirmed instruction-for-instruction; T+0x6058 is identified (NEW) as the port-26
  72-float table data pointer (also cached at T+0x6028; advanced by state-7's matched-row scan),
  so the record's i4/i5 originate from the matched harmony row's fields 5/4.
- **44-float voice pages** [OBS, reconfirmed]: port 21 A (+0x4280), port 22 B (+0x42A8), selector
  port 23 (+0x42D0, 1→B; fresh prepare sites 0x1800E2444–0x1800E2456); published element-by-element
  onto the inactive page then flipped (publishing sites 0x1800E7749…0x1800E7859). Contents
  (corpus R9/R12, stable): page[0] = ratio/16 glide; page[1] = target ratio = page[2+i5];
  page[2..9] = degree-walk ratios/16; page[10] = clamp(modamt/2400, ±1) (fresh: fslot[v] read at
  0x1800E2348 as [vector_begin + 4·v]); page[11] = crossfade limit; page[12..43] = 32 period-column
  crossfade encodings (1/rate)·2²³ | octave, idx = clamp(int((v−48)/16), 0, 31) (fresh sites
  0x1800E269B–0x1800E26BB: (xmm0−48)·0.0625 vs 31.0, cvttss2si).
- Engine-side slot copies per block [OBS]: slot_40F8[v] = page[10]; slot_4128[v] =
  page[0]·ring[idx+3] (fallback page[0]); slot_4110[v] = page[12+idx]·2⁻²³.
- m01/m02 complete parameter surfaces: see Appendix C (F4.1/F4.2 — jump-table-exact, ids 0..17
  and 7..13 with every case's store target).

## 4. The 8-voice generator
- [OBS/DER, reconfirmed] Per voice: work-ring read position `pos` (double, +0x4188) advanced by
  **step = 4·P(f)·ratio·correction** per 4-sample block, where f = F·page[10] (F = rdm-slew walk
  element [0] of the mod port, fresh read confirmed at [engine[0x68]+8+0x10·v]);
  P = the quartic with coefficients c0..c4 at engine+0x40DC..+0x40E8 (fresh polynomial
  evaluation chain 0x1800E2371–0x1800E23D9: u = F·fslot[v] in double, Horner in u²…);
  ratio = page[0]/page[1] glide; correction = ring[idx+3].
- [OBS] **256×6 kernel table** @0x1803B6170 — R26 quantitative closure attempt (F5):
  - **Bit-exact palindrome (NEW)**: flat 1536-float h̃[m] = h̃[1535−m] ∀m; 608 distinct values.
  - DC gain ≈ 1 with ripple ≤ +1.22e-4 (max at r=58) — refines "rows sum 1.0".
  - Nyquist gain 0.0039 (r≈128) → 0.641 (r=0/255); |H(π/4)| ≈ 0.965–0.968.
  - **Eliminated (NEW, numeric)**: Farrow polynomial structure (residual plateau 2.6e-5 ≫ ulp);
    unconstrained LS fractional-delay (all cutoffs, ≥0.158 coeff error; row 0 is not a delta);
    windowed-sinc family (7 windows × 4 widths) and Gaussian fits (rms ≈ 0.27).
  ⇒ **generator UNK** (offline-optimised table by design; no closed-form candidate survives),
    characterisation DER-complete. Per the R26 bar, this is recorded as UNK-generator — not
    guessed as windowed-sinc.
- [OBS, reconfirmed] Engine read path: phase = floor(frac·256)&255 over all 256 rows
  (engine+0x40F0 installed at ctor 0x1800E1FB4); output-stage readers "just for the filter"/2
  use int(frac·255) (row 255 unreachable there); both consumers + a NEW third accessor thunk
  0x18032B720 (`lea rax,[kernel]; ret`) are the only .text references to the table.
- [OBS, reconfirmed] settled path = single interpolated tap; crossfade state +0x4158 (init 0,
  settles −1); re-arm compare `pos − delay < 8.000000000000007` (0x180412428); unity ⇒ frozen
  pos (rate bit-exact 1) — single fixed tap, no two-tap comb (Q-14 static premise).
- int_delay init 280 (+0x4140); glide scalars +0x44E0/+0x44F8; voice delays D_v = int[nv] +0x4258
  (read freshly at 0x1800E2200–0x1800E2222 in prepare).

## 5. Harmony mapping (key map → scale mux → temperament → interval mux → ratio)
- [OBS, byte-exact re-dump] Temperament 5×12 @0x1803BB690; 19-row scale patterns @0x1803BB780
  (row 12 chromatic with 99-fillers; rows 8/9 unreferenced); key map @0x1803BB610;
  **userscale map @0x1803BB60C = keymap rotated right by one (NEW)** — 17-entry chromatic
  spelling map; no direct rip-refs (region addressed via base 0x1803BB5F0 at 0x1800C9A73/
  0x1800C9F7C/0x18032CBA0 + computed offsets); the dtonicii9 record carries **userscale1..12 = 0**
  (12 slots, all zero in the shipped patch — the userscale path is record-declared but unwired).
- [OBS/DER, reconfirmed] Interval mux: 29 choices, default 14; rule v = 14+s+δ (δ=1 iff s mod 7
  ∈ {2,6}) reproduces all authored values; 8 collapsed label pairs (labels mislead: a "+3rd"
  sounds a 4th-class step — labels walk DOWN, − labels UP).
- [OBS, reconfirmed] scal-mux irregularity: chromatic→row 14 (penta maj), whole tone→15, …,
  neapolitan→13; "hungarian" (choice 16) exceeds the 15-entry table — authored data
  inconsistency kept verbatim. c_or=0 (all scales except chromatic/whole-tone) selects the raw
  choice index; else the mapped int-table value.
- [DER, reconfirmed] Key-ratio law symmetric ±temperament[|k−9|] (R17 "asymmetry" was a
  decompiler artifact — corrected R18); composition ratio_v = keyRatio·exp((voiceCents+tune)·
  ln2/1200); page scale ratio/16 internal (0.0625 double @0x1804121B0).
- [OBS, reconfirmed] The walk: 8 rows published to page[2..9] via the grid-fill (movups row
  loads freshly at 0x1800E64B5/64BF/64D5); reachable targets {0,2,3,4,6} mod 7 — 2nd/6th never
  targeted.

## 6. Random subsystem + the three delay/feedback systems
- [OBS, reconfirmed] rdm noise [212] (empty record — the round-4 dump gap): lagged-Fibonacci
  additive generator, seeds 0xD00/0x1100, mask 0xFFFFFF00, shift 8, scale 0x34000001; period
  2²²; |noise| → S&H → slew → comp re-arm; F = rdm slew N element [0]; modulation one-sided
  upward ∈ [0, +200¢] (R22 resolution).
- [OBS, reconfirmed] rdep → modamt1..8 → page[10] = clamp(modamt·1/2400, ±1); the dead
  authoring duplicate (c_divide26/c_fltr_a36/multiply12) mirrors the internal product.
- Delay systems (3): (1) per-voice echo moddelayN (maxdelay 2000, declick=1, delayamt = raw
  dlyN ms) with fb loop addN ← multiplyN = moddelay·10^(fbkN/20) (c_power via c_divide18..25 ÷20,
  tc-20 smoothers); (2) global wet loop: feedback/1 (amp2 = mfbk) → softclip/2 → delay_xf/1
  (maxdelay 2400 ms, xfadetime 20 ms fixed, tapdls1 = max(fbkd,50) via c_minmax) →
  fractional-delay readers → highcut 5 Hz → subtract → back to feedback.in2 (post-pan,
  per-channel taps — R22-3 reconfirmed at record level); (3) engine-internal: int_delay 280 +
  D_v (+ live-mode +40 via c_adderN) + work-ring write depth 3 + interpolation group delay ≤5.
- [OBS/DER] Live Mode: c_switch = live ? 40 : 0; engine delayamtN += 40; dry path delayed via
  delay comp 1/2 (delayamt = 40, maxdelay = 51) — **unit of 40: DER = samples (medium
  confidence; semantically coherent with the 288-sample structural latency), formally UNK at
  the conversion site** (delay-comp is a different module class than the ms-domain moddelay;
  see F9).

## 7. Post-processing modules
All module equations/topologies are tabulated in Appendix D. R26 highlights:
- **PAN LAW — RESOLVED TO FAMILY, LINEAR LAW REFUTED (NEW, Section 7 mandate)**: the mix
  family's pan computation (FUN_18013DD34..0x18013DEAB) is **trigonometric/equal-power**: the
  only DSP-side 1/√2 (0.70710677 @0x1804120A4, magic-static → 0x1804CCD9C via 0x18013E070,
  read 0x18013DD77), 0.5, 0.5/g division, double sqrt, **atan2f (0x180180200) ×2** (0x18013DE2E,
  0x18013DE51), a sin/cos-family call (0x180350400 @0x18013DE7A), sqrtf (0x180351690), and a
  1/(ninputs/2) pair normalisation; per-input coefficients stored at [r15+0x20/0x24/0x34].
  ⇒ **0.5·(1±p)·g is refuted for the original** (the R26 spec's premise, presumably from the
  lost R25 doc, does not hold at the instruction level). monoSumWetChans' 0.707/0.707 amps are
  consistent with the equal-power family. Exact closed form of the two-branch construction:
  DER-partial; family: OBS.
- [OBS, reconfirmed] stereomixer3 composite = clip sub (module id 0x152, int16-range markers
  0xC7000000/0x46FFFE00, 1.0f, −1s) + mix sub (ninputs=8, tc=25); the "vtable" question is
  resolved: the mix sub object's vtable is **0x180409E20** = {deleting dtor 0x1800D9E80 +
  dsp_shared base thunks} — no per-class audio slots; the arithmetic lives in the mix-family
  functions (the earlier "vtable UNK" is closed: the composite registers a standard module
  vtable; the earlier confusion came from misreading the ctor's dtor store).
- modiirq anti-alias (2-section IIR, q=0.7, LP, cutoff from the largest down-shift in scale
  degrees, instrument-table switched); modfilterN = per-voice 20 Hz HP; eqnN = 1-band cut EQ
  driven by the voice interval; softclip (select=1, drive=0, order=1 — feedback bus saturating
  by construction); highcut 5 Hz + subtract = DC blocker H(z) ≈ (1−z⁻¹)/(1−az⁻¹).
- Wet/dry + ogan/bypa/meters: reconfirmed at record level (F10.4); bypa is a hard output-domain
  switch (no ramp declared).

## 8. VST3/JUCE host interface
- [OBS] Topology: JuceVST3Component (6 COLs) → delegate +0x38 → VoxAudioProcessor (vtable
  0x1803F05D0, "Octavox"/"Ocxv", dtor 0x1800175D0; 3 vtable refs freshly: ctor 0x1800175DA,
  JUCE wrappers 0x1801A7590/0x1801A784F); getNumPrograms slot-46 reads +0x27DC (zero-init).
- **LatencySamples verdict CORRECTED (Section 8 mandate)**: FUN_180310F20 is NOT dead code —
  it is called from exactly ONE site (0x180347857) inside the **DebugReport builder**
  (FUN_0x180347500, "DebugReport"/"PluginEnvironment" XML diagnostic; 2 callers), writing
  LatencySamples = [env+0x24] as a diagnostic attribute. It is a debug-report environment
  dump, NOT a host latency report. The host-reported latency (`setLatencySamples` /
  IComponent path) remains **UNK** — no binding located statically (no strings; virtual-only),
  and the **288-sample figure is a STRUCTURAL estimate (DER), not a host report**. Q-8 stands
  OPEN-negative with the corrected rationale.
- [OBS] m02 ids 12/13 (correction target/gain read-outs, ∓1731.234·ln(corr), the 10^(x/20)
  double-conversion DER) reconfirmed instruction-exact; their polling site remains UNK
  (m02 is virtual-only — zero direct callers in 1.07M instructions).
- [OBS] 73-record host parameter surface + JUCE 30-family table (0x18038EB88, one constructor
  region 0x1801A0CFD+, 1 ref) — both reconfirmed; MIDI/tempo/snapshot: no DSP-record modules
  (negative results stand); state = tagged-chunk snapshot bank (UI-layer semantics UNK).
- [OBS] sht1 ±2400 (res 1) vs sht2..8 ±1 (res 0.001) — record-level reconfirmation of the
  authoring conflict (Q-10: authoring, not DSP).

## 9. OBS/DER/UNK/CONFLICT closure (mandated items)
| item | R26 status |
|---|---|
| record-ring field order | **CLOSED (OBS)** — writer {correction,i4,i5,period} descending / consumer {period,interval,dead,correction}; both sides disassembled this round; dead field (idx+2) write-only; i4/i5 sources identified (port-26 matched-row fields). Old versions retained as history (Appendix F). |
| Minimum Pitch | **CLOSED (OBS)** — m01 id 1 = minp pair (T+0x5F70/74 = v, v+2); 48 choices C0..B3 default 24; low-tolerance regime (0xD2 → 0.03125) stands (R22). |
| sht1 vs sht2..8 | **CLOSED (authoring CONFLICT, OBS)** — re-extracted verbatim; one cent-unit for all 8 ⇒ voices 2–8 ±1¢. |
| m01 id0/id4 | **RESOLVED (NEW)** — id0 → engine+0xA484 (T+0x5F6C; no reader found — UNK field); id4 → engine+0xA440 (T+0x5F28, the adaptive-gate multiplier written by the gatelevel applier with 2/3 / sentinels). |
| m02 12/13 | reconfirmed OBS (data path + constants); polling consumer UNK (virtual-only). |
| stereomixer3 vtable | **CLOSED (NEW)** — mix sub vtable 0x180409E20 = {dtor + dsp_shared base thunks}; composite ctor wraps clip (0x1800D8230) + mix (0x1800D7400) subs. |
| Live Mode 40 | **bounded (DER/UNK)** — +40 to engine delayamtN & dry-path delay comp (maxdelay 51); unit most plausibly samples; conversion site not re-traced. |
| stereo profile differences | CLOSED (OBS) — mimo-only iswitch9; ≥357 index shift; inch/ouch = intra-patch fold. |
| pan law | **REFUTATION (NEW)** — linear 0.5·(1±p)·g refuted; equal-power/trig family OBS-established (atan2f×2 + trig + 1/√2 + 0.5 + 1/nPairs). |
| kernel generator | **UNK (quantified)** — palindrome law + DC/Nyquist profiles + Farrow/LS/windowed-sinc/Gaussian eliminations; no closed-form candidate survives. |
| host latency | Q-8 OPEN-negative; LatencySamples = DebugReport diagnostic (NEW rationale). |
| full contradiction register | Appendix F (all C1–C6 + 41 retractions preserved with resolutions; none reopened; two prior verdicts corrected: "dead serialiser" → DebugReport path; "rows sum 1.0" → ripple ≤1.22e-4). |

## 8-9. Definition-of-Done cross-check
1. Complete signal diagram — Appendix A (3 profiles, module-level, worker trees). ✔
2. Every DSP module located + connected — Appendices A/D (395/394/394 inventory + per-voice chains). ✔
3. Tracker→voice path — §3 + Appendix B (record ring → pages → slots → step). ✔
4. Record/page fields — all identified or proven dead (userscale1..12: declared, unwired). ✔
5. Host parameter surface + VST3 state — §8 + Appendix E. ✔ (state = snapshot bank, UI-layer UNK).
6. Contradictions resolved or precisely bounded — §9 + Appendix F. ✔
7. Every statement traceable — claim ledger + fresh-evidence file + scripts. ✔
8. Original-vs-PitchLab separation — enforced (our-implementation numbers are marked wherever
   they appear; none are audited as target claims).

## 10. Deliverables & verification
- This audit; Appendices A–F (`octavox-work/r26/audit/docs/`); updated
  `OCTAVOX_ORIGINAL_DSP_CHAIN.md` (repo root); evidence matrix (Appendix F + ledger);
  scripts + JSON evidence under `octavox-work/r26/audit/`.
- Analysis integrity: target hash verified before/after every pass; no execution; `main`
  untouched; superseded prior-session SIMD work left as-is (documented, not deleted).
