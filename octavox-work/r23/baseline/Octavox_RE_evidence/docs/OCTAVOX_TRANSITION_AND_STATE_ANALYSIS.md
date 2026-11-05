# OCTAVOX_TRANSITION_AND_STATE_ANALYSIS

**Document 4 of 8 — state variables, transitions, smoothing, reset and inter-stage synchronisation**
Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f6…7f32f1f2`). Static analysis, Ghidra 12.1.4 (REA).
Sources: `FUN_1800e2030`, `FUN_1800e0360`, `FUN_1800e8b50`, `FUN_18013bbc0`, `FUN_1800dfe80`,
`FUN_1800e0190`, `FUN_1800e7960`, `FUN_180139650`, `FUN_18013a250`, `FUN_1800c7460`, `FUN_1800d4710`,
`FUN_18013a790`.
Labels: **OBS** observed, **DER** derived, **UNK** unestablished.

---

## 1. Complete state inventory

### 1.1 Audio-path engine state (per engine)

| state | type | lifetime | writer | reader |
|---|---|---|---|---|
| `+0x41a0..` input history ring | float[2^k] | engine | prepare L49-59 | prepare L89-111 |
| `+0x41b8` input write index | int | engine | prepare L304-308 | prepare L51-59 |
| `+0x40a0..` per-voice work ring | float[nv·stride] | engine | prepare L89-111 | prepare L142-159/L241-274 |
| `+0x40c0[nv]` work write pointer | int | engine | prepare L112 | prepare L86 |
| `+0x4188[nv]` read delay `pos_v` | double | engine | prepare L300 | prepare L87 |
| `+0x4140[nv]` integer delay | int | engine | prepare L215/L222/L224 | prepare L210 |
| `+0x4158[nv]` crossfade state | float | engine | L225 (=0), L277 (=ramp) | L132 |
| `+0x4110[nv]` period correction | float | engine | L198 | L207/L258 |
| `+0x4128[nv]` main rate factor | float | engine | L188 | L125 |
| `+0x4208[nv]` voice gain `g_v` | float | engine | L82 | L90/96/102/108 |
| `+0x42f8[nv]` record read index | int | engine | other modules (port 24) | L163 |
| `+0xa4fc` record write index | int | tracker (`FUN_1800e0190`) | — | L164 |
| `+0x98..` record ring | float[4096] | tracker | `FUN_1800e0190` | prepare L163-181 |
| `+0x4098` phase mask | int const 0x7F8 | engine | ctor | L141/L243 |

### 1.2 Tracker state

Full table in `OCTAVOX_TRACKER_DEEP_ANALYSIS.md` §2. The two stateful elements that matter for
transitions are:

* `T+0x5FF8` — **finite state machine**, 0..9;
* `T+0x5FFC` — **per-state countdown** (state 0 arms 0x51 = 81; state 2 arms 7).

### 1.3 Voice/control state

| state | owner | role |
|---|---|---|
| `+0x194` / `+0x198` per voice | `FUN_1800e8b50` | SVF `low` / `band` |
| `+0x190` per voice | `FUN_1800e8b50` | voice level ramp accumulator |
| flags at object `+8` (bit0, bit1) | rdm / c_multiply / c_adder8 / scal | pending-change bits |
| dirty bits in subscriber flag words | control graph | change notification |

---

## 2. Tracker state machine as a transition graph (OBS)

```
        +--------------------------------------------------------------+
        |                                                              v
     [0] arm: hold=cadence>=0, +0x5F30=-1, snapshot, countdown=0x51 --> [1]
                                                                      |
     [1] correlate 48-tap, per-lag analysis[j] += (x-prev[j])*(1/v) --+
            countdown==0 -------------------------------------------> [2] or [3]
     [2] peak/period search, parabolic refine, countdown=7 ----------> [3]
     [3] verify / second pass; countdown ---------------------------> [3] or [4]
     [4] candidate accept/reject (vs +0x5F74 max index) ------------> [5]
     [5] confidence gate (0.203125*+0x5F80, 5-count) --------------> [6]
     [6] period scaling (*2 cadence4, *4 cadence8) -> +0x5FD4,
            clamp < +0x6008*50, +0x5FD4 = 2*period -----------------> [7]
     [7] FUN_1800dfe80 : omega / stability / correction -----------> [8]
     [8] FUN_1800e0190 : gated 4-float record write ---------------> [9]
     [9] AGC update (0.99 / 0.7 attack), +0x5FF0 = gain*32 ---------> [0]
