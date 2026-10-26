# OCTAVOX_TRACKER_DEEP_ANALYSIS

**Document 2 of 8 — pitch tracker / periodicity detector, complete analysis**
Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f6…7f32f1f2`). Static analysis via Ghidra 12.1.4 (REA).
Source of record: `FUN_18013bbc0` (`OX_RESEARCH\analysis\rea3_18013BBC0.txt`, 959 lines) called *only*
from `FUN_1800e2030` L60 — `FUN_18013bbc0(engine+0x4518, input+1, 1)` (**OBS**).
Helpers: `FUN_1800dfe80` (state 7), `FUN_1800e0190` (state 8), `FUN_18013e160` (peak interpolation),
`FUN_18013a790` (table interpolator), `FUN_1800d4710` (delay publisher).
Labels: **OBS** observed, **DER** derived, **UNK** unestablished, **CONFLICT** disagreement.

---

## 1. Interface

```
FUN_18013bbc0(T, samples /* = input ptr + 1 */, flag /* = 1 */)
```

* `T = engine + 0x4518`. `T` is the tracker instance; the engine embeds exactly one tracker (**OBS**).
* `samples` points at the *second* of four input samples pushed per block. Inside, the detector
  consumes `samples[0]` and `samples[2]` → raw input samples `p[1]` and `p[3]` (**OBS**).
* The 3rd argument is read as a boolean-ish "masked/valid" selector: the constant `1` is passed from
  the engine, which sets the mode byte `T+0x5FE8 = 1` at construction; `T+0x5FE8 == 1` selects
  "masked mode" in the record writer (**OBS**, `FUN_1800e0190`).

---

## 2. Internal layout of `T` (OBS, T-relative)

| offset | size | role |
|---|---|---|
| `+0x000` | 243 floats | per-lag **analysis/correlation accumulator** array; write cursor `T+0x5F40` |
| `+0x800` | 512 floats | **detection ring** (mask `0x1FF`), index `T+0x6004` |
| `+0x1000`,`+0x1004` | scratch | correlation window working buffers |
| `+0x10C0`,`+0x10C8` | scratch | 48-sample **history ring** used by the correlator |
| `+0x11C0` | v×243 floats | per-lag **previous value** array (`v` = leak factor, see §5) |
| `+0x5DB0` | 64 floats | **raw decimated ring**, mask `0x3F`, 2 samples written per call |
| `+0x5EB0` | 21 floats | symmetric **low-pass FIR coefficients** (max 21 taps) |
| `+0x5F04` | int | forward tap count (5 / 10 / 20) +1 |
| `+0x5F08` | int | backward tap count (6 / 11 / 21) |
| `+0x5F0C` | int | LP ring index |
| `+0x5F10` | int | **cadence counter** |
| `+0x5F14` | int | **cadence reload** (2 / 4 / 8) |
| `+0x5F18` | float | `0.99182129` energy leak |
| `+0x5F1C` | float | `1/128` energy leak |
| `+0x5F20` | float | `0.99219` peak decay |
| `+0x5F24` | float | running peak |
| `+0x5F28` | float | gate × peak (adaptive gate threshold) |
| `+0x5F2C` | byte | hold flag (`cadence counter >= 0`) |
| `+0x5F30` | int | hold/extend counter, set to `18` / `−1` |
| `+0x5F34` | float | `1/v` leak |
| `+0x5F38` | float | `v*243` limit |
| `+0x5F3C`,`+0x5F48`,`+0x5F4C`,`+0x5F50` | int | scanning indices |
| `+0x5F58` | ptr | `T` itself |
| `+0x5F60` | ptr | `T+4` |
| `+0x5F68` | int | clamp `[2,18]` |
| `+0x5F6C` | int | scan base (set by setparam id 0) |
| `+0x5F70` | int | period base (set by setparam id 1) |
| `+0x5F74` | int | max index (`= x+2` with setparam id 1) |
| `+0x5F78`…`+0x5FC0` | floats | peak / confidence state |
| `+0x5FC8` | ptr | → `engine+0x41E8` (`{period, e_raw, e_filt}`) |
| `+0x5FD0` | float | **gate level**, stored as the **linear power** `10^(g/10)` of the dB parameter (set by setparam id 2, default −55 dB → `3.16e-6`); compared against `[+0x5FC8]+4`, a one-pole **power** envelope (round 8, D6 §10.8) |
| `+0x5FD4` | float | current **period**, initialised `102400.0` |
| `+0x5FD8` | ptr | → `engine+0x98` (**record ring**, float[4096]) |
| `+0x5FE0` | int | record mask `0xFFF` |
| `+0x5FE4` | int | record **write pointer** (= `engine+0xa4fc`) |
| `+0x5FE8` | int | masked-mode flag (1 = on) |
| `+0x5FEC` | float | AGC level |
| `+0x5FF0` | float | AGC gain (≤ 32) |
| `+0x5FF4` | float | `1/128` |
| `+0x5FF8` | int | **state machine state (0..9)** |
| `+0x5FFC` | int | per-state **countdown** |
| `+0x6000` | float | `1/14` |
| `+0x6004` | int | detection-ring index |
| `+0x6008` | float | `2048.0` (fixed scale; see D3) |
| `+0x600C` | float | **sample rate** |
| `+0x6018`,`+0x6020` | ptr | candidate-table pointers (`engine+0x4320`, `+0x4328`) |
| `+0x6028` | ptr | row-table pointer (`engine+0x4370`, stride 6 floats) |
| `+0x6030` | ptr | `engine+0x4388` |
| `+0x6038` | ptr | scale-mask pointer (`engine+0x44A0`, int[12]) |
| `+0x6040` | int | row count (setparam id 14) |
| `+0x6044`,`+0x6048` | float | correction clamps (setparam id 17) |
| `+0x604C`,`+0x6050` | float | band bounds (setparam id 16) |
| `+0x6058` | ptr | row pointer |
| `+0x6060` | float | scale |
| `+0x6064`,`+0x6068` | float | band bounds (zeroed by setparam id 15) |
| `+0x606C` | float | ω |
| `+0x6070` | float | running gain |
| `+0x6074` | float | correction ratio |
| `+0x6078` | int | stability counter |

---

## 3. Front end (states −, every call) (OBS)

Lines 110-153 of the pseudocode run on **every** invocation, before the state switch:

1. **Decimation + LP FIR** — 2 samples per call (the raw `p[1]`, `p[3]`) are pushed into the
   64-float raw ring at `+0x5DB0` (mask `0x3F`). The symmetric FIR (`+0x5EB0`) is applied with
   forward taps `+0x5F04` and backward taps `+0x5F08` (5/6, 10/11 or 20/21 taps per rate class).
   The output is the low-passed, decimated stream.
2. **Adaptive gate** — `if (|x| < |T+0x5F28 · peak|) x = 0;`
   (peak = `+0x5F24`, decays by `+0x5F20 = 0.99219` per call, line 131).
3. **Detection ring write** — the gated value is written to `+0x800 + (idx·4)`, then
   `idx = (idx + 1) & 0x1FF` (lines 147-148). This ring is the ~12 kHz periodicity stream.
4. **Cadence** — `+0x5F10` counts down; when it hits 0 it reloads from `+0x5F14` (line 139) and
   advances the state machine.

### 3.1 Rate configuration (setparam id 5)

`m01` case 5 configures the whole front end from the sample rate (`engine+0x600C`):

* cadence reload `+0x5F14 ∈ {2,4,8}` and tap counts `{5,10,20}` per class;
* chosen so the **detection ring always advances at ≈ 12 kHz** regardless of host rate
  (SR/4 < 50 k, SR/8 ∈ [50 k,100 k), SR/16 ≥ 100 k) — (**DER**, matching the class thresholds in
  the engine ctor at `FUN_1800e0360` L125-133: 0x1000/0x2000/0x4000).
* symmetric LP coefficients written into `+0x5EB0`.

---

## 4. The state machine (OBS, verified against the switch at line 154)

| state | lines | action | exit |
|---|---|---|---|
| **0** | 155-276 | **snapshot & arm.** Sets hold flag `+0x5F2C = (cadence≥0)`, arms `+0x5F30 = −1`, snapshots ≈96 samples into scratch; sets countdown `+0x5FFC = 0x51` (81); → state 1 | 275 |
| **1** | 277-480 | **correlation accumulate.** 48-tap windowed correlation of the current window against the 48-sample history; per lag `j`: `analysis[j] += (x_new − prev[j]) · (1/v)`; uses `+0x5F34 = 1/v` and `+0x5F38 = v·243` | countdown→ state 2 or 3 (479) |
| **2** | 481-569 | **period/peak search** on the analysis array with adaptive thresholds; parabolic peak refinement; sets countdown `7` and → state 3 (566-568) | 568 |
| **3** | 570-761 | **verification / second pass**; countdown `+0x5FFC` → state 3 or 4 (757) | 757 |
| **4** | 762-787 | **candidate accept/reject**; compares against `+0x5F74` (max index) and jumps to `LAB_18013cd39` on overflow | 786 |
| **5** | 788-862 | **period finalisation.** Confidence via `+0x5F80 · 0.203125` and a `5`-count test; → state 6 | 861 |
| **6** | 863-924 | **period scaling & hold.** `×2` if cadence==4, `×4` if cadence==8; overridden by the hold value `+0x5FD4`; clamp `< 2048·50`; store `+0x5FD4`; → state 7 (923) | 923 |
| **7** | 925-928 | **`FUN_1800dfe80`** — ω / stability / hysteresis / correction-ratio computation → state 8 | 927 |
| **8** | 929-932 | **`FUN_1800e0190`** — gated **record writer** (4 floats) → state 9 | 931 |
| **9** | 933-952 | **AGC update** (see §7) → state 0 | 953 |

Default (uninitialised) values come from the engine ctor: `+0x5FF8 = 0`, `+0x5FD4 = 102400.0`,
`+0x5F30 = 18`, `+0x5FFC = 0x51`.

---

## 5. Periodicity representation (OBS)

* The periodicity estimate is produced by **windowed autocorrelation-like differencing**: for each
  lag `j` the accumulator is updated by the *difference* between the new sample and the corresponding
  lagged previous sample, scaled by `1/v`, over a 48-sample window.
* `analysis[]` holds 243 lag values (`v·243` is the array limit, `+0x5F38`), so the searchable period
  range covers 243 decimated lags ≈ 243/12000 s ≈ **20.3 ms ≈ 49 Hz** lower bound
  (**DER**; consistent with a `minpitch` C0..B3 option list and default 24).
* A separate **decaying peak tracker** (`+0x5F20 = 0.99219`) provides the adaptive gate reference,
  and energy estimates are leaked with `0.99182129` (`+0x5F18`) and `1/128` (`+0x5F1C`).

### 5.1 Peak refinement `FUN_18013e160` (OBS, verbatim constants)

```
den = p0 - 2*p1 + p2;
if (|den| < 7.1054e-15) return 0;
f   = ((p0 - p2) * 0.5) / den;
clamp f to [-1, 1];
f *= 8.0;
if (T+0x6008 > 1.0)  return (float)(int)(f * 2048.0);
```

i.e. a **parabolic sub-sample peak interpolator**, with the fixed scale `2048` applied when the
fixed-scale flag is engaged.

### 5.2 Confidence (OBS)

* state 5 uses threshold **`0.203125`** on `+0x5F80` multiplied by the running value, and the test
  `0xd2` (= **210**) on `+0x5F70` for the hold path;
* confidence counters observed: `0.0234375`, `0.4453125`, `0.140625`;
* state 6 uses `×2` (cadence 4) and `×4` (cadence 8) to convert the decimated lag to a full-rate
  period, and the period is clamped to `< 2048·50` (`+0x6008·50`), then doubled once more
  (`+0x5FD4 = 2·period`) — a halving/octave-fold guard.

---

## 6. Record writing — the tracker's only output (OBS)

`FUN_1800e0190` (state 8) writes **4 floats per detection** into the ring `engine+0x98`
(mask `0xFFF`, write pointer `engine+0xa4fc`):

```
{ period , i5 , i4 , correction }
```

* **gating**: only written when the scale mask `int[12] @ engine+0x44a0` has bit
  `((i4 + 1) & 0x1f)` set — i.e. the "scale/ding" enable mask selects which interval candidates may
  be emitted;
* records are written **descending** so the most recent detection is readable at the lowest index
  (**DER** from the consumer arithmetic in `FUN_1800e2030` L163-165);
* the consumer reads `ring[(read_index[v] + engine+0xa4fc) & 0xFFF]`.

### 6.1 Interpretation of the 4 fields (OBS + DER)

| field | observed use |
|---|---|
| `period` | scaled by `2^-11` by the consumer; drives `col = (period-48)/16` into the period table |
| `i5` | written to the rate slot `float[nv] @ +0x4128` (multiplied by `page[0]`) |
| `i4` | selects the interval column: `page[i4 + 2]` becomes the new **target ratio** `page[1]` |
| `correction` | written as `c · 2^-23` into `float[nv] @ +0x4110`; its low nibble is the power-of-two exponent used to rescale `period` |

**DER** — this is an *interval/scale-driven* pitch tracker: it does not merely report a frequency, it
emits a **quantised interval index** (`i4`) plus a **period correction** that the voices use to
retune. That is the mechanism by which Octavox's "diatonic" behaviour is produced.

---

## 7. AGC (state 9) (OBS)

```
e    = meter[1]                                   // engine+0x41E8 + 4  (e_raw)
f    = 0.99;  if (|e| > |AGC_level| + eps) f = 0.7   // fast attack
level = (AGC_level - e) * f + e;   level = min(level, 1.0)
gain  = (1/128) / sqrt(level);  gain = min(gain, 1.0)
AGC_gain = gain * 32;   → T+0x5FF0
```

The AGC normalises the correlator input so that the detector's thresholds are level-independent
(**DER**). `T+0x5FF0` (≤32) is the multiplier applied elsewhere in the front end.

---

## 8. Failure cases and limitations (OBS/DER/UNK)

| case | behaviour | basis |
|---|---|---|
| silence / below gate | gated to 0 → no new peak → hold counter expired → state 6 re-uses `+0x5FD4` (last period) and the `min(period, 2048·50)` guard keeps it bounded | state 6, L913-922 |
| pitch tracking disabled | the *consumer* forces `rate = 1.0` and `i4 = 0` (`engine+0x4510 == 0`), so no detune is applied even if records exist | `FUN_1800e2030` L176-182 |
| period out of range | clamped by `+0x5F74` (max index = minpitch+2) and by `2048·50` | states 4/6 |
| detection too fast | cadence + `0x51` (81) countdown limits the state machine to ≤ 1 full cycle / 81 cadence ticks | state 0, L272 |
| low-frequency input | 243-lag limit ⇒ ~49 Hz floor | DER |
| noisy input | confidence counters (`0xd2`=210, `0.203125`, `5`-count) must be satisfied before acceptance | states 5/6 |
| **UNK** | whether the tracker runs on a copy of the input or on the shared bus pointer, and whether it is reset by the host `reset` call | see D7 Q-6 |

---

## 9. Influence on the voices (OBS, cross-ref D1 §3 S3)

For each voice `v`:

```
idx   = (read_index[v] + engine[0xa4fc]) & 0xFFF
period= ring[idx] * 2^-11          → clamped [48, 480]
i4    = (int) ring[(idx+1) & 0xFFF]      // 0 when tracking disabled
rate  = page[0] * ring[(idx+2) & 0xFFF]  // main rate factor
col   = clamp((period - 48) * 0.0625, 0, 31)
c     = page[col + 12]
period= 2^(c & 0xF) * period + 0.5       // integer period, power-of-two rescale
...
page[1] = page[i4 + 2]                    // new target ratio  (interval table)
```

So the tracker influences the voices through **three** channels:
1. **main rate factor** (`ring[idx+2] × page[0]`) → resampling rate;
2. **interval index `i4`** → `page[i4+2]` → the glide target and hence the transposition interval;
3. **period correction `c`** → `2^(c&0xF)` period scaling (voice retuning) and the crossfade step
   decrement.

Which page column is used is itself selected by the tracker's period (`col`), i.e. the tracker both
*chooses the interval* and *chooses the table row*.

---

## 10. Tracker configuration as actually shipped (OBS, from the `dtonicii9` module record)

The engine's declared configuration — this is the authoritative set of tracker controls and their
values in the shipped patch (`kernel_modules.txt`, module [275]):

| field | value | meaning / mapping |
|---|---|---|
| `pitchtracking` | ← `ptrk` (host, default 1) | master enable; when 0 the consumer forces `rate = 1.0`, `i4 = 0` |
| `minpitch` | ← `minp` (host, default index 24 of 0…47) | lowest accepted period; drives the `+0x5F74` clamp (max index = `minpitch+2`) |
| `tune` | ← `tune` (host, 392…494, default **440**) | reference pitch for the interval/period scale. **Resolves C1**: the engine's `tune` input is a *frequency in Hz* (A4 = 440), so `m02`'s radix `0.003822256433` (D5 §3) is this value expressed differently — the two are **not** contradictory but the exact radix↔Hz relation remains DER |
| `tuning` | `0` (fixed) | additional tuning offset, disabled |
| `quantization` | `0` (fixed) | interval quantisation to the key/scale, disabled at this level |
| `steps` | `12` (fixed) | scale/key table resolution (12 semitones) |
| `hysteresis` | `10` (fixed) | hysteresis of the interval-correction acceptance |
| `correctrate` | `1` (fixed) | correction applied every qualifying frame |
| `mincorrect` | `0` (fixed) | minimum correction magnitude admitted |
| `gatelevel` | `-55` (fixed) | silence gate, dB (converted once to linear power by the applier; compared with a power envelope, D6 §10.8) |
| `intervalglide` | `1` (fixed) | interval changes glide (see D3 §9 / D4 §3.2) |
| `xfadetime` | ← `xfad` (host, 0…100 ms, default **10**) | voice crossfade time on delay re-commit |
| `scale` | ← `scal table` (15-entry table indexed by `scal`) | scale degree set |
| `key` | ← `keys` (17 choices) | key (root) |
| `mod1..mod8` ← `rdm slew 1..8` (full record) | per-voice modulation input (pitch deviation) |
| `shift1..shift8` ← `c_multiply 1..8` | per-voice interval input (see D5 §10) |
| `pcent1..pcent8` ← `sht1..sht8` | per-voice fine shift in cents ("fshift") |
| `modamt1..modamt8` ← `rdep` | per-voice modulation **depth** ("rnd dpth", 0…200) |
| `delayamt1..delayamt8` ← `c_adder8..15` | per-voice delay amount = `dlyN + (live ? 40 : 0)` |
| `userscale1..userscale12` ← `0` | 12 entry user-scale slots; all wired to constant 0 in the shipped patch (`steps = 12`) |
| `maxdelay` | `2400` | internal record max, matching the delay stage |
| `nvoices` | `8` | voice count |

**Scale/key tables (verbatim).** `scal table` (`nentries=15; select=scal`) →
`in1..in15 = 1, 2, 3, 4, 5, 6, 7, 10, 11, 14, 15, 16, 17, 18, 13`; the host `scal` list is
`ionian (maj), dorian, phrygian, lydian, mixolydian, relative minor, locrian, harmonic min, melodic
min, chromatic, whole tone, penta maj, penta min, enigmatic, neapolitan, hungarian` (16 choices,
default 0 = ionian). `c_cmp2`/`c_cmp3` test the `scal table` output against **14** and **15**; `c_or`
ORs the two results and drives the per-voice `int table switch N`. **Round-4 polarity correction:**
the `c_switch` convention is *select 0 ⇒ in1* (proven by `m switch N`: `mswN = off = 0` selects the
`−100` mute branch), so `c_or = 0` selects the **raw** choice index `intN`, and only when the scale
code is 14 (chromatic) or 15 (whole tone) does the switch select `int table N`. The round-2 wording
("in chromatic/whole-tone modes the semitone table is bypassed") had this **inverted** and is
retracted; the polarity is now [DER] from the switch convention.

*Arithmetic note (OBS/UNK):* the choice list has 16 entries but `scal table` only 15, so the last
choice (index 15 = `hungarian`) has **no table entry**; which value the module yields for it is not
visible in the record. (Compare `inst`: 13 reachable choices over 16-entry tables, D3 §14.4.)

**Instrument/pitch-mapping tables (verbatim, full record).** `int table N` (`nentries=29;
select=intN`) → `in1..in29 = 0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,
25,26,28,28`; host choices are `-2 oct, -14th, -13th, -12th, -11th, -10th, -9th, -1 oct, -7th,
-6th, -5th, -4th, -3rd, -2nd, unison, +2nd, +3rd, +4th, +5th, +6th, +7th, +1 oct, +9th, +10th,
+11th, +12th, +13th, +14th, +2 oct` (29 choices, default index 14 = unison, 0-based).
The per-voice interval path is `intN` → `int table N` → `int table switch N` (`select = c_or`) →
`int sub N` (= switch − 14) → `c_multiply N` (= −(switch − 14)); the engine receives `c_multiply N`
as `shiftN`. With the switch convention *select 0 ⇒ in1*, **`c_or = 0` (all scales except chromatic
and whole-tone) passes the raw choice index `intN`**, and `c_or ≠ 0` passes the mapped `int table N`.
Either way the engine's `shiftN` is the *negated, unison-centred* value. (Wiring [OBS]; the select
polarity [DER] from the `c_switch` convention; the musical reading of the table values remains
[UNK].)
