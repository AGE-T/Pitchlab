# 03 — Kernel Module Graph and Inventory

Scope: the *static structure* of the plug-in's DSP graph (module instances, their types, and the wiring model). Execution order and audio flow are in doc 04; parameter wiring in doc 08; the pitch engine in docs 05–07.

All data extracted from the kernel record tables (`kernel_modules.json`; raw records in `.data` `0x1804A5000`–`0x1804CD000`) and the kernel method decompiles (doc 01 §3).

---

## 1. Three kernels, one design

| Kernel (in, out) | RTTI class | Modules | Difference |
|---|---|---|---|
| (1, 1) | `octavox_mimo_t` | **395** | has `iswitch9`; `multiply2.in1 = iswitch9-out` |
| (1, 2) | `octavox_miso_t` | **394** | `inch` default = 1 |
| (2, 2) | `octavox_siso_t` | **394** | `inch` default = 2 |

- `octavox_mimo_t` (1-in/1-out) adds a mono-wet summing path for the second internal wet channel:
  - `iswitch9` = `{select: c_subtract3-out (= ouch−1); in1: monoSumWetChans-out; in2: feedback1-out}` [OBS]
  - in `octavox_mimo_t`, `multiply2.in1 = iswitch9-out`; in the other kernels `multiply2.in1 = feedback1-out`. [OBS]
  - `iswitch11` (left channel counterpart, present in all kernels) = `{select: c_subtract3-out; in1: monoSumWetChans-out; in2: feedback-out}`. [OBS]
- No other graph differences exist between the three kernels (name-set difference = `{iswitch9}` only). [OBS]
- [DER] `inch`/`ouch` (1..2 knobs, default 1/2 resp. 2/2) are the effect's **input/output channel mode** controls; `c_subtract1 = inch−1`, `c_subtract3 = ouch−1` are the 0/1 selectors derived from them. Their UI statements are generic (`knob: %f`), so the semantic labeling is derived from wiring, not from text.

## 2. Module type inventory (kernel `octavox_siso_t`, 394 modules)

Counts are exact instance counts from record extraction. Names as found in records (naming is generic: trailing digits distinguish instances).

### 2.1 Pitch engine

| Module | Count | Role anchor |
|---|---|---|
| `dtonicii9` | 1 | The pitch-shifting core (8-voice). Slot `0xBFB8`, module index 275 (`0x113`). Composite: wrapper + `dsp_t` engine at `+0x430`. Batch 2: ring/voice-read worker `0x1800E2030` invoked from `[01]` (call `0x18011DFBB`, slot `0xC3E8`); control worker `0x1800E5160` from `[05]` (`0x1801206EF`, slot `0xBFB8`). See docs 05-07. |

### 2.2 Per-voice DSP blocks (8 voices, stride `0xB70`)

| Module (voice 1) | Cluster |
|---|---|
| `modfilter8` | 20 Hz high-pass per voice (`freq=20`, `q=1`, mod inputs null) [OBS] |
| `eqn` | 1-band EQ tilt per voice: `in=modfilter8-high`, `freq1=CutFreq-out`, `boost1=c_bound2-out`, `q=0.7`, `type=2` [OBS] |
| `add` | loop summer: `in1=eqn-out`, `in2=multiply4-out` [OBS] |
| `moddelay7` | per-voice echo delay: `maxdelay=2000`, `in=add-out`, `delayamt=dly1-out`, `declick=1` [OBS] |
| `c_multiply8` | control: `in1=c_multiply 1-out` (semitone shift), `in2=CutMultiplier-out` [OBS] |
| `c_bound2` | control clamp: `input=c_multiply8-out`, `min=MaxCut-out`, `max=0` [OBS] |
| `c_fltr_a23` | control smoother (tc=20) of `c_power10-out` (voice-2 feedback gain) [OBS] |

### 2.3 Per-voice control (8× each)

`int1..8` (pitch interval selector, 29 choices), `int table 1..8` (29-entry mapping table), `int table switch 1..8`, `int sub 1..8` (−14), `c_multiply 1..8` (×−1), `sht1..8` (fine shift, cents), `rdm sh/slew/comp/a-c/div/add/mult ×8`, `fback switch 1..8`, `m switch 1..8`, `msw1..8` (voice on/off), `ogn1..8` (level dB), `pan1..8`, `dly1..8` (delay ms), `fbk1..8` (fback dB). [OBS: records]

### 2.4 Global control & tables

