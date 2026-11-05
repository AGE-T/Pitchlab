# 04 — Signal Chain and Execution Order (Canonical)

**Canonical document** for the audio signal chain and the per-block execution order. Evidence base: `method05_annotated.txt` (all devirtualized calls of kernel process `[05]` `0x18011FD60`–`0x180120C12` in execution order), module records (`kernel_modules.json`), and targeted disassembly.

All kernel references below are for `octavox_siso_t` `[05]` unless stated otherwise. Addresses are call-site addresses inside `[05]`.

---

## 1. Signal chain overview

```
adc-in1/in2 (host in)
   │                       ┌────────── mono downmix (mix: 0.5·L + 0.5·R, tc=20 ms) ─────┐
   ├─ left in (gain=igan) ─┤                                                          │
   │     │                 └─ iswitch4: mono→left in, stereo→mix ─→ [engine feed]     │
   │     ├─ delay comp 1 (40 samples, Live Mode) ─→ iswitch6 (dry L, live-switched) ──┤──────────────┐
   │     │                                                                             │              │
   │     │                                          anti-alias: modiirq (2×, q=0.7, cutoff=bound1)    │
   │     │                                                     ↘ iswitch8 (AA on/off)                 │
   │     │                                                        → softclip → dtonicii9 (mono in)    │
   │     │                                                                  │ 8 voice outs           │
   │     │   PER VOICE n (n=1..8):                                          ▼                        │
   │     │     dtonicii9-outN → modfilter (20 Hz HP) → eqn (EQ shelf, CutFreq/CutMultiplier)         │
   │     │        → add ⇄ moddelay (dlyN ms, ≤1250) ⇄ multiply4 (gain = 10^(fbkN dB/20))             │
   │     │        → stereomixer3 (gains: m switch N; pans: panN; tc=25 ms)                           │
   │     │                │                                                                          │
   │     │                ├─ (dry path bypasses everything above; see wet/dry below)                │
   ▼     ▼                ▼                                                                          │
   right in…           [wet L] = feedback1 = stereomixer3-right + mfbk·subtract1-out …              │
   (analogous)         [wet R] = feedback  = stereomixer3-left  + mfbk·subtract-out               │
                            │                                                                       │
                            ▼                                                                       │
                     softclip1/2 → delay_xf (tap = max(fbkd, 50) ms) → jff (1 smp) → highcut (5 Hz)
                            │  subtract = jff − highcut        [DC-blocked tap]                     │
                            └────── amp mfbk (feedback %) ──→ feedback1/feedback (loop)              │
                            │                                                                       │
                            ▼                                                                       │
                     iswitch11/iswitch9: ouch=1→monoSumWetChans (wet L+R)·0.707                     │
                            │                                                                       │
   ┌────────────────────────┴───────────────┬───────────────────────────────────────────────────────┘
   ▼                                        ▼
 multiply3(in=iswitch3: dry R) × (1−mix)   multiply  (in=iswitch6: dry L) × (1−mix)
 multiply2(in=wet R)          × mix        multiply1 (in=wet L)          × mix
   └────────── RWetDryMix ────┘              └────────── LWetDryMix ─────┘
                    │                                        │
              right out (gain=ogan)                    left out (gain=ogan)
                    │                                        │
              iswitch1 (bypa) ─→ host out R          iswitch (bypa) ─→ host out L
```

[All wiring from records; exact records in docs 03/08.]

## 2. Execution phases of `[05]` (address-annotated)

The kernel executes the graph in one pass per audio block. The phases below are contiguous address ranges in `[05]`.

### Phase 0 — Mono detect + parameter-change broadcast
- `0x18011FD77`: `c_subtract1` (slot `0x360`) = `inch − 1`.
- `0x18011FD7C`–`0x18011FDCA` (inline): if `[kernel+0x4D0]` differs from `[kernel+0x4B0]`, latch and **OR a bit into every registered target flag word** from the `{target, bit_index}` vector at `[kernel+0x4B8]…[0x4C0]` (`lock or` at `0x18011FDBA`). This is the parameter-change notification bus (doc 02 §2.3).

