# 06 — Pitch Tracking, Tonality, and Scale Awareness

What the plug-in does to *detect* pitch and tonality, how those products are exposed, and how they gate the interval/scale logic. Items that could not be traced in the shipped code paths are marked [UNK] with the specific question attached.

Evidence: records; `dec_dsp_m02_1800E1520.json` (getparam); `dec_dsp_m01_1800E10B0.json` (setparam); `dec_dsp_ctor_1800E0360.json` (dsp_t ctor); `dec2_base_ctor_1800E2C00.txt` (6 named outputs, instruction-verified); `dec2_pitch_config_1800E3350.txt` (record parser, 13 binding blocks); `dec2_pitch_process_1800E5160.txt` (engine control worker `[05]`); `dec2_dsp_prepare_1800E2030.txt` (per-quantum worker `[01]`); `table_dump.txt` (scale/key tables); `pitch_process_scan.txt`; targeted disassembly.

---

## 1. Control inputs relevant to tracking

All 13 record keys are bound into the engine module as **binding blocks** (stride `0x30` at `+0x10..+0x27F`, layout `{min, max, def, notifyTarget, notifyBit, valuePtr, cachedValue}` — doc 05 §4). Values arrive clamped to `[min,max]`; a notify bit (when present) makes the process publish the derived quantity per block. Bit numbers below are the *global* process flag-word bits.

| Record key | Source module | Value/domain | Block | Notify bit → publish | Meaning status |
|---|---|---|---|---|---|
| `pitchtracking` | `ptrk` (default 1) | 0/1 | `+0x250` | bit 5 → id `0x1E` (int) | [OBS] track-pitch enable; 0/1 preserved through the publish |
| `hysteresis` | constant 10 | — | `+0x70` | bit 0 | [UNK] semantics (units unknown); **bit 0 has no consumer in `[05]`** |
| `correctrate` | constant 1 | — | `+0xA0` | bit 3 → id `0x11` (float) | [UNK]; passive re-publish (no engine-side consumer located) |
| `mincorrect` | constant 0 | — | `+0xD0` | bit 2 → id `0x10` (float) | [UNK]; passive re-publish |
| `intervalglide` | constant 1 | glide time (×0.001 s, floor 10 ms) | `+0x100` | bit 4 → ids `0x1C/0x1D` | [OBS] rate pair `2^(±y/1200)`, `y = 100/(sr·0.25·t)`, `t = max(0.01, v·0.001)` → [DER] glide-time control |
| `tuning` | constant 0 | — | `+0x130` | — | [UNK] (candidate: temperament selector; no consumer seen) |
| `tune` | `tune` (392..494 Hz, default 440) | Hz | `+0x160` | — | [UNK] engine-side use not located; getparam 7 uses the fixed C4 = 261.6256 Hz |
| `quantization` | constant 0 | — | `+0x190` | — | [OBS] **branch selector**: `(int)clamp(v) >= 0` → ids 9/11 feed `tonality`/`timbre`; `< 0` → ids 12/13 do |
| `minpitch` | `minp` (48 choices C0..B3, default 24) | choice index | `+0x1C0` | special | [OBS] the only block registered through a *special* path (4 accessors + appended subscriber; doc 05 §4) |
| `gatelevel` | constant −55 | dB | `+0x1F0` | bit 1 → gate broadcast `0x1800E7960` | [OBS] gate threshold = `10^(v/20)` (used by getparam 12/13) |
| `xfadetime` | `xfad` (0..100 ms, default 10) | ms | `+0x220` | bit 0 (no subscriber) | [UNK] consumption site not found in `[05]` |
| `scale` | `scal table` out | scale code | `+0x10` | — | [OBS] |
| `key` | `keys` out (0..16) | key index | `+0x40` | — | [OBS] |
| `steps` | constant 12 | — | — | record only | [OBS] octave division used by the engine |

[OBS: records + config parser `0x1800E3350`]; interpretation status per row.

## 2. Detection products and their exact encodings (getparam `0x1800E1520`, broadcast in `[05]`)

