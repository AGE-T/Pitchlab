# 05 — Pitch Engine `dtonicii9`: Interface, Parameters, and Data Structures

The pitch engine is the single module `dtonicii9` (module index 275 = `0x113`) at kernel slot `0xBFB8`. This document covers its external interface, parameter surface, memory layout, and control paths. The internal processing algorithm is covered in docs 06–07.

Evidence: `dec_dsp_factory_180137E20.*`, `dec_dsp_ctor_1800E0360.*`, `dec_dsp_m01_1800E10B0.*`, `dec_dsp_m02_1800E1520.*`, batch-2 normalized decompiles `dec2_base_ctor_1800E2C00.txt` (6 output registrations), `dec2_pitch_config_1800E3350.txt` (record parser + binding blocks), `dec2_fn_1800E18B0.txt` (input/mod binder), `dec2_pitch_process_1800E5160.txt` (block process), `dec2_clear_state_1800E7960.txt` (gate/status publish), `dec2_dsp_prepare_1800E2030.txt` (voice read engine), `dec2_fn_1800E7F70.txt`; records; targeted disassembly; `find_callers.py` scan.

---

## 1. Composite structure and call sites

```
kernel+0xBFB8  dtonicii9 module composite
   +0x000      base engine part        (ctor 0x1800E2C00; 6 named analysis outputs)
   +0x430      dsp_t "engine" object   (ctor 0x1800E0360; shared objects, ring buffer)
```

- Factory `0x180137E20`: `base_ctor(param_1, owner+0x260)` then `dsp_ctor(param_1+0x430, owner, …)`. The composite self-registers under index 275 (`0x113`) with the owner from within the dsp ctor (`owner[+0x18](owner, 0x113, this)`, last statement of `0x1800E0360`). The factory is instantiated from all three kernels: call sites `0x1800F6F1A` (mimo), `0x180110BBA` (miso), `0x180129C7A` (siso). [OBS: factory + `find_callers.py`]
- Config wrapper `0x180137DF0` → engine record parser `0x1800E3350`. Call sites: `0x1800FD423` (mimo), `0x180117063` (miso), `0x180130123` (siso `[02]` configure). [OBS]
- Reset wrapper `0x180137D40` → **`0x1800E7F70` is the `modfilter_t::dsp_t` constructor, NOT an engine clear** (corrected in batch 2). The wrapper also calls the modfilter ctor at `0x180137D2F`. Call sites of the wrapper: `0x180105859` (mimo), `0x18011F429` (siso `[03]` reset). **The engine's own clear/reset entry remains [UNK]** — engine state is (re)initialized by the binder `0x1800E18B0` and by the prepare pass (§7). [OBS: `dec2_fn_1800E7F70.txt`, `find_callers.py`]
- Prepare: engine prepare `0x1800E2030` is called from the kernel prepare pass (`[01]`) at `0x1801043DB` (mimo) and `0x18011DFBB` (siso `[01]`). [OBS]
- Per-block process: `0x1800E5160` (one logical function through `0x1800E7959`), called once per block from `[05]` at `0x180106B2F` (mimo) and `0x1801206EF` (siso `[05]`) with the `dl` flag. [OBS]
- Engine destroy: `0x1800E7DF0` (called from kernel dtor `0x18012179B`). [OBS, from earlier pass]

## 2. Module record (69 entries, verbatim)

```
maxdelay=2400            # ms — delay memory depth
nvoices=8
steps=12                 # scale steps (octave division used by the engine)
in=softclip-out          # mono audio feed (post AA/softclip)
scale=scal table-out     # scale code (1..18 domain)
key=keys-out             # key selector value (0..16)
pitchtracking=ptrk-out   # 0/1
hysteresis=10
correctrate=1
mincorrect=0
intervalglide=1
tuning=0
tune=tune-out            # Hz (392..494, default 440)
quantization=0
minpitch=minp-out        # 48 choices C0..B3
gatelevel=-55            # dB gate
xfadetime=xfad-out       # ms (0..100, default 10)
mod1..mod8=rdm slew 1..8-out
shift1..shift8=c_multiply 1..8-out
pcent1..pcent8=sht1..8-out
modamt1..modamt8=rdep-out
delayamt1..delayamt8=c_adder8..15-out
userscale1..userscale12=0
```