### Phase 1 — Per-voice control combine (voices 1..8), `0x18011FDCF`–`0x180120196`
Per voice *n*, in order: `m switch n` (`0x12D0` + stride) → `fback switch n` → `c_adder8..15` (user delay + live comp) → `int table n` → `int table switch n` → `int sub n` → `c_multiply n` (×−1).
- `m switch n`: `{select: mswN; in1: −100; in2: ognN}` (voice mute).
- `fback switch n`: `{select: mswN; in1: −100; in2: fbkN}` (voice feedback level, gated by mute).
- `c_adder8..15`: `dlyN + c_switch` (`c_switch` = live? 40 : 0).
- `int table n` + `int table switch n` + `int sub n` + `c_multiply n`: the interval→shift converter (doc 08 §3).

### Phase 2 — Per-voice feedback gain, `0x18012009B`–`0x18012014F`
`c_divide18..25` (÷20) → `c_power9..16` (`10^(x)`) — one pair per voice; later smoothed per voice by `c_fltr_a12` (voice 1, `0x1801206B3`) and `c_fltr_a23/25/27/29/31/33/35` (voices 2..8, executed in Phase 9 area).

### Phase 3 — Anti-alias ratio weights, `0x180120197`–`0x18012032B`
`c_abs`×8 (`|shtN|`) → `c_adder`×8 (`+200`) → `c_divide3..10` (`shtN / (|shtN|+200)`) → `c_fltr_a13/15/16/…/21` (tc varies).

### Phase 4 — Random modulation subsystem, `0x180120337`–`0x180120597`
`rdm div N` (`÷1.5`) → `rdm add N` (`+0.5`) → `rdm mult N` (`×2000`) — **cross-coupled**: `rdm mult N` reads `rdm add (N−1)`, `rdm mult 1` reads `rdm add 8`. Then `c_subtract3` (`ouch−1`), then per voice `rdm slew N` (slew up/down with `pslew=nslew=rdm mult N`) → `rdm comp N` → `rdm a-c N`.

### Phase 5 — Input stage and input meters, `0x1801205A3`–`0x180120627`
`left in` (gain=igan) → `delay comp 1` (40 smp) → `iswitch6` (`live ? delaycomp : left in`) ; `right in` → `delay comp 2` → `iswitch7` ; `mix` (0.5·L+0.5·R, tc=20) ; `iswitch4` (`inch−1 ? mix : left in`) = **engine feed** ; `iswitch5` (`inch−1 ? iswitch7 : iswitch6`) ; `peak4` (in=iswitch6) → `LevMetrAtoC3`.

### Phase 6 — Anti-alias control and filter, `0x180120633`–`0x1801206DF`
Tables: `MaxCut`, `CutFreq`, `CutMultiplier`, `AntiAliasOnOff` → `c_minmax2` (max of the 8 shift values, floor 0) → `c_divide2` (÷7) → `c_multiply6` (×−1) → `c_power1` (`2^x`) → `c_fltr_a14` → `bound1` (0..1) → `modiirq` (`in=iswitch4`, `n_sections=2`, `q=0.7`) → `iswitch8` → `softclip`.

### Phase 7 — Pitch engine
- `0x1801206EF`: `dtonicii9` at slot `0xBFB8` — engine control worker `0x1800E5160`, called once per block with the `dl` flag (`dl≠0` = first block → sample-rate object id 5 re-publish). `in = softclip-out`. The engine's ring/voice-read worker `0x1800E2030` runs from the kernel `[01]` pass (`0x18011DFBB`, slot `0xC3E8`; 4 samples per voice per call). Docs 05–07.

