# 07 — Transitions, Reconstruction Mechanisms, and Latency

How the plug-in moves between pitch/interval/delay states without artifacts, what the engine's reconstruction machinery actually does (as established from the traced workers), and the latency/compensation story. Remaining unresolved items are marked [UNK] with the concrete target.

Evidence: records; `dec_dsp_ctor_1800E0360.json` (dsp_t ctor); `dec_dsp_m01_1800E10B0.json` (setparam); `dec2_pitch_process_1800E5160.txt` (`[05]` engine control worker); `dec2_dsp_prepare_1800E2030.txt` (`[01]` per-quantum worker); binder `dec2_fn_1800E18B0.txt`; coefficient-table dump at `0x1803B6170`; `pitch_process_scan.txt`; `method01_annotated.txt` / `method05_annotated.txt`.

---

## 1. Transition vocabulary (all user-visible or wiring-confirmed)

| Mechanism | Where | Parameter/time | Evidence |
|---|---|---|---|
| Engine crossfade time | engine record `xfadetime` (`xfad` module) | 0..100 ms, default **10** | [OBS] record; consumption site not found in `[05]` [UNK] |
| Interval glide (step ratio) | engine record `intervalglide` | per-step ratio `2^(±1/(12·x))` (`+0xA55C/+0xA560`) | [OBS] setparam 17; consumer still [UNK] |
| Interval glide (rate pair) | same record | ids `0x1C/0x1D` = `2^(±y/1200)`, `y = 100/(sr·0.25·t)`, `t = max(0.01, v·0.001)` | [OBS] process flag bit 4 |
| Cents fine-shift ramps | engine record `pcentN` | `2^(±cents/1200)` pairs (`+0xA564/+0xA568`) | [OBS] setparam 16 |
| Exponential rate glide | prepare worker `[01]` | per-voice multipliers at `+0x44E0`/`+0x44F8` | [OBS] `0x1800E2030` |
| Per-voice directory pages | shared ids `0x15`/`0x16` + pulse `0x17` | per-voice stride `0x2C`; page chosen by a per-voice flag | [OBS] `[05]`; page semantics [UNK] |
| Voice restart/reset | shared ids `0x12`, `0x13`, `0x14` | reset when `|id13| ≤ 1/128` → id `0x12` = `1/2048`, id `0x14` int | [OBS] `[05]` |
| Echo delay declick | `moddelay` `declick=1` | delay-time change handling | [OBS] record |
| Far-feedback tap change | `delay_xf`/`delay_xf1` `xfadetime=20` ms | tap = max(`fbkd`, 50) ms | [OBS] records |
| Voice feedback gain smoothing | `c_fltr_a12/23/…/35` | tc = 20 ms | [OBS] records |
| Anti-alias cutoff smoothing | `c_fltr_a14` | tc = 20 ms | [OBS] records |
| Wet/dry smoothing | `c_fltr_a`, `c_fltr_a1..3` | tc = 10 ms | [OBS] records |
| Voice mix (gain/pan) smoothing | `stereomixer3` | tc = 25 ms | [OBS] record |
| Mono-sum wet crossfade | `monoSumWetChans` | tc = 20 ms, amps 0.707/0.707 | [OBS] record |
| Engine-feed mono downmix smoothing | `mix` | tc = 20 ms | [OBS] record |
| Random modulation slew | `rdm slew N` | limits from neighboring voice ×2000 domain | [OBS] records (doc 08 §3) |

[DER] Two-tier scheme: **fast (10–20 ms)** for continuous control values that must not zipper, **slow (20–25 ms)** for spatial/gain crossfades where movement should be inaudible or musical.

## 2. Engine-side transitions (confirmed encodings)