- `maxdelay` and `nvoices` are read by the `dsp_t` constructor (stored `+0x50`/`+0x54`) **and re-read by the binder `0x1800E18B0`** (same offsets; `dec2_fn_1800E18B0.txt` lines 226–261). All other scalar fields are applied by the record parser `0x1800E3350` as binding blocks (§4/§6). `in`/`mod1..8` are resolved by the binder to module outputs (with a "none" module fallback). [OBS]

## 3. Registrations, outputs, and shared objects

### 3.1 Six named analysis outputs (base ctor `0x1800E2C00`)

Registered via `owner[+0x10](owner, moduleId, name, target)`; each target = one *output record* of `0x20` bytes:

| Output | len | name bytes | value slot | subscriber vector (begin/end/cap) |
|---|---|---|---|---|
| `pitch` | 5 | `0x6863746970` | `+0x2F8` | `+0x300/+0x308/+0x310` |
| `period` | 6 | `0x646f69726570` | `+0x318` | `+0x320/+0x328/+0x330` |
| `freq` | 4 | `0x71657266` | `+0x338` | `+0x340/+0x348/+0x350` |
| `amp` | 3 | `0x706d61` | `+0x358` | `+0x360/+0x368/+0x370` |
| `tonality` | 8 | `0x7974696c616e6f74` | `+0x378` | `+0x380/+0x388/+0x390` |
| `timbre` | 6 | `0x6572626d6974` | `+0x398` | `+0x3A0/+0x3A8/+0x3B0` |

[OBS] Registration calls and target arguments re-verified in the batch-2 decompile. `owner[+0x10]` has signature (owner, module-id, name, target); each call is followed by the compiler's inlined small-string (SSO) cleanup, so the name literals are visible verbatim in the decompile.

**Subscriber protocol** [OBS]: subscribing = appending a 16-byte entry `{qword targetFlagWordPtr, u32 bitIndex}` to the output's vector. Publishing = compare-and-store the float, then for every subscriber: `LOCK; *target |= 1 << (bitIndex & 0x3F); UNLOCK` — i.e., the engine wakes downstream modules through the same atomic flag words it reads itself (`+0x3B8`, §6.4). This is the modpp change-notification mechanism.

### 3.2 Named `outN` slots (dsp ctor)

The `dsp_t` ctor also registers its own output array `dsp_t+0x80` (stride `0x18`, count derived from `[+0x10]..[+0x11]`) under generated names `out1..outN` via `owner[+0x8]`. (Array entries are `{ptr flagword, value, …}` style records; exact per-slot semantics [UNK].) [OBS]

### 3.3 Shared objects (dsp ctor, ids 18–30 decimal = `0x12..0x1E` hex)

Created before any configure step; the process publishes most of its per-voice/per-step data through these objects:

| id | Type | Size | Role (process usage) |
|---|---|---|---|
| 3 | `sharedarray_t<float,3>` | 3 floats (`+0x41D8`) | helper array |
| `0x12` | `sharedarray` | `nvoices` floats (`+0x4220`) | per-voice float #1 (published: `0.00048828125` = 1/2048, `−0.02` markers) |
| `0x13` | `sharedarray` | `nvoices` floats (`+0x41F8`, init −1.0f) | per-voice float #2 (read back via "own param" each block) |
| `0x14` | `sharedvector<int>` | `nvoices` (`+0x4248`) | per-voice int (voice-reset index) |
| `0x15` | `sharedarray` | `nvoices × 0x2C` floats (`+0x4270`) | per-voice 44-slot parameter directory (active directory) |
| `0x16` | `sharedarray` | `nvoices × 0x2C` floats (`+0x4298`) | per-voice 44-slot parameter directory (alternate) |
| `0x17` | `sharedvector<int>` | `nvoices` (`+0x42C0`) | per-voice restart/pulse vector |
| `0x18` | (via `0x18013BA60`) | `nvoices` (`+0x42E8`) | per-voice int publish target |
| `0x19` | `sharedarray_t<float,16>` | 16 floats (`+0x4310`) | scale-step table (step ratios) |
| `0x1A` | `sharedarray_t<float,72>` | 72 floats (`+0x4360`) | per-step table (steps × 6) |
| `0x1B` | `sharedarray_t<int,12>` | 12 ints (`+0x4490`) | 12-slot step mask (`0` / `0x7FFFFF`) |
| `0x1C` | `shared_t<float>` | 1 (`+0x44D0`) | interval-glide ratio (+) |
| `0x1D` | `shared_t<float>` | 1 (`+0x44E8`) | interval-glide ratio (−) |
| `0x1E` | `shared_t<int>` | 1 (`+0x4500`) | pitch-tracking enable (0/1) |