| Module | Count | Record highlights |
|---|---|---|
| `c_fltr_a` family | 22 | smoothing filters, `time_constant` ∈ {0,10,20,25} ms — the graph's control-rate slew system [OBS] |
| `c_divide` family | 18 | arithmetic (`in1/in2`); includes ÷20 dB→linear heads, ÷2400 rnd scaling, ÷7 AA scaling, ÷1.5 rdm, ÷(x+|x|+200) style ratios |
| `c_multiply` family | 17 | arithmetic; includes −1 sign inversion, ÷-chains, anti-alias power heads |
| `c_adder` family | 16 | arithmetic (+200 bias, +0.5 rdm bias, user delay + live comp) |
| `c_abs` | 8 | `abs(shtN)` → `+200` → ratio for AA/limiter weighting [DER] |
| `c_power` family | 9 | `in1^x` style; used as `10^(dB/20)` and `2^(x)` exponential heads [OBS/DER] |
| `c_bound` | 8 | clamps |
| `c_cmp` | 2 | comparisons vs 14 / 15 (chromatic / whole-tone scale detection) [OBS] |
| `c_or` | 1 | OR of the two comparisons |
| `c_minmax` | 2 | `max(fbkd,50)`; `max(shift1..8,0)` |
| `c_switch` | 3 | live-mode delay comp select; others |
| `c_subtract` | 3 | `inch−1`, `ouch−1`, `1−mmix` |
| `keys`, `scal`, `scal table`, `int table switch` support | — | key/scale tables |
| `ptrk` (Pitch Track), `minp` (minpitch), `tune` (392..494, default 440), `xfad` (xfade ms), `inst` (instrument/Knob 0..12) | 1 each | global pitch controls |
| `rdep` (rnd dpth 0..200), `rdm noise/abs` | 1 each | random modulation source |
| `live`, `live mode delay comp` (value=40) | 1 each | Live Mode |
| `igan` (input gain −96..12), `ogan` (Out Gain −96..24), `inch`, `ouch`, `bypa` (normal/bypass) | 1 each | I/O |
| `mfbk` (feedback %, 0..100), `fbkd` (fback dly ms, 0..5000) | 1 each | far-feedback |
| `mmix` (mix %, 0..100) | 1 | wet/dry |

### 2.5 Anti-alias control system

| Module | Record highlights |
|---|---|
| `inst` | Knob 0..12 (default 0), selects per-instrument presets |
| `MaxCut` | 16-entry table: `0,0,-14,-14,-14,-14,-14,-18,-14,-7,-14,-14,-7,-18,-7,-7` (dB) |
| `CutFreq` | 16-entry table: `20000,20000,8000,8000,8000,8000,8000,2000,3500,12000,8000,8000,3500,1200,4000,10000` (Hz) |
| `CutMultiplier` | 16-entry table: `0,0,-1,-1,-1,-1,-1,-1.2857,-1,-0.5,-1,-1,-0.5,-1.2857,-0.5,-0.5` |
| `AntiAliasOnOff` | switch |
| `modiirq` | `in=iswitch4-out`, `modfreq=bound1-out`, `n_sections=2`, `qfactor=0.7`, `type=0` |
| `iswitch8` | `{select: AntiAliasOnOff-out; in1: iswitch4-out (bypass); in2: modiirq-out}` |
| `c_minmax2` | `max(c_multiply 1..8-out, 0)` → `c_divide2 (÷7)` → `c_multiply6 (×−1)` → `c_power1 (2^x)` → `c_fltr_a14 (tc=20)` → `bound1 (0..1)` → `modiirq.modfreq` |

[OBS] All table values verbatim from records. The interpretation (EQ-tilt cut per instrument, modulated 2-section low-pass cutoff) is [DER] and anchored in the full wiring above.

### 2.6 Output & feedback stage

| Module | Record highlights |
|---|---|
| `stereomixer3` | `ninputs=8`, `time_constant=25`, `in1..8 = add..add7-out`, `gain1..8 = m switch 1..8-out`, `pan1..8 = pan1..8-out` |
| `feedback` / `feedback1` | `time_constant=20`, `in1=stereomixer3-left/right`, `in2=subtract/subtract1-out`, `amp1=1`, `amp2=mfbk-out` |
| `softclip`, `softclip1`, `softclip2` | `select=1`, `drive=0`, `order=1` |
| `delay_xf` / `delay_xf1` | `maxdelay=2400`, `xfadetime=20`, `tapdls1=c_minmax-maximum` (= max(fbkd,50) ms), `gain1=0` |
| `just for the filter`(+`1`), `highcut`(+`1`), `subtract`(+`1`) | 1-sample delay + 5 Hz high-cut + subtraction = DC blocker around the feedback delay taps |
| `c_fltr_a..c_fltr_a3` | wet/dry gain smoothers (tc=10): `mmix` and `1−mmix` |
| `multiply`(dry L), `multiply1`(wet L), `multiply2`(wet R), `multiply3`(dry R) | wet/dry combination before `LWetDryMix`/`RWetDryMix` |
| `LWetDryMix`, `RWetDryMix` | sums; feed `left out`/`right out` (gain = `ogan`… — **CAUTION**: `RWetDryMix.in1=multiply3-out, in2=multiply2-out`; `LWetDryMix.in1=multiply-out, in2=multiply1-out` [OBS]) |
| `monoSumWetChans` | `time_constant=20`, `in1=feedback-out`, `in2=feedback1-out`, `amp1=amp2=0.707` |
| `iswitch3` | `{select: c_subtract1 (inch−1); in1: iswitch6-out; in2: iswitch7-out}` — right-side dry pick |