### Phase 8 — Per-voice DSP
For voice *n* (calls at `0x1801206FF`…`0x18012073E`, then stride `0xB70`):
`modfilterN` (`0x1800E9800`) → `eqn` (`0x1800EBAF0`) → `moddelay(8−n)` (`0x1800D4710`) → `c_multiply` → `c_bound(1+n)` (`0x1800EC600`) → `c_fltr_a`.
- `modfilterN`: `in=dtonicii9-outN`, `freq=20`, `q=1`.
- `eqn`: `in=modfilterN-high`, `freq=CutFreq`, `boost=c_bound(1+n)` (from Phase 6/8 control), `q=0.7`, `type=2`.
- `moddelay(8−n)`: `in=add-out` (loop), `delayamt=dlyN`, `maxdelay=2000`.
- `c_multiply` instance per voice: **8, 9, 11, 13, 15, 17, 19, 21** (voices 1..8; `in1=c_multiply n-out`, `in2=CutMultiplier`).
- `c_bound(1+n)`: clamp to [`MaxCut`, 0] (voice 1 ⇒ `c_bound2` … voice 8 ⇒ `c_bound9`).
- `c_fltr_a` instance in block *n* smooths **voice *n+1*'s** feedback gain: `c_fltr_a23`…`c_fltr_a35` for voices 2..8 (`in=c_power10..16`); voice 1's gain smoother is `c_fltr_a12` (executed in Phase 6 at `0x1801206B3`, `in=c_power9`). Evidence: records `c_fltr_a23: in=c_power10-out` … `c_fltr_a35: in=c_power16-out`; call sites in `method05_annotated.txt`. [OBS]

### Phase 9 — Mixer and far feedback, `0x18012098B`–`0x180120A13`
`stereomixer3` (8 in, gains `m switch N`, pans `panN`, tc=25) → `feedback1` (right) / `feedback` (left) → `softclip2`/`softclip1` → `delay_xf1`/`delay_xf` (tap = `c_minmax` max(fbkd,50) ms, xfadetime=20) → `just for the filter2/…` (1 smp) → `highcut1`/`highcut` (5 Hz) → (`subtract1`/`subtract` = jff − highcut, computed in Phase 10).

### Phase 10 — Wet/dry and outputs, `0x180120A1F`–`0x180120ABF`
`c_subtract2` (`1−mmix`) → `c_fltr_a1` (mix), `c_fltr_a` (1−mix), `c_fltr_a2` (mix), `c_fltr_a3` (1−mix) (tc=10 each) → `iswitch3` → `monoSumWetChans` → `iswitch11` → `left out`/`right out` (gain=ogan) → bypass switches `iswitch2`/`iswitch`/`iswitch1`.

### Phase 11 — Meters and status, `0x180120ACF`–`0x180120C12`
`peak1`, `peak2`, `peak5` → `LevLintodB3/4`, `LevMetrAtoC/1/4`, `LevLintodB/1` → `lipD`/`ripD`/`lopD`/`ropD` (gain-reduction/level display tags) ; `overload1/3/0/2` (in=iswitch6, thresh=−0.1) ; `c_switch7`/`c_switch8` ; `lopL`/`ropL`/`lipL`/`ripL` (LED endpoints) ; `pitM` (`in=dtonicii9-pitch`, statement “pitch detect”) ; tail `jmp` to `tonM` (`in=dtonicii9-tonality`, “tonality detect”, slot `0x20A58`, at `0x180120C03`).

## 3. Audio-path interpretation