[OBS] All rows from `dec_dsp_ctor_1800E0360.txt` (registration calls with sizes/init). The right column cross-references the process publish/read ids in §7.4. Additional fixed pointers: detection struct at `+0xA4E0`, `+0xA4F0 → +0x98`, direct data pointers into the shared arrays at `+0xA530..+0xA570` region. Self-registration: `owner[+0x18](owner, 0x113, this)`.

## 4. Engine memory layout (byte offsets relative to the dsp_t base)

| Offset | Content | Evidence |
|---|---|---|
| `+0x50` | `maxdelay` (2400) | ctor / binder |
| `+0x54` | `nvoices` (8) | ctor / binder |
| `+0x10..+0x27F` | **13 scalar binding blocks**, stride `0x30` (§4.1) | ctor init + config parser |
| `+0x98..+0x4097` | engine state region, zeroed by binder `FUN_1800E18B0` (`memset(+0x98, 0, 0x4000)`) before configure | binder |
| `+0x280..+0x2F7` | zeroed working area | ctor |
| `+0x2F8..+0x3B7` | the 6 named output records (§3.1) | base ctor |
| `+0x3B8` | **atomic flag word** (change bits; §6.4); qword actually spans 6 words `+0x3B8..+0x3E7` used as flag roots | ctor + process |
| `+0x3E8 / +0x3EC` | 0 / 1 (config-time constants) | config parser |
| `+0x3F0 / +0x408` | two `float[nvoices]` vectors (begin/end/cap at `+0x3F0+`): per-voice int mirror + per-voice state flags | config parser (resized to `nvoices`) |
| `+0x420` | gate state (written by `0x1800E7960`) | gate publish |
| `+0x424` | `nvoices` count (loop bound in process) | config parser |
| `+0x428` | `steps` (scale step count, loop bound in process) | config parser |
| `+0x4098` | **fractional-delay phase mask `0x7F8`** (= 2040, i.e. 256 phase buckets × 8) | ctor |
| `+0x409C` | analysis size: 4096 (<50 kHz), 8192 (50–100 kHz), 16384 (≥100 kHz) | ctor |
| `+0x40A0` | work-buffer vector begin: `nvoices × analysisSize` floats; per-voice slice at `begin + voice × analysisSize` | ctor |
| `+0x40B8` | mask = `analysis size − 1` | ctor |
| `+0x40C0` | sub-object (init `0x18013BAF0`) | ctor |
| `+0x40D8..+0x40E8` | **rate polynomial coefficients** 0..4, (re)initialized to `0.25, 0.34606, 0.24393, 0.11983, 0.03487` at every binder run (4th-order polynomial in the per-voice rate driver) | binder |
| `+0x40F0` | pointer to the **256-phase, 6-tap fractional-delay coefficient table** `0x1803B6170` (set at every binder run; 256 × 6 floats × 4 B = 6144 B; mirror-symmetric kernel, entry 255 = reversed entry 0) | binder + direct binary dump |
| `+0x40F8/+0x4110/+0x4128/+0x4158` | `float[nvoices]` vectors (init: zero / `0.0625` / zero / zero) | ctor |
| `+0x4170` | sub-object (init `0x18013BAF0`) | ctor |
| `+0x4188` | double read positions (init `0x18013D560`) | ctor |
| `+0x41A0` | delay ring buffer (`sampleRate × 0.001 × maxdelay` floats ≈ 115,200 = 2.4 s @48 kHz) | ctor |
| `+0x41B8 / +0x41C0` | ring write index / mask | ctor + prepare |
| `+0x41D8..+0x4517` | shared-object objects (§3.3) | ctor |
| `+0x4518` | sub-object serviced by `0x18013BBC0(ptr,1)` in prepare (candidate: tracker sub-system) [UNK] | prepare |
| `+0xA3C8..+0xA41B` | decimation filter table (21 floats; per class only the first 6/11/21 are written, the rest zeroed) | setparam case 5 |
| `+0xA41C/+0xA420/+0xA42C` | half-length−1 / count / decimation factor of current SR class | setparam case 5 |
| `+0xA440`, `+0xA448`, `+0xA44C`, `+0xA450` | setparam-4 and setparam-6 storage (`0x12`, `(int)x·0xF3`, `1/x`) | setparam |
| `+0xA484`, `+0xA488`, `+0xA48C` | setparam 0/1 storage; `(int)x + 2` | setparam |
| `+0xA4E0` | detection-result struct pointer (period `+0`, power `+4`, P0/P1/P2 fields) | ctor + getparam |
| `+0xA4E8` | gate threshold in dB (setparam 2 = `gatelevel`) | setparam |
| `+0xA498/+0xA4A8/+0xA4B8` | id-9 accumulators (B / A-alt / A) | ctor + getparam |
| `+0xA520` | constant 2048.0 | ctor |
| `+0xA524` | sample rate | setparam case 5 |
| `+0xA558` | setparam-14 scalar | setparam |
| `+0xA55C/+0xA560` | glide step ratios `2^(±(ln2/12)/x)` (setparam 0x11 = 17) | setparam |
| `+0xA564/+0xA568` | cents→ratio pair `2^(±x/1200)` (setparam 0x10 = 16) | setparam |
| `+0xA57C` | 8 bytes cleared by setparam 15 | setparam |
| `+0xA588/+0xA58C` | id-13 / id-12 gated-value sources (init 1.0) | ctor + getparam |