Cross-reference of the wet/dry sums (verbatim records): `multiply` = `iswitch6-out` (dry left, live-compensated) × `c_fltr_a-out` (1−mix); `multiply1` = `iswitch11-out` (wet left) × `c_fltr_a1-out` (mix); `multiply3` = `iswitch3-out` (dry right pick) × `c_fltr_a3-out` (1−mix); `multiply2` = (`feedback1-out` or `iswitch9-out`) (wet right) × `c_fltr_a2-out` (mix). `LWetDryMix = multiply + multiply1`, `RWetDryMix = multiply3 + multiply2`. All [OBS].

## 3. Wiring model

- Wires are **named references**: `"<module>-out"` (or `-out1`, `-left`, `-right`, `-high`, `-maximum`, `-dbvalue`, `-eq` etc. depending on the source module's ports). [OBS]
- Constants appear directly as numbers (`in2=1`, `amp1=0.5`, `taps…`). [OBS]
- `adc-in1`/`adc-in2` = host audio inputs; `adc-null` = disconnected/all-zero input used for unused modulation ports. [OBS]
- Modules with `processoff=0` are active; multiple records carry `processoff` fields. [OBS]
- The special "table" modules (`MaxCut`, `CutFreq`, `CutMultiplier`, `int table`, `scal table`) map a selector value to an entry (`select=`, `in1..inN`, `nentries=`). [OBS]
- Switch modules (`iswitch*`, `m switch *`, `fback switch *`, `c_switch`) follow `{select; in1; in2}` (or `numins`). [OBS]

## 4. Voice blocks and slot arithmetic

- Voice *i* (1..8) block base = `0x16980 + (i−1)·0xB70`. Module subunits inside a block (offsets from block base): `modfilter +0x0`, `modfilter state +0x108`; `eqn +0x2A8`; `eqn state +0x360`; `add +0x5F0`; `moddelay +0x660`; `moddelay state +0x718`; `c_multiply +0x8A8`; `c_bound +0x968`; `c_fltr_a +0xA30`; block size `0xB70`. [OBS: slot table in `method05_annotated.txt` + prepare offsets]
- Global modules occupy fixed slots outside the voice region, e.g.: `gain` (`0x160`), key/scale (`0x980`/`0xA38`), pitch controls (`0xAF0`–`0x10A8`), voice-1 knobs (`0x1160`–`0x1980`), rdm system (`0x7320`–`, 0x84A8`+), anti-alias (`0xADB0`–`0xB858`), engine (`0xBFB8`), output stage (`0x1C3C0`–`0x1F468`), meters (`0x1F588`–`0x20A58`). [OBS]

## 5. The graph in one paragraph

Host input → input gain → (mono mix or per-channel) → host audio appears twice: once as the *dry* signal feeding the wet/dry mixer, once as the *engine feed* (mono downmix) → anti-alias conditional low-pass → softclip → **dtonicii9** (8-voice diatonic pitch shifter, mono in, 8 out) → per voice: 20 Hz HPF → EQ tilt (anti-alias coloring) → feedback echo loop (`moddelay`, up to 1250 ms user delay + up to 5000 ms far feedback path) → 8-input panner/gain mixer (`stereomixer3`) → per-channel far-feedback delay line (up to 2400 ms, `feedback %` gated, DC-blocked, soft-clipped) → wet/dry mixer → output gain → host output. Meters tap the engine's `pitch`/`tonality` outputs and the signal levels. Details and evidence: doc 04.

## 6. Open structural questions

| Question | Status |
|---|---|
| Where exactly `add`/`multiply4` (voice feedback loop) are evaluated at runtime | [UNK] — see doc 04 §5 |
| Meaning of `ouch` (1..2) beyond mono/stereo wet selection | [UNK]/[DER] |
| `add8` (voice-1-style mod-depth node) has no named consumer | [OBS]/[UNK] — candidate: dead node or engine-side consumption |
| Whether the 4-sample priming implies a 4-sample internal processing quantum everywhere | [DER] — evidence in doc 04 §5 |