```

**Hold / re-arm transitions (OBS)**

* state 0 sets `+0x5F30 = 0xFFFFFFFF` and `+0x5F2C = (cadence counter >= 0)`; a positive
  `+0x5F30` is the "hold" marker used in state 6.
* state 6: `if (-1 < +0x5F30) period = +0x5FD4` — i.e. while holding, the previously latched period
  is re-emitted instead of a fresh detection.
* state 6 clamp: `if (period < +0x6008 * 50.0) period += period;` then `+0x5FD4 = period`.
* state 5 confidence: `0xd2` (210) test on `+0x5F70`, `0.203125` on `+0x5F80`, `5`-count via
  `iVar25`.

**DER** — this is a *track-and-hold with hysteresis*: acquisition runs the full 0→9 chain, and the
hold flag keeps the last good period alive when confidence decays. The `18` in `+0x5F30` (set by
setparam id 6) is the hold length.

---

## 3. Voice-level transitions

### 3.1 Voice gain ramp (OBS)

`g_v(t) = clamp(g_v(t−1) − target_v, −1, 0)` — a linear ramp: on each block the current gain moves
at most 1.0 (i.e. ≤ 0.25 per sample) toward the target. This is the *voice enable/disable* and
*level* transition; it is refreshed once per 4-sample block.

### 3.2 Interval / ratio glide (OBS, see D3 §9)

`page[0]` moves geometrically toward `page[1]` with **separate up/down multipliers**
(`engine+0x44e0` up, `engine+0x44f8` down). `page[1]` is set from the interval table
(`page[i4 + 2]`) whenever a fresh record is consumed. So a *pitch-interval* change is a two-stage
transition: a discrete target change (`page[1]`) followed by a glide (`page[0]`).

### 3.3 Delay-jump crossfade (OBS, see D3 §7-8)

* **arm**: any successful delay commit sets `+0x4158[v] = 0` (prepare L225).
* **run**: each block, `xf_v = max(xf_v − corr, −1.0)`; the output is
  `(tapA − tapB)·xf_prev + tapA` with `tapA/tapB` exactly `D_v` apart.
* **settle**: `(int)xf_v == −1` ⇒ switch to the settled path and run the record/period update.
* **DER** — the ramp length is `1/corr` blocks; `corr` is `page[col+12]·2^-23`, so the crossfade
  length is *table-driven by the detected period*, i.e. longer crossfades for longer periods.

### 3.4 Period rescaling (OBS)

`period → 2^(c & 0xF) · period + 0.5`, rounded with a floor correction using the sign mask. This is a
**power-of-two octave fold** applied to the tracker period before it drives the table column.

---

## 4. Control-graph (cross-thread) transitions

Each control node has a **two-bit flag word at object `+8`**; bit 0 = channel 0 pending, bit 1 =
channel 1 pending. A node processes a channel only when its bit is set (or when a "force" argument is
non-null). After processing, the bit is atomically cleared with `LOCK CMPXCHG`
(`and mem, ~bit`) (**OBS** in `FUN_180139650` L19-63 and `FUN_18013a250` L20-37).

On a **value change** a node sets a bit in every subscriber's flag word:
`*flag |= 1 << (idx & 0x3F)` under `LOCK` (**OBS** in `FUN_18013a250` L73-82,
`FUN_1800c7460` L151-159, `FUN_18013a790` L70-81).

* **DER** — this is a **lazy, lock-free pull graph**: producers mark consumers dirty; consumers
  recompute only when dirty. It is the mechanism that keeps parameter changes sample-accurate without
  a lock in the audio thread.
* `FUN_1800d4710` (moddelay) is the special case that publishes a *delay length* with
  `(*(*owner + 0x58))(owner, id, ...)` — a flag-bit-gated indexed publish (**OBS**).
* `FUN_180139650` uses `(*(*owner + 0x60))(owner, id, subindex, value)` instead — a different owner
  publish slot, and it emits the **bipolar pair** `value` and `value ^ 0x80000000`.

---

## 5. Synchronisation between tracker and voices

The only tracker→voice channel is the **record ring** `engine+0x98` (**OBS**):

* producer: `FUN_1800e0190` (tracker state 8), write pointer `engine+0xa4fc`, mask `0xFFF`,
  4 floats per accepted detection, gated by the scale mask bit `((i4+1) & 0x1f)`;
* consumer: `FUN_1800e2030` per voice, read pointer `engine+0x42f8[v]` (port 24) plus the write
  pointer, so each voice can lag the writer independently (**DER** — this is how individual voices get
  different interval alignment).

**Transition semantics (OBS)**: because the consumer uses `(read_index[v] + write_index) & 0xFFF`, a
new record appears immediately for every voice; the per-voice offset only biases *which* of the four
floats (period / i5 / i4 / correction) is picked up at a given time. **DER** — this produces the
characteristic per-voice rhythmic offset of the algorithm.

---

## 6. Reset / re-initialisation

* `FUN_1800e7960` (`dec2_clear_state_1800E7960.txt`) is a candidate clear-state routine; it was
  extracted but its callers were not yet resolved. **UNK** — the host event that triggers it.
* Structurally, a click-free reset is available: setting `+0x4158[v] = 0` re-arms a crossfade, and
  `+0x4208[v] = 0` forces a full gain fade-in over ≥ 4 samples. **DER**
* The tracker's state 0 snapshot plus the `0x51` countdown guarantee that a fresh acquisition takes
  at least 81 cadence ticks; there is no "instant" detection path. **OBS**

---

## 7. Transition summary table

| transition | trigger | state written | smoothing | source of truth |
|---|---|---|---|---|
| voice on/off | target gain array (port 19) | `+0x4208` | linear ≤ 0.25/sample | OBS |
| interval change | new record `i4` | `page[1]` then `page[0]` | geometric glide, asym. up/down | OBS |
| delay jump | delay commit | `+0x4140`, `+0x4158 = 0` | 2-tap crossfade, `corr`-stepped | OBS |
| period rescale | record `c` | `+0x4110`, tracker period | power-of-two fold | OBS |
| detection loss | confidence counters | tracker `+0x5FD4` hold | hold length 18 | OBS |
| AGC level | meter `e_raw` | `+0x5FEC`, `+0x5FF0` | one-pole 0.99 / 0.7 attack | OBS |
| pitch tracking off | `+0x4510 == 0` | rate = 1.0, `i4` = 0 | hard reset of interval | OBS |

---

## 8. Module-graph transition mechanisms (OBS, from `kernel_modules.txt`)

The engine's internal transitions (§1–§7) are only one layer. The surrounding graph declares its own
de-click machinery, all of it **fixed in the record** (not exposed to the host):

| mechanism | module(s) | record evidence | effect |
|---|---|---|---|
| **Live-Mode delay compensation** | `delay comp 1` [248], `delay comp 2` [251] | `maxdelay=51; delayamt=live mode delay comp` ; `live mode delay comp.value=40` | while `live` = on, the *dry* path is delayed 40 samples, and the **engine's** per-voice `delayamtN` becomes `c_adderN = dlyN + 40`; the per-voice echo module still gets the raw `dlyN` (**round-4 correction**: `c_adderN` feeds only `dtonicii9.delayamtN`). This keeps the dry signal aligned with the engine's analysis/wet latency |
| **Global feedback delay crossfade** | `delay_xf`, `delay_xf1` [341],[347] | `maxdelay=2400; xfadetime=20; tapdls1=c_minmax-maximum` | any change of the feedback tap (fback dly) is crossfaded over **20 ms**, and the tap is clamped to ≥ 50 (units per record) |
| **Variable-delay de-click inside the engine** | `dtonicii9` voice `moddelay*` | `maxdelay=2000; declick=1` | the per-voice delay is a de-clicking variable delay; `declick=1` enables the same crossfade machinery |
| **Interval glide** | `dtonicii9` | `intervalglide=1` | interval changes glide rather than jump (matches D3 §9) |
| **Parameter de-zipper** | every `c_fltr_a*` | `time_constant=0/10/20` | see D3 §14.1 |
| **Mute / unmute** | `m switch N`, `fback switch N` | `select=mswN`, `in1=-100` | muting is a **level** change through the smoothed `stereomixer3` (tc 25 ms) and the smoothed fb gain (tc 20 ms) — no hard gate |
| **Hard bypass** | `iswitch`, `iswitch1` | `select=bypa` | output selector: bypass is sample-accurate but the `ogan` gain stage is before/after per record; no ramp declared |
| **Tracker re-acquisition** | `dtonicii9` | `hysteresis=10`, `correctrate=1`, `mincorrect=0` | the interval-correction thresholds of §3.2; `correctrate=1` = correct every qualifying frame |
| **Gate** | `dtonicii9` | `gatelevel=-55` | silence gate level in dB; the applier stores `10^(g/10)` (power) and the gate compares it with a one-pole power envelope (D6 §10.8) |

**Resolution of the earlier C4 (control-flow alternation in the engine):** the `xfade`/`settled`
alternation in `FUN_1800e2030` is the *engine-side* implementation of the same "de-clicking variable
delay" that the module system also declares via `declick=1` / `xfadetime`. The engine's own
`xfadetime` input is wired to module `xfad` [21] (`0…100 ms`, default **10**), and the global
`delay_xf` uses a **fixed 20 ms**. These are two different, independently parameterised crossfades.
The engine's `+0x4158` alternation therefore remains the authoritative implementation for the voice
path (no module-level replacement exists). **STATIC**; not runtime-validated.