### 4.1 The 13 scalar binding blocks

Initialized by the base ctor as `{min = −32768.0f, max = 32767.0f, default = 1.0f, 0}` and filled by the record parser `0x1800E3350`:

```
block = { f32 min @+0x00, f32 max @+0x04, f32 def @+0x08, pad,
          qword notifyTarget @+0x10,  u32 notifyBit @+0x18,
          f32* valuePtr   @+0x20,  f32 cachedValue @+0x28 }        (stride 0x30)
```

| Block base | Record key | notify bit | Read in process |
|---|---|---|---|
| `+0x10` | `scale` | — | scale machinery (§7.3) |
| `+0x40` | `key` | — | scale machinery |
| `+0x70` | `hysteresis` | 0 | not consumed in process [UNK] |
| `+0xA0` | `correctrate` | 3 | bit 3 → publish id 0x11 |
| `+0xD0` | `mincorrect` | 2 | bit 2 → publish id 0x10 |
| `+0x100` | `intervalglide` | 4 | bit 4 → publish ids 0x1C/0x1D |
| `+0x130` | `tuning` | — | [UNK] |
| `+0x160` | `tune` | — | [UNK] (C4 reference is hard-coded 261.6256 in getparam) |
| `+0x190` | `quantization` | — | output-broadcast selector (§7.2) |
| `+0x1C0` | `minpitch` | — | [UNK]; special-cased in configure (4 accessors + `FUN_1800C4930` subscriber append) |
| `+0x1F0` | `gatelevel` | 1 | bit 1 → `0x1800E7960` gate publish |
| `+0x220` | `xfadetime` | 0 | [UNK] (consumed by the read engine in prepare, doc 07) |
| `+0x250` | `pitchtracking` | 5 | bit 5 → publish id 0x1E (0/1) |

