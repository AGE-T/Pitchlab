# 08 — Control Surface and Parameter Wiring

How every user-visible control maps to the DSP graph and into the pitch engine. All record values are verbatim from `kernel_modules.json`. UI label strings come from `statement=` record entries (doc 01 §3).

---

## 1. Parameter inventory

### 1.1 Global

| UI statement (as found) | Module | Range / choices | Default | Destination (wiring) |
|---|---|---|---|---|
| `input gain:%3.0f dB` | `igan` | −96..12 dB | 0 | `left in`/`right in` gain |
| `knob: %f` (1..2) | `inch` | 1..2 | 2 (siso/mimo), 1 (miso) | `c_subtract1` → `iswitch4`, `iswitch3`, `iswitch5`, `iswitch2` selects |
| `knob: %f` (1..2) | `ouch` | 1..2 | 2 | `c_subtract3` → `iswitch11` (+`iswitch9` in mimo) selects |
| `input: %s` | `bypa` | normal/bypass | normal | `iswitch`, `iswitch1` (output bypass) |
| `Out Gain:%3.0f dB` | `ogan` | −96..24 dB | 0 | `left out`/`right out` gain |
| `mix:%3.0f %%` | `mmix` | 0..100 (%) | 100 | `c_subtract2` (1−mix) → `c_fltr_a`/`c_fltr_a3` (dry gain); `c_fltr_a1`/`c_fltr_a2` (wet gain) |
| `feedback: %f %` | `mfbk` | 0..100 (%) | 0 | `feedback`/`feedback1` `amp2` (far-feedback loop gain) |
| `fback dly: %f ms` | `fbkd` | 0..5000 ms | 0 | `c_minmax` with 50 → `delay_xf`/`delay_xf1` tap |
| `Live Mode %s` | `live` | off/on | off | `c_switch` (0 : 40-smp comp), `iswitch6`, `iswitch7` selects |
| `rnd dpth: %f` | `rdep` | 0..200 | 50 | engine `modamt1..8`; `c_divide26` (÷2400) → `c_fltr_a36` → `multiply12` (rnd-depth scaler) |
| `key  :   %s` | `keys` | 17 choices (C, C#, Db, D, D#, Eb, E, F, F#, Gb, G, G#, Ab, A, A#, Bb, B) | C | engine `key` |
| `scale: %s` | `scal` | 16 scales (ionian…hungarian) | ionian | `scal table` select; engine `scale` |
| `Pitch Track` | `ptrk` | 0..1 | 1 | engine `pitchtracking` |
| `minpitch: %s` | `minp` | 48 choices C0..B3 | 24 | engine `minpitch` |
| `xfade   : %3.0f ms` | `xfad` | 0..100 ms | 10 | engine `xfadetime` |
| `tune: %3.0f` | `tune` | 392..494 Hz | 440 | engine `tune` |
| `knob: %f` (0..12) | `inst` | 0..12 | 0 | `MaxCut`, `CutFreq`, `CutMultiplier`, `AntiAliasOnOff` table selects |
| (internal) | `live mode delay comp` | value=40 | — | `delay comp 1/2` delayamt; `c_adder8..15` (adds 40 when Live on) |

### 1.2 Per voice (8×)

| UI statement | Module | Range | Default | Destination |
|---|---|---|---|---|
| `voice N: %s` | `mswN` | off/on | off | `m switch N` (level gate), `fback switch N` (feedback gate) |
| `pitchN: %s` | `intN` | 29 intervals (−2 oct … +2 oct) | unison | `int table N` → (`int table switch`) → `int sub` (−14) → `c_multiply N` (×−1) → engine `shiftN` |
| `fshift: %f` | `shtN` | −2400..2400 (cents) | 0 | engine `pcentN`; `c_abs N` → `c_adder N` (+200) → `c_divide(2+N)`: `sht/(\|sht\|+200)` → `c_fltr_a…` (AA/limit weighting) |
| `delay: %f ms` | `dlyN` | 0..1250 ms | 0 | `c_adder(7+N)` = dlyN + live comp (40/0) → engine `delayamtN`; also `moddelay(8−n)`.delayamt |
| `pan: %f` | `panN` | −1..1 | 0 | `stereomixer3.panN` |
| `level: %f dB` | `ognN` | −100..0 dB | −3 | `m switch N` → `stereomixer3.gainN` |
| `fback: %f` | `fbkN` | −100..0 dB | −100 | `fback switch N` → `c_divide(17+N)` (÷20) → `c_power(8+N)` (`10^x` linear) → `c_fltr_a` smoother → `multiply(3+N)` (voice loop gain) |

## 2. Interval → shift conversion chain (per voice)

```
intN (29 choices, default 14 = unison)
  → int table N : 29-entry value table (below)
  → int table switch N : select = c_or; in1 = intN-out; in2 = int table N-out
       (when the scale is chromatic or whole-tone, the raw choice index is used)
  → int sub N : −14
  → c_multiply N : ×(−1)
  → dtonicii9.shiftN
```

**`int table` values (verbatim, choice 1..29)**: `0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,25,26,28,28`

Observations [OBS]:
- The extrema correspond to −2 octaves (0) and +2 octaves (28); 4 octaves span 28 units (7 units/octave).
- The table contains **duplicate pairs** (−13th/−12th→3; −9th/−1 oct→7; −6th/−5th→10; −2nd/unison→14; +3rd/+4th→17; +7th/+1 oct→21; +10th/+11th→24; +14th/+2 oct→28).
- [UNK] The unit of these values (and therefore the exact semitone resolution of `shiftN`) is not resolved. Candidate: a 7-per-octave diatonic grid (1 unit ≈ 1.71 semitones) with name-based rounding; the observed per-octave difference pattern `1,2,0,1,1,2,0` implies a systematic diatonic construction. The engine side will be checked against doc 05/06 when the engine decompiles land (open item).
- [OBS] Sign inversion: `c_multiply N` = `int sub N-out × (−1)`; the engine thus receives the *negated* interval value.

**Scale gating of the interval table**:
- `scal table` (select = `scal`): verbatim 15 entries `1,2,3,4,5,6,7,10,11,14,15,16,17,18,13` — never emits 12; 13 sits last. The chosen scale also feeds engine `scale`. [OBS literal table]
- `c_cmp2` (`scal table == 14`) and `c_cmp3` (`scal table == 15`) combine via `c_or` → bypasses the interval table (raw choice index used). [OBS]
- [CONFLICT] The engine side maps scale code 12 → chromatic row and 13 → whole-tone row (doc 06 §5), so `== 14`/`== 15` cannot both mean chromatic/whole-tone. Candidate resolutions: domain offset/renumbering between the two sides, or the comparators discriminate different modes. Settle via `scal` decompile or a runtime value trace.
- **Key**: `keys` 17 enharmonic choices (C…B) feed engine `key` directly; the engine receives the raw selector value (key map table at `0x1803BB610` — doc 06 §5). [OBS]
- [OBS] Engine side of the chain (batch-2): `shiftN` and `pcentN` arrive as per-voice binding blocks inside the engine module (same block factory as the 13 global record keys; per-voice keys `shift`/`pcent`/`modamt`/`delayamt`/`userscale` — doc 05 §4); `pcentN` is converted to a `2^(±cents/1200)` ratio pair once (setparam 16). The quantization arithmetic that turns `shiftN` units into pitch offsets remains [UNK] (docs 06 §5, 07 §2).

## 3. Random modulation system (per voice, cross-coupled)

```
rdm noise → rdm abs → rdm sh N (sample&hold, newsamp = rdm comp N)
                        → rdm slew N (pslew = nslew = rdm mult N)
                        → rdm comp N (in1 = slew, in2 = sh)   [resample trigger]
                        → rdm a-c N
                        → rdm div N (÷1.5) → rdm add N (+0.5) → rdm mult (N+1) (×2000)
                        → slew limits of voice N+1 (rdm mult wraps 8→1)
dtonicii9.modN   ← rdm slew N-out        (the random signal, per voice)
dtonicii9.modamtN ← rdep-out             (global rnd depth, 0..200)
multiply12       = c_fltr_a36 × rdm slew 1   (rnd depth (rdep/2400, tc=20) × random signal)
add8             = c_fltr_a13 + multiply12    → no named consumer found [UNK]
```

- [OBS] The `rdm mult ×2000` outputs are the positive/negative slew limits of the *next* voice's slew limiter (`rdm slew N` `pslew`/`nslew`), creating a cross-voice randomized modulation-rate structure.
- [OBS] `rdm a-c` shares its process with `LevMetrAtoC` (address `0x1801391F0`); the exact transform is not traced [UNK].
- [UNK] `add8`'s output has no consumer in any record (all three kernels); it may be a vestigial/diagnostic node or consumed by a non-record path.

## 4. Anti-alias system

```
inst (0..12) → MaxCut (dB) / CutFreq (Hz) / CutMultiplier / AntiAliasOnOff tables (16 entries, indexed 1..16)
per voice: eqn (shelf EQ):  freq = CutFreq,  boost = clamp(shiftN_value × CutMultiplier, MaxCut, 0)
global:    c_minmax2 (max of the 8 shift values and 0) → ÷7 → ×(−1) → 2^x → smooth (tc=20) → clamp(0..1) → modiirq.modfreq
           modiirq (2 sections, q=0.7, in = engine feed) → iswitch8 (AntiAliasOnOff) → softclip → engine
```

**Tables (verbatim)**:
| Table | Values (16) |
|---|---|
| `MaxCut` | `0,0,−14,−14,−14,−14,−14,−18,−14,−7,−14,−14,−7,−18,−7,−7` |
| `CutFreq` | `20000,20000,8000,8000,8000,8000,8000,2000,3500,12000,8000,8000,3500,1200,4000,10000` |
| `CutMultiplier` | `0,0,−1,−1,−1,−1,−1,−1.2857,−1,−0.5,−1,−1,−0.5,−1.2857,−0.5,−0.5` |
| `AntiAliasOnOff` | `0,0,1,1,1,1,1,1,1,0,1,1,0,1,0,0` |

[OBS] values; [DER] the EQ-cut/AA-lowpass interpretation is anchored in the wiring above (see also doc 03 §2.5). The `inst` control selects a “virtual instrument” set of AA/EQ parameters; its label is generic (`knob: %f`).

## 5. Live Mode and channel modes

- `live` (off/on): when on, input passes through `delay comp 1/2` (40-sample delay each) before the dry path (`iswitch6/7`) and 40 samples are added to the engine's `delayamt1..8` (`c_switch` → `c_adder8..15`). [OBS] → [DER] this compensates the engine's internal analysis latency in live use (docs 05–07).
- `inch`/`ouch` (1/2): `c_subtract1` = `inch−1` selects mono-left vs stereo paths (`iswitch4`: engine feed = left channel or (L+R)/2; `iswitch3`/`iswitch5`: right path pick); `c_subtract3` = `ouch−1` selects mono wet-sum (`monoSumWetChans`, amps 0.707/0.707, tc=20) vs full stereo wet at `iswitch11`/`iswitch9`. [OBS]

## 6. Meters, LEDs and status

| Module | Statement/tag | Input source |
|---|---|---|
| `pitM` | `statement=pitch detect` (tag `pitM`) | `dtonicii9-pitch` |
| `tonM` | `statement=tonality detect` (tag `tonM`) | `dtonicii9-tonality` |
| `lipD`/`ripD`, `lopD`/`ropD` | tag `ganred:` | `LevLintodB3/4` (in), `LevLintodB/1` (out) — level dB meters |
| `lipL`/`ripL`, `lopL`/`ropL` | LED endpoints (`0x1801366B0`) | meter endpoints |
| `overload`, `overload1..3` | threshold −0.1 (in = `iswitch6`) | clip indicators |
| `c_switch7`/`c_switch8` | — | meter/status switches |

[OBS] Engine pitch/tonality status also travels through the module status broadcast `0x1800E7960` (doc 05 §6).

Note: `tonM` consumes the engine's `tonality` named output, which mechanically carries the id-9 integer voicing ratio (id 11 goes to `timbre`) — the "tonality detect" label is inherited from the original product, not from the id (doc 06 §4). [OBS]/[UNK naming]

## 7. Cross-cutting notes

1. **Internal normalization vs display units.** Several `%`-style controls (`mmix`, `mfbk`) are used directly as linear gains in the graph (e.g., `feedback.amp2 = mfbk-out`; `multiply1 = wet × mmix-derived gain`), while their statements print `%`. [DER] such controls are normalized 0..1 internally and scaled ×100 only for display. dB controls (`ogan`, `fbkN`, `mfbk`… — no: `mfbk` is linear) are converted by `÷20 → 10^x` where applicable (`c_divide18..25 → c_power9..16` for voice `fbkN`).
2. **Several controls pass through 20 ms smoothing** (`c_fltr_a*` instances with `time_constant=20`) — all continuous gains are smoothed before use (docs 07).
3. **Misleading names:** `modfilter`/`moddelay` have their modulation ports wired off (`mod=adc-null`, `modamt=0`) in Octavox; they act as a static 20 Hz HPF and a static echo, respectively. [OBS]
4. **Unused-but-present vocabulary:** `rdm comp`’s `inspread/constval/outspread` are 0; the `antiderivative_antialiasing` string is unreferenced (doc 01 §2).
5. **Record→engine parameter transport.** All 13 engine record keys are bound as 13 fixed binding blocks (`+0x10..+0x27F`, stride `0x30`, layout `{min, max, def, notifyTarget, notifyBit, valuePtr, cachedValue}`); per-voice engine keys (`shift`/`pcent`/`modamt`/`delayamt`/`userscale`) use the same block factory. Notify bits are consumed per block; publishes wake subscribers via atomic flag words (`LOCK; *flag |= 1<<bit`). [OBS] doc 05 §4/§7, doc 06 §1.