1. **Dry path**: `iswitch6` (left, live-compensated) and `iswitch3` (right pick) feed the wet/dry combiner. [OBS]
2. **Engine feed is mono**: `iswitch4` = `left in` when mono input, otherwise `(L+R)/2` smoothed by `mix` (tc=20 ms). The engine (`dtonicii9`) therefore always receives a single channel. [OBS]
3. **Anti-alias**: a 2-section IIR low-pass (`modiirq`, q=0.7) with cutoff controlled by the largest shift (Phase 6), bypassable via the instrument table (`AntiAliasOnOff`), then `softclip` (order=1) into the engine. Per-voice EQ (`eqn`) additionally tilts each voice with per-instrument cut amounts. [OBS/DER]
4. **Engine → 8 voices**: each voice output goes through a 20 Hz HPF and the AA-tilt EQ into its own echo delay that is **inside a feedback loop** with gain `10^(fbkN/20)` (“fback”, per voice, gated by the voice mute). The 8 voice signals (post-echo) are gain+panned by `stereomixer3`. [OBS]
5. **Far feedback**: the stereo mix feeds two independent far-feedback delay lines (`delay_xf`, `fbkd` ms, up to 2400 ms module max/5000 ms control max, cross-mixed via `feedback`/`feedback1` with `mfbk` %), DC-blocked (1-sample + 5 Hz HP subtraction) and soft-clipped. [OBS]
6. **Wet/dry**: crossfade with `mmix` % (`c_fltr_a` smoothers, tc=10 ms), all smoothed. [OBS]
7. **Outputs**: per-channel `ogan`, with `bypa` routing raw input. [OBS]

## 4. Amounts that matter (defaults from records)

| Quantity | Record | Range | Default |
|---|---|---|---|
| input gain | `igan` | −96..12 dB | 0 |
| mix | `mmix` | 0..100 % | 100 |
| out gain | `ogan` | −96..24 dB | 0 |
| feedback % | `mfbk` | 0..100 % | 0 |
| fback dly | `fbkd` | 0..5000 ms | 0 |
| voice level / pan / delay / fback | `ogn/dly/pan/fbk` 1..8 | −100..0 dB / −1..1 / 0..1250 ms / −100..0 dB | −3 / 0 / 0 / −100 |
| Live Mode comp | `live mode delay comp` | — | 40 samples |

## 5. Execution-model observations (important for reading the graph)

1. **"Ghost" modules.** `add` (voice loops), `multiply4..11` (loop gains), `add8` (rnd-depth sum), `multiply..multiply3` (wet/dry) have **no call sites in `[05]`** (byte-level verification: no instruction in `[05]` touches their slot fields; `lea rcx,[rbx+0x16F70]` occurs only in prepare `0x18011DFF2`/mimo prepare and the constructors; `multiply4` slot `0xB5C0`: zero `lea` anywhere). [OBS]
   - Their output buffers *are* primed during `[01]` prepare (4-value ramp/multiply blocks at `0x18011DE90`–`0x18011DF27`), showing the framework treats them as normal stateful nodes at prepare time. [OBS]
   - [DER] Their per-block math must be folded into an adjacent processed node (most plausibly the `moddelay` process `0x1800D4710`, whose input is `add-out`); the exact fold site was not traced. [UNK]
2. **Internal vector quantum = 4 samples.** Prepare primes exactly 4 consecutive values per smoother/multiplier (`[rax]`, `[rax+4]`, `[rax+8]`, `[rax+0xC]`); the engine worker writes 4 samples per voice into its delay/working ring (`+0x41A0`) and work buffers (`0x1800E2030`, batch 2). [OBS] → [DER] audio is processed in 4-sample vectors (SSE lanes).
3. **Ordering semantics.** The call order is topological; control modules always run before the audio modules consuming them in the same block. There is no sample-accurate staging between drill phases; all Phase 1–6 results are used by Phases 7–10 of the same block. [DER]
4. **Flags carry control changes.** Only the parameter-change bits (doc 02 §2.3) and the `dl` flag break the pure data-flow model. [OBS]
5. **Meters are graph nodes.** Meter endpoints (`lipD`, `lopD`, `pitM`, `tonM`, etc.) are modules processed inside `[05]`; their values feed the UI/status layer, not audio. [OBS]

## 6. Cross-references

| Topic | Document |
|---|---|
| Kernel lifecycle, vtable slots, notification bus | doc 02 |
| Module inventory, records, slot map | doc 03 |
| Engine record/interface, setparam/getparam | doc 05 |
| Pitch tracking details | doc 06 |
| Transitions, xfades, delay compensation | doc 07 |
| Parameter→module wiring matrix | doc 08 |
| Claims→evidence | doc 09 |