[OBS] base/keys/bit indices read from the parser (`dec2_pitch_config_1800E3350.txt`, all 13 `FUN_1800C55A0` call sites) and cross-checked against every flag-bit section of the process. The per-voice keys `shift`, `pcent`, `modamt`, `delayamt`, `userscale` are created per voice with the same block factory (`FUN_1800C4930` loops, one block per voice); `mod1..8` are 16-byte entries at `+0x68..+0x70` resolved by the binder. [OBS]

## 5. Sample-rate classes and coefficient tables (setparam case 5)

When the sample rate is set, the engine selects a class, fills the single 21-float coefficient array at `+0xA3C8` and records the class metrics [OBS: `dec_dsp_m01_1800E10B0.*`]:

| SR class | Factor (`+0xA42C`) | Half-length−1 (`+0xA41C`) | Count (`+0xA420`) | Tap length (2·half−1) |
|---|---|---|---|---|
| < 50 kHz | 2 | 5 | 6 | 11 |
| < 100 kHz | 4 | 10 | 11 | 21 |
| else | 8 | 20 | 21 | 41 |

Coefficient sets (verbatim bit patterns, published as floats):

- **11-tap class**: `0x3c7faa00, 0x3d44c140, 0x3db18f00, 0x3dfaf420, 0x3e170420, 0x3e203ab8` (= 0.0156045, 0.0480359, 0.0866985, 0.1225359, 0.1474767, 0.1564740)
- **21-tap class**: `0x3bffaa00, 0x3c795520, 0x3cc4c140, 0x3d09811b, 0x3d318f00, 0x3d5829a9, 0x3d7af420, 0x3d8c060d, 0x3d970420, 0x3d9de343, 0x3da03ab8` (= 0.0078022, 0.0152180, 0.0240179, 0.0335704, 0.0433493, 0.0527741, 0.0612680, 0.0683709, 0.0737383, 0.0770936, 0.0782370)
- **41-tap class**: `0x3b7faa00, 0x3bb8f340, 0x3bf95520, 0x3c1fa8b6, 0x3c44c140, 0x3c6b60b9, 0x3c89811b, 0x3c9d8ec0, 0x3cb18f00, 0x3cc53279, 0x3cd829a9, 0x3cea2a89, 0x3cfaf420, 0x3d05278e, 0x3d0c060d, 0x3d120099, 0x3d170420, 0x3d1aff56, 0x3d1de343, 0x3d1fa436, 0x3d203ab8` (= 0.0039011, 0.0056442, 0.0076090, 0.0097448, 0.0120090, 0.0143663, 0.0167852, 0.0192331, 0.0216746, 0.0240719, 0.0263871, 0.0285847, 0.0306340, 0.0325084, 0.0341855, 0.0356451, 0.0368692, 0.0378412, 0.0385468, 0.0389750, 0.0391185)

[OBS] values; [DER] they form symmetric low-pass windows with roughly constant time-length across classes and an equivalent low-pass corner in the ~1.7–1.9 kHz region under a 44.1/96/192 kHz interpretation. The **factor** (2/4/8) indicates analysis-stage decimation. [DER]

## 6. Parameter interface (`dsp_t` vtable)

### 6.1 `setparam(id, float*)` — `0x1800E10B0` (vtable slot `[01]`)

Slot identity [OBS]: RTTI `.?AUdsp_t@dtonicii_t@modules@modpp@@` has COL `0x180435D08` and
vtable `0x180409F60` = `0x1800E7DB0`, **`0x1800E10B0`**, **`0x1800E1520`**, `0x1800C5750`,
`0x1800C5770`, `0x1800C5790`, `0x1800C57B0`; the ctor writes it at `0x1800E03B3`/`0x1800E03BA`.
The dispatch table (18 entries, `cmp edx,0x11`) is at `0x1800E14D0`; index 2 → `0x1800E110F`.
Out-of-range ids and ids 3/7–13 go to `0x1800E14B3`, which calls `[vptr+0x18]` = slot 3
(`0x1800C5750`, a base-class thunk to `modules[edx]->slot1(id, value*)`).