The engine computes a **period estimate** (fixed point) from which all tracking outputs derive. Formulas verified byte-for-byte against the decompiles:

| Product | id | Formula / constants | Named output slot |
|---|---|---|---|
| pitch (cents) | 7 | `1731.234 · ln( (sr·2048/period) / 261.6256 )`; `−65535` sentinel when the period magnitude ≈ 0 | `pitch` `+0x2F8` |
| frequency | 8 | `f = sr·2048 / period` (0 when `period ≤ 7.1054e−15`) | `freq` `+0x338` |
| period (ms) | — | `1000/f` (0 when f ≈ 0) — computed in `[05]`, not a getparam case | `period` `+0x318` |
| magnitude | 10 | `sqrt( power · 0.9999999 )`; `power` = detection struct `+4` | `amp` `+0x358` |
| voicing ratio | 9 | `A = (int)(cumA·8388607)` (if 0 → `A = (int)(cumB·8388607)`); `B = (int)(cumC·8388607)`; `out = A/(max(A,B)+8)` | `tonality` `+0x378` |
| periodicness | 11 | `sqrt( (p1·8388607 − p2·8388607) / (p1·8388607 + 7.1054e−15) ) · (p0·2048/2048.0) · 1.0646746e−05` | `timbre` `+0x398` |
| gated cents (−) | 12 | `v = (float)F(+0xA58C)`; if `sqrt(power·0.99999988) < 10^(F(+0xA4E8)·0.11513)` → `v = 0`; out = `−1731.234·ln(v)` (0 when `|v| ≤ 7.1e−15`) | replaces id 9 when `(int)clamp(quantization) < 0` |
| gated cents (+) | 13 | same with `F(+0xA588)` and `+1731.234·ln(v)` | replaces id 11 when `(int)clamp(quantization) < 0` |

- Detection struct at `+0xA4E0`: `p0` = period-domain value; `p1`/`p2` = power accumulators (fixed-point 2^23−1 domain); `+4` = magnitude². [OBS]
- Voicing-ratio inputs: `cumA` = `+0xA4B8`, fallback `cumB` = `+0xA4A8`, `cumC` = `+0xA498`. [OBS]
- Constants: `1731.234 = 1200/ln2`; `0.11513 = ln10/20` (dB→amplitude); `1.0646746e−05` fixed scalar. [OBS]
- The process publishes each named output only when the value changed (compared against a cached copy first), then wakes subscribers with `LOCK; *flag |= 1<<bit` (doc 05 §7). [OBS]

[DER] Product *names* above are the mechanical slot pairings (id→slot verified in `[05]`); only "pitch detect"/"tonality detect" exist as literal UI tags. Note the counter-intuitive pairing: the *named* `tonality` slot carries id 9 (integer voicing ratio) and `timbre` carries id 11 (periodic-vs-residual measure) — see §4.

## 3. Analysis front-end (confirmed parts)