- **Cents domain is first-class.** `pcentN` → ratio pairs `2^(+cents/1200)` / `2^(−cents/1200)` at `dsp_t+0xA564`/`+0xA568` (setparam 16). [OBS]
- **Glide steps are quantized to sub-semitone resolution.** setparam 17 computes `2^(±1/(12·x))` into `+0xA55C`/`+0xA560` — one semitone divided into `x` steps. [OBS] → [DER] `x` encodes the glide resolution/steps.
- **Intervalglide drives an exponential rate pair** (process flag bit 4): shared ids `0x1C`/`0x1D` = `2^(±y/1200)` with `y = 100/(sampleRate·0.25·t)`, `t = max(0.01, value·0.001)`. [OBS formula] → [DER] glide-time-derived rate magnitude (time interpretation not proven beyond the formula).
- **Per-voice directory pages**: each block the process writes a per-voice state record (stride `0x2C`) into shared id `0x15` or `0x16` — page selected by a per-voice flag (`param_1[0x81]` byte) — and publishes a `0/1` pulse on id `0x17` (the decompiler renders the integer `1` as `1.4013e−45`). [OBS] Page semantics [UNK] (likely active/next double-buffering for voice-state changes).
- **Voice restart/reset path**: per voice, id `0x13` is read via `owner[+0xC0]`; when `|v| ≤ 1/128` the process publishes id `0x14` (int) and id `0x12` = `1/2048`. A marker value `−0.02` and step publications (id `0x19` (16 floats), ids `0xE`, `0x1A`, `0x1B` (12 ints `0`/`0x7FFFFF`), `0xF`) surround this path. [OBS]
- **Glide-table build**: a 512-entry loop packs ratio entries as `(int)(8388608/v) & 0xFFFFF0 | exp` (log-packed fixed point); step ratios use `2^(x/2400)` (quarter-semitone at the `ln2/2400 = 0.0002888113` scale). [OBS line-level]
- **`dl` flag resolved**: `dl ≠ 0` = first block of a run → the process re-publishes the **sample-rate object (id 5)**; it is not a declick selector. [OBS]
- **Parameter updates arrive by flag bits** (doc 02 §2.3) and are applied at block boundaries: bit 1 → gate broadcast `0x1800E7960`; bit 2 → id `0x10`; bit 3 → id `0x11`; bit 4 → ids `0x1C/0x1D`; bit 5 → id `0x1E`. Bit 0 (`xfadetime`/`hysteresis` mates) has **no consumer** in `[05]`. [OBS]

## 3. Reconstruction machinery (established)

| Element | Value | Status |
|---|---|---|
| Delay memory ring | `sampleRate · 0.001 · maxdelay` floats = **2.4 s @48 kHz** at `+0x41A0`; write index `+0x41B8`, mask `+0x41C0`; **4 samples / quantum** | [OBS] ctor + `0x1800E2030` |
| Fractional-delay interpolation | **256-phase × 6-tap** coefficient table at `0x1803B6170` (pointer `+0x40F0`; phase mask `0x7F8` at `+0x4098`; mirror-symmetric kernel) | [OBS] binder + table dump |
| Read-rate driver | 4th-order polynomial of the modulated per-voice state (`+0x40D8..+0x40E8`, defaults `0.25/0.34606/0.24393/0.11983/0.03487`), fed by a module input × per-voice gains (`+0x40F8`) × per-voice state (`+0x4128`), **× 64.0** | [OBS] `0x1800E2030` |
| Exponential rate glide | per-voice multipliers `+0x44E0`/`+0x44F8` applied to the rate before integration | [OBS] `0x1800E2030` |
| Read positions | double-buffered vector at `+0x4188`; samples sign-inverted into per-voice work buffers (`+0x40A0`, slice `voice×analysisSize`) | [OBS] `0x1800E2030` |
| Analysis sizes | 4096 (<50 kHz) / 8192 (<100 kHz) / 16384 (else) at `+0x409C` | [OBS] ctor |
| Decimation filters | 11/21/41-tap banks per SR class (setparam case 5) | [OBS] m01 |
| Granular/OLA layer | **none found** in `[01]`/`[05]`; residual possibility in unlocated code (candidates: `xfadetime` consumer, `+0x4518` sub-object) | [UNK] |
| Frame/grain length, hop, window shape | not applicable to the traced read path; [UNK] elsewhere | [UNK] |

[DER] The reconstruction method is now established **at the read level**: voice shifting = **variable-rate reads from the 2.4 s delay line with 256-phase, 6-tap Lagrange interpolation**, rate driven by a quartic polynomial of the modulated per-voice state with exponential glide smoothing. This matches the classic Eventide harmonizer architecture (rate-modulated delay taps); the decimated analysis buffers belong to the **tracker**, not the shifter. What remains unproven is whether any *additional* crossfade layer (per `xfadetime`) samples or blends these reads. One cadence caveat: the worker is invoked from the kernel `[01]` pass (call `0x18011DFBB`); whether `[01]` runs once at setup or per block is not established statically [UNK] — the *method* (rate-modulated interpolated reads) is the batch-2 conclusion either way.