| id | Action [OBS] |
|---|---|
| 0 | store at `+0xA484` |
| 1 | store at `+0xA488`; `+0xA48C = (int)x + 2` |
| 2 | store at `+0xA4E8` — the gate threshold in dB used by getparam 12/13 (i.e. `gatelevel`) [DER] |
| 4 | store at `+0xA440` |
| 5 | **sample-rate class setup** (§5); sample rate stored at `+0xA524` |
| 6 | `+0xA448 = 0x12`; `+0xA450 = (int)x · 0xF3` (243); `+0xA44C = 1/x` |
| 14 (0xE) | store at `+0xA558` |
| 15 (0xF) | zero 8 bytes at `+0xA57C` |
| 16 (0x10) | `2^(±x/1200)` → `+0xA564` / `+0xA568` (cents ratios) |
| 17 (0x11) | `2^(±(ln2/12)/x)` → `+0xA55C` / `+0xA560` (per-step glide ratios) |
| default | forward to `owner[+0x18]` |

### 6.2 `getparam(id, float*)` — `0x1800E1520` (vtable slot `[02]`)

| id | Returns [OBS] |
|---|---|
| 7 | **pitch in cents relative to C4**: `1731.234·ln( (sr·2048/period)·(1/261.6256) )`; `−65535` when period ≤ 7.1e−15 |
| 8 | **frequency in Hz**: `sr·2048 / period` (0 when period ≈ 0) |
| 9 | `A / (max(A,B) + 8)` on 2^23-scaled ints; `A` = `+0xA4B8` (fallback `+0xA4A8` if zero), `B` = `+0xA498` |
| 10 | `sqrt(power · 0.9999999)` (magnitude) |
| 11 | `sqrt((P1·2^23 − P2·2^23)/(P1·2^23 + ε)) · (P0·2048/2048) · 1.0646746e−05` (P0/P1/P2 = detection struct) |
| 12 | gated value: `v = F(+0xA58C)`; magnitude `sqrt(power·0.99999988)`; threshold `10^(F(+0xA4E8)·ln10/20)`; if magnitude < threshold → 0; else `−1731.234·ln(v)` |
| 13 | same as 12 with `v = F(+0xA588)` and `+1731.234·ln(v)` |
| default | forward to `owner[+0x20]` |

[OBS] formulas/constants as decompiled. Which products reach which named output is resolved in §7.2 — note that the **named output `tonality` carries id 9** and the **named output `timbre` carries id 11** in normal operation; ids 12/13 feed the same two outputs only in the fallback branch.

### 6.3 Status broadcast — `0x1800E7960`

- Clamp → `×ln(10)/10 (0.23025850929)` → `10^x` → floor 1.0 → push via `owner[+0x60]` with indices 2 and 4; sentinel values −99/−97/−100 dB handled; gate state byte written at `+0x420`. [OBS]
- [DER] This is the engine dB→linear status publication used by `pitM`/`tonM` and UI meters; index 2 carries the gate level (linear), index 4 the gate state.

### 6.4 The engine flag word `+0x3B8`

The process pops bits with a LOCK/CAS loop (read → clear → retry). Verified bit assignments:

| Bit | Source block | Action in process |
|---|---|---|
| 0 | `hysteresis` / `xfadetime` shares 0 | not consumed in `0x1800E5160` [UNK] |
| 1 | `gatelevel` | `0x1800E7960` gate publish |
| 2 | `mincorrect` | publish id 0x10 (float) |
| 3 | `correctrate` | publish id 0x11 (float) |
| 4 | `intervalglide` | publish ids 0x1C/0x1D (glide ratio pair) |
| 5 | `pitchtracking` | publish id 0x1E (int 0/1) |

[OBS: config parser bit indices + process sections]

## 7. Engine block process — `0x1800E5160`

### 7.1 Entry