- **Sample-rate classes** (setparam case 5): low-pass coefficient banks of **11 / 21 / 41 taps** for `<50 kHz`, `50–100 kHz`, `≥100 kHz`, written into `+0xA3C8..+0xA41B` with `half−1` at `+0xA41C` (5/10/20), `count` at `+0xA420` (6/11/21) and factor 2/4/8 at `+0xA42C` (doc 05 §5). [OBS]
- **Analysis sizes** per class: **4096 / 8192 / 16384** samples at `+0x409C` (ctor reads the sample rate via `owner[+0x30]`); work-buffer vector at `+0x40A0` holds `nvoices × analysisSize` floats with per-voice slice `base + voice×size`; index mask at `+0x40B8`. [OBS]
- **Ring worker** `0x1800E2030` (invoked from `[01]`, call `0x18011DFBB`): ring at `+0x41A0` (write index `+0x41B8`, mask `+0x41C0`), **4 samples per voice per call**; per-voice read positions are double-buffered at `+0x4188`; samples are sign-inverted into the work buffers. [OBS]
- **Gate state** byte at `+0x420` written by the status broadcast. [OBS]
- Six `−1`-initialized qwords at `+0x3B8..+0x3E0` (candidate: per-output subscriber wake words; the six outputs' structs end at `+0x3B7`). [OBS]/[UNK]
- [DER] Together these indicate the detector runs on a **low-pass + decimated, SR-class-normalized buffer** (class-dependent window). The exact detector algorithm (autocorrelation / FFT / other) was **not located** in the traced workers `[01]`/`[05]`; the `+0x4518` sub-object primed by prepare (`FUN_18013BBC0`) is the leading unexamined candidate. [UNK]

## 4. Tonality and timbre outputs

- `tonality` (`+0x378`) and `timbre` (`+0x398`) are two of the engine's six registered named outputs (doc 05 §3.1); UI taps `pitM` (`statement=pitch detect`) and `tonM` (`statement=tonality detect`, kernel slot `0x20A58`) consume the engine's published values. [OBS]
- Mechanical mapping (`[05]`): id 9 → `tonality`; id 11 → `timbre`. With `quantization = 0` (shipped), the gated ids 12/13 never run. [OBS]
- So the *named* `tonality` output carries the integer voicing ratio `A/(max(A,B)+8)`, and *timbre* carries the `(p1−p2)/p1`-shaped periodicness measure. The names are counter-intuitive; the code mapping is authoritative. A runtime tap of `tonM` would corroborate the naming. [OBS]/[DER]/[UNK]
- The tonality-family value is also pushed through the status broadcast `0x1800E7960` (dB→linear conversion with 1.0 floor; indices 2 and 4 via owner `[+0x60]`; sentinels −99/−97/−100). [OBS]

## 5. Scale/key awareness and interval gating

The interval system is **scale-aware on the control side**:

```
scal table (verified literal values, 15 entries: 1,2,3,4,5,6,7,10,11,14,15,16,17,18,13; select = scal)
   ├─ c_cmp2: == 14
   └─ c_cmp3: == 15
        └─ c_or → int-table switch N (per voice): 14/15 → use the raw choice index;
           otherwise → use int table N (diatonic value table)
```

- [OBS] The **engine** receives `scale`, `key` (0..16), `steps = 12` — quantization/scale-mapping exists inside the engine too; the control-side table shapes the user's interval choices before they are sent as `shiftN`.
- [OBS] Engine scale rows: input code `c` selects row `5+c` of the tone table based at `0x1803BB780` (rows 0–4 = tunings 12-EDO/just/variant/Pythagorean/quarter-comma; rows 5..23 = scales; code 12 → row 17 chromatic (99-terminated), 13 → whole tone, 14/15 → pentatonic major/minor, 16 → enigmatic, 17 → neapolitan, 18 → hungarian). Read site: `0x1800E6269`. [OBS table dump]
- [OBS] Key map table at `0x1803BB610`: 17 ints `0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11` — the enharmonic key-spelling → pitch-class map (C, C#, Db, D, D#, Eb, E, F, F#, Gb, G, G#, Ab, A, A#, Bb, B). Ionian marker pattern at `0x1803BB660`; ionian fallback row at `0x1803BB7B0`. [OBS dump]
- [OBS] `keys` presents 17 enharmonic choices (C … B); the engine receives the raw 0..16 index.
- [OBS] `userscale1..12` (all 0 shipped): ≥8 valid entries required, else ionian fallback; user rows use the same 99/100+n marker convention.
- [CONFLICT] The `scal table` literal set `{1..7, 10, 11, 14, 15, 16, 17, 18, 13}` never emits 12 and re-orders the tail; against the 16-entry chooser (`ionian..hungarian`) and the engine row map (12 = chromatic, 13 = whole) the control-side comparators `== 14`/`== 15` cannot simply mean "chromatic"/"whole tone". Candidate resolutions: (a) offset/renumbering between the scal-table domain and the engine code, (b) the comparators discriminate different modes. Settle by `scal` module decompile or a runtime value trace. [CONFLICT]
- [UNK] Exact engine-side *consumption* of `key`/`scale` (which arithmetic turns codes into quantization targets) was not traced to a site.

## 6. Tracking-related UI and metering

| Surface | Module | Statement/tag | Source |
|---|---|---|---|
| `Pitch Track` | `ptrk` | (label above the knob) | record |
| `minpitch: %s` | `minp` | 48 choices | record |
| `tune: %3.0f` | `tune` | Hz | record |
| `xfade   : %3.0f ms` | `xfad` | 0..100 ms, default 10 | record |
| pitch detect meter | `pitM` | `statement=pitch detect` | `dtonicii9-pitch` |
| tonality detect meter | `tonM` | `statement=tonality detect` | `dtonicii9-tonality` |

[OBS] Engine outputs `pitch` and `tonality` are registered outputs of the module (base ctor `0x1800E2C00`; names appear in records as `statement=…` taps and in the process tail `pitM`/`tonM`).

## 7. Tracking-relevant processing seen in the engine interface (smoothed quantities)

- `pcentN` inputs are converted with **`2^(±cents/1200)`** ratio pairs (setparam case 16 → `+0xA564`/`+0xA568`) — cents-domain fine shift. [OBS]
- Glide quantities use **`2^(±1/(12·x))`** per-step ratios (setparam case 17 → `+0xA55C`/`+0xA560`), i.e. one semitone divided into `x` steps — per-step coefficients for gliding between pitch/interval targets (doc 07 §2). [OBS] → [DER]
- `intervalglide` additionally publishes the exponential rate pair ids `0x1C/0x1D` (§1 table; doc 07 §2). [OBS]
- `delayamtN` receives the user delay + (Live Mode) 40 — one of the engine's time-offset inputs. [OBS]

## 8. Open questions specific to tracking

| # | Question | Status |
|---|---|---|
| T1 | Detector algorithm (autocorrelation / FFT / other) | **partially closed**: not in `[01]`/`[05]`; leading candidate the `+0x4518` sub-object (`FUN_18013BBC0`) primed by prepare [UNK] |
| T2 | Role of decimated buffers / ×2×4×8 factors | **mostly closed**: SR-class low-pass banks + analysis sizes + per-voice slices (§3); exact application site inside the front-end not pinpointed |
| T3 | `hysteresis`, `correctrate`, `mincorrect`, `tuning`, `tune`, `quantization` meanings | **partially closed**: quantization = branch selector (§2); pitchtracking = 0/1 → id `0x1E`; gatelevel = dB threshold; intervalglide = glide-time formula; remaining five have mechanics but no located consumer [UNK] |
| T4 | Tonality meaning / thresholds in UI coloring | **partially closed**: id↔slot mapping verified; UI thresholds unverified [UNK] |
| T5 | `key`/`scale` quantization; `userscale` gating | **partially closed**: tables dumped, userscale rule known; engine consumption site unlocated [UNK] |
| T6 | Tracker response time (window + hysteresis) | open [UNK] |
| T7 | `minpitch` numeric effect | open: block mechanics known (special registration), math unlocated [UNK] |
| T8 | Gate applied to audio or only to outputs | open: gate broadcast `0x1800E7960` confirmed; effect site [UNK] |

## 9. Status summary

- The plug-in detects a period, expresses pitch in **cents relative to C4** (−65535 sentinel), derives magnitude, a voicing ratio, and a periodicness measure; publishes them on six named outputs; applies the **−55 dB-class gate level** as a threshold inside the gated-cents cases. [OBS]
- Output naming/mechanics: `pitch`, `freq`, `period`, `amp`, `tonality` (id 9), `timbre` (id 11) — verified at instruction level (base ctor registrations + process publishes). [OBS]
- Scale/key data flows **both** ways: control side (interval-table gating) and engine side (row-based tone tables + 17-entry key map, both dumped). [OBS]
- The detector front-end is **low-pass + SR-class-normalized**; the detector algorithm itself was not located in the traced workers — the `+0x4518` sub-object is the next target. [DER]/[UNK]