## 4. Latency

| Contribution | Value | Evidence |
|---|---|---|
| Live Mode input compensation | 40 samples on the dry/input path (`delay comp 1/2`, maxdelay 51) | [OBS] records |
| Live Mode engine offset | +40 added to `delayamt1..8` (`c_switch` → `c_adder8..15`) | [OBS] records |
| Interpolated reads | sub-sample (6-tap) — read positions are not integer-quantized | [OBS] `0x1800E2030` |
| Engine internal analysis latency | not resolved; no fixed compensation constant found in the traced workers | [UNK] |
| Host-side latency reporting | not located in this analysis phase | [UNK] (plug-in layer candidate `VoxAudioProcessor`; AAX chunk parsing noted in doc 02 §1) |

[DER] The 40-sample pair (input delayed 40, engine delay amounts +40) is a **relative alignment** measure: in Live Mode the dry path waits 40 samples so the wet path (whose internal offset grows by 40) stays aligned at the same relative position, keeping the wet/dry mix coherent when the user changes Live Mode. The absolute engine latency is elsewhere (tracker/analysis dependent) and is not equal to 40 in general.

## 5. Anti-click/anti-zipper checklist (all wiring-verified)

1. Every gain/level control passes a `c_fltr_a` smoother before use (10–25 ms). [OBS]
2. Delay-time changes: `moddelay declick=1`; far-feedback taps crossfade over 20 ms. [OBS]
3. Switch modules (`iswitch*`, `m switch*`, `fback switch*`) are selected by smoothed/gated values (e.g. voice mute gates both level and feedback synchronously). [OBS]
4. Engine transitions: per-step interval glide ratios, the intervalglide rate pair, and per-voice exponential rate glide in the read driver; `xfade` (default 10 ms) consumption site still unlocated. [OBS]/[UNK]
5. Parameter broadcasts are bit-flag based, so the audio thread never reads moving UI state directly. [OBS]
6. Clip protection on the far-feedback loop via `softclip`; DC blocking via the 1-sample + 5 Hz filter subtraction. [OBS]
7. Voice restart path publishes explicit reset values (id `0x12` = `1/2048`, id `0x14`) instead of jump discontinuities. [OBS]

## 6. Open questions specific to transitions/reconstruction

| # | Question | Status |
|---|---|---|
| R1 | What exactly crossfades over `xfadetime` (grain boundary? read-pointer swap? voice restart?) | open: flag bit 0 has no consumer in `[05]`; candidates: `+0x4518` sub-object, kernel-side consumer [UNK] |
| R2 | Relationship between the glide-step ratio (setparam 17) and the intervalglide rate pair (ids `0x1C/0x1D`) | partially closed: both formulas known; which one drives which stage [UNK] |
| R3 | Role of the 2.4 s ring vs. per-voice `delayamtN` | **closed at read level**: the ring is the voice delay line — variable-rate interpolated reads per voice; `delayamtN` offsets the read position |
| R4 | Analysis window shape/length and hop; filter application site | partially closed: SR classes + sizes + banks confirmed; application site inside the front-end not pinpointed |
| R5 | `dl` flag semantics | **closed**: first-block marker → sample-rate re-publish (id 5) |
| R6 | Engine's true latency and host reporting | open [UNK] |

## 7. Summary

- Transitions are handled at **two levels**: kernel-level smoothing of control values (10–25 ms), and engine-level machinery — glide step ratios, the intervalglide rate pair, and per-voice exponential rate glide in the read driver. `xfadetime` remains a declared-but-unlocated crossfade allowance. [OBS]/[UNK]
- The reconstruction method is established **at the read level**: variable-rate, 256-phase 6-tap Lagrange-interpolated reads from the 2.4 s ring, rate driven by a quartic polynomial of modulated per-voice state. No OLA/granular layer was found in the traced workers; residual unknowns: the `xfadetime` consumer, the per-voice page semantics, and the `+0x4518` tracker sub-object. [OBS]/[DER]/[UNK]
- Latency compensation exists in Live Mode only as a **relative** 40-sample alignment; absolute engine latency and host reporting remain open. [OBS]/[UNK]