- Signature `(dsp_t*, dl_flag)`; called once per block. If `dl_flag != 0` (first block of a processing stretch), the engine re-publishes its sample rate: `owner[+8]()` result → `owner[+0x60](…, 5, sr)`. [OBS] → `dl` semantics partially resolved: "first-block refresh", not a declick selector [R5 in doc 07 updated].
- Then the flag-pop sections (§6.4) run, followed by the output broadcast and the per-voice loop.

### 7.2 Output broadcast (every block)

Reads the detection products **through its own vtable** (`owner[+0x80]` = "read module output by id"): id 7 → slot `pitch` (+0x2F8), id 8 → `freq` (+0x338) (and its reciprocal `1000/freq` ms → the `period` slot +0x318), id 10 → `amp` (+0x358); id 9 and id 11 feed `tonality`/`timbre` (selector below); ids 12/13 are the gated fallbacks. Each store only wakes subscribers when the value actually changed. [OBS]

**Tonality/timbre selector**: the clamped value of the `quantization` binding (block `+0x190`, default 0) is read; if `(int)value < 0` → `tonality` output ← getparam 12 and `timbre` ← getparam 13 (gated variants); otherwise `tonality` ← getparam 9 and `timbre` ← getparam 11. With the shipped record (`quantization=0`) the normal branch is always taken. [OBS] — this resolves the earlier tonality/timbre ambiguity: both slots are written, and the mapping is `tonality` = id 9, `timbre` = id 11.

### 7.3 Per-voice loop (bound `+0x424` = nvoices) and step loop

- Per voice: read its own per-voice parameter via `owner[+0xC0]` (id 0x13, voice index); when `|v| ≤ 1/128`, a reset path runs: cached per-voice value read (`vector +0x3F0`), publish `owner[+0x98]` id 0x14 (int) and `owner[+0xA0]` id 0x12 = `0.00048828125` (1/2048). [OBS]
- Step machinery (bound `+0x428` = steps): builds the scale-step tables, using `2^(x/2400)`-domain factors (`ln2/2400 = 0.0002888113`); publishes to ids 0x19 (16 entries), 0xE (count), 0x1A (per-step table), 0x1B (12 entries: `0` or `0x7FFFFF`), 0xF. [OBS]
- Per-voice **44-slot directory**: `slotbase = voice × 0x2C`; the directory id is `0x16 − (restartFlag[voice] != 0)` → so id **0x15** (flag 0) or **0x16** (flag 1); slots: `+0` clamped value, `+1` aux, `+2..+9` eight floats, `+10` clamped rate, `+11` `iVar+15`, `+12..+43` 32 int-valued entries. [OBS; slot semantics refined in doc 07]
- **Restart pulse**: if `restartFlag[voice] == 0` → publish (id 0x17, int `1`; decompiler renders the float bit pattern `1.4013e-45`) and set flag 1; else publish int `0` and clear. The same flag also selects the directory id above. [OBS] — likely edge/restart signalling to the delay-read subsystem [DER].
- Additional publishes: id 0x18 via `owner[+0x98]` (per-voice int) and id 0x12 = `−0.02` when the per-voice mirrored int changed (`vector +0x3F0`). [OBS]

### 7.4 Bus/publish id summary (engine → owner)

| Id | Direction / API | Payload | Source section |
|---|---|---|---|
| 2, 4 | `owner[+0x60]` | gate level (linear), gate state | gate publish 0x1800E7960 |
| 5 | `owner[+0x60]` | sample rate | configure tail + first block (`dl`) |
| 0xE, 0xF | `owner[+0x58]` | step counts | step machinery |
| 0x10 | `owner[+0x60]` | clamped `mincorrect` | bit 2 |
| 0x11 | `owner[+0x60]` | clamped `correctrate` | bit 3 |
| 0x12 | `owner[+0xA0]` | per-voice float (markers 1/2048, −0.02) | per-voice loop |
| 0x13 | `owner[+0xC0]` (read) | per-voice "own param" | per-voice loop |
| 0x14 | `owner[+0x98]` | per-voice int (reset) | per-voice loop |
| 0x15 / 0x16 | `owner[+0xA0]` | 44 slots per voice (directory A/B) | per-voice loop |
| 0x17 | `owner[+0x98]` | restart pulse (1/0) per voice | per-voice loop |
| 0x18 | `owner[+0x98]` | per-voice int | step/voice loop |
| 0x19 | `owner[+0xA0]` | 16-entry scale-step table | step machinery |
| 0x1A | `owner[+0xA0]` | per-step table | step machinery |
| 0x1B | `owner[+0x98]` | 12-entry step mask (0 / 0x7FFFFF) | step machinery |
| 0x1C / 0x1D | `owner[+0x60]` | interval-glide ratio pair `2^(±y/1200)`, `y = 100/(sr·0.25·t)`, `t = max(0.01, intervalglide·0.001)` | bit 4 |
| 0x1E | `owner[+0x58]` | pitch-tracking enable (0/1) | bit 5 |

Ids match the shared-object ids registered in §3.3 (0x12–0x1E) — the process writes into those objects. [OBS/DER]

### 7.5 Call profile (unchanged from earlier pass)

Direct callees: `0x180351F50` (pow/exp), `0x18017EB60` ×3, `0x18017D2E0` ×2, `0x1800E7960`, `0x1800E7B80`; ~34 indirect dispatches through the owner interface (`+8, +0x58, +0x60, +0x80, +0x98, +0xA0, +0xC0`). [OBS: `pitch_process_scan.txt`] — the engine is written entirely against the modpp owner/shared-object API.

## 8. Constants of interest (engine region)

| Value | Meaning (derived) | Evidence |
|---|---|---|
| `1200.0` | cents per octave | ctor/m01/m02 |
| `1731.234049` | 1200/ln2 (cents per ln) | m02 |
| `0.00057762265` | ln2/1200 | m01/m02 |
| `0.0002888113` | ln2/2400 (quarter-octave step domain) | process |
| `0.057762265` | ln2/12 (semitone) | m01 |
| `0.1151293` | ln10/20 (dB→ln) | m02 |
| `0.23025850929` | ln10/10 (dB→ln for gate publish) | 0x1800E7960 |
| `261.6256` | C4 frequency | m02 |
| `2048.0` | fixed scale for period/2^11 | ctor/m02 |
| `8388607.0` | 2^23−1 fixed scale | m02 |
| `1.4013e-45` | int `1` as float (restart pulse) | process |
| `0.0078125` = 1/128, `0.00048828125` = 1/2048, `−0.02` | per-voice reset markers | process |
| `0x7FFFFF` (8388607) | step mask "all bits" | process (id 0x1B) |
| `0x7F8` (2040) | fractional-delay phase mask → 256 phases | ctor `+0x4098` |
| `0.25, 0.34606, 0.24393, 0.11983, 0.03487` | default rate-polynomial coefficients | binder (`+0x40D8..+0x40E8`) |
| `1/14` (`0x3D924925`) | `+0xA518` (unidentified) | ctor |
| `0.9921875 / 0.992188` | `0x3F7E0000 / 0x3F7E0002` (unidentified scales) | ctor (`+0xA430/+0xA438`) |
| `1.0646746e−05` | id-11 scale factor | m02 |
| `100.0, 0.25` | interval-glide rate formula constants | process |
| `50.0` | `+0xA4EC` = 2048·50 | ctor |

## 9. Open items for this document

1. Semantics/units of `hysteresis` (bit 0 unconsumed in process), `correctrate`, `mincorrect`, `quantization`, `tuning` beyond their mechanical handling above. [UNK]
2. Exact role of the two 44-slot directories (id 0x15 vs 0x16) and the restart pulse protocol as seen by the consumer side (delay-read engine / `moddelay`). [UNK]
3. `+0x4518` sub-object identity (`0x18013BBC0`); candidate: tracker subsystem. [UNK]
4. Writer/consumer pairing for `out1..outN` slots at `+0x80`. [UNK]
5. `xfadetime` block consumption site (prepare-side read engine) — doc 07 refines. [UNK]
