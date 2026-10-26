# OCTAVOX_ROUND6_DSP_FINDINGS

**Round 6 — closing the DSP data paths**
Target `F:\VST\OV_DIS\Ox.vst3`, SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
(PE, x86-64). Static analysis only; the target was never modified or executed.
Labels: **OBS** observed in the binary · **DER** derived from OBS · **UNK** still open ·
**CONFLICT** two observations disagree · **RETRACTED** earlier claim withdrawn.

All addresses are image VAs. Decompile artefacts referenced here are in
`OX_RESEARCH/analysis/` and are exported verbatim from Ghidra 12.1.4 (REA).

---

## 1. Executive summary

| id | question | status | one-line answer |
|---|---|---|---|
| W1–W3 | What fills the 16-float, 72-float and `int[12]` engine tables? | **CLOSED** | `dtonicii9::update` (`FUN_1800E5160`) writes them **element by element through the host port-publish vtable** (`owner->setPortValue`, slots `+0xA0` / `+0x98`), ports 25/26/27. No direct memory store exists, which is why the displacement scan was empty. |
| Q-6 | Producer of the state bit consumed in `FUN_1800E5160`, path to `FUN_1800E7960` | **CLOSED** (interpretation corrected) | The bit is the **`gatelevel` parameter dirty bit** (`[module+0x3B8]` bit 1), set by the shared-source publish path. `FUN_1800E7960` is the **gatelevel applier**, not a tracker reset. The actual "full rebuild" trigger is bit 0 = `minpitch`. |
| Q-7 | Delay-read-rate → pitch relationship | **CLOSED** (structure) / **DER** (calibration) | The read position **decreases** by `dVar33` per output sample, so the pitch multiplier equals the **magnitude** of the per-sample position step. `dVar33 = poly(f)·page·64`, with `poly` = quartic in `f`. |
| Q-8 | Source of the value written under `LatencySamples` | **CLOSED (negative)** | `*(u32*)(obj+0x24)`, written by `FUN_180310F20` — which has **zero references** in the whole image (dead code). It therefore proves nothing about a host-facing latency. |
| C5 / Q-10 | Gate-level scaling; `sht1` vs `sht2..sht8` ranges | **CLOSED** (scaling) / **CONFLICT** (authoring) | Gate level is converted with `10^(x/10)` (power) and clamped to ≤ 1.0. `pcent` (fshift) is in **cents**; `sht1` is ±2400, `sht2..8` are ±1 → voices 2–8 can only be fine-shifted by ±1 cent. |
| Q-11 | Meaning of `delay_xf.tapdls1` | **CLOSED** | Tap-1 delay in **ms** = `max(fbkd, 50)`, internally limited by `maxdelay = 2400` ms. |

Round 6 also delivered the independent PoC work: a real WAV→WAV path, a second and third
pitch-shift design, and a measurement-driven comparison (see
`OCTAVOX_DSP_DEMO_USAGE.md` and `OCTAVOX_ROUND6_MEASUREMENTS.md`).

---

## 2. W1–W3 — who fills the engine tables (CLOSED)

### 2.1 Why the earlier scans failed

The three tables are **inline members** of the module object (not of the DSP sub-object):

| port | object | header | data | element type | count |
|---|---|---|---|---|---|
| 25 | `module+0x4310` | vtable `0x18040A308`, count at `+0x8` | `+0x4320` | float32 | 16 |
| 26 | `module+0x4360` | vtable `0x180409BE8` | `+0x4370` | float32 | 72 |
| 27 | `module+0x4490` | vtable `0x180409C48` | `+0x44A0` | int32 | 12 |

**OBS** `FUN_1800E0360` (module DSP constructor) at `0x1800E0BD8…0x1800E0C56`:

```
0x1800E0BD8  lea  r8,  [r15+0x4310]      ; object 25
0x1800E0BE3  mov  dword ptr [r8+8], ebx ; count = 0
0x1800E0BE7  lea  rax, [rip+0x32971A]   ; vtable 0x18040A308
0x1800E0BEE  mov  qword ptr [r8], rax
0x1800E0BF1  lea  r14, [r8+0x10]        ; data
0x1800E0BF8…0x1800E0C06  four × movups [r14+…], xmm0   ; zero 64 bytes
0x1800E0C0B  lea  edx, [rbx+0x19]       ; port id 25
0x1800E0C0E  lea  r9d, [rbx+0x10]       ; count 16
0x1800E0C15  call 0x1800C57C0           ; registry: register(owner, 25, obj, 16)
```

and the same pattern for `+0x4360` (id 26, count 0x48 = 72) and `+0x4490`
(id 27, count 0xC = 12). The registry function is `FUN_1800C57C0`.

A whole-`.text` displacement scan finds **no store** to `[reg+0x4320]`,
`[reg+0x4370]`, `[reg+0x44A0]` outside these constructors. That is because the values
are not stored by the DSP at all: they are **published through the module host**.

### 2.2 The writer

**OBS** The module update `FUN_1800E5160` is called once per block from the kernel's
module chain:

```
0x180106B24  lea  rcx, [rbx+0xBFB8]   ; module 275 = dtonicii9
0x180106B2B  movzx edx, dil
0x180106B2F  call 0x1800E5160
```

(`FUN_1801061A0`, the `miso` kernel) and identically at `0x1801206E4…0x1801206EF`
(`FUN_18011FD60`, the `siso` kernel). `rbx` is the kernel base; `module = kernel+0xBFB8`
matches `slot 0xBFB8`, `index 275` from `kernel_modules.json`.

The update has two parts (the `.pdata` table splits it into chunks
`0x1800E5160–0x1800E5999` and `0x1800E5999–0x1800E78FE`; Ghidra merges them because the
second is a continuation of the same control flow):

1. `FUN_1800E5160+0x00…` — dirty-bit dispatch (parameters, see §3).
2. continuation `0x1800E5999…0x1800E78FE` — **rebuild and publish** the tables.

**OBS** the publishes (decompile `rea6c_1800E5160.txt`):

| port | site | call | loop | values |
|---|---|---|---|---|
| 25 | `0x1800E6C10` | `call [rax+0xA0]` at `0x1800E6C24` | 16 iterations, index 0…15 | `afStack_330[lVar23+6]`, `lVar23 = 0xF…0` → **reverse** of elements 6…21 |
| 26 | `0x1800E6DDF` | `call [rax+0xA0]` at `0x1800E6DF4` | `iVar15*6` iterations (= 72 when `steps=12`) | `afStack_330[lVar33+6]`, `lVar33` descending → **reverse** of the 72 row floats |
| 27 | `0x1800E6E89` | `call [rax+0x98]` at `0x1800E6E97` | 12 iterations | constant `0` or `0x7FFFFF` (8388607) |

`rax+0xA0` is the host's **shared-array port write** (`setPortValue(owner, moduleId,
portId, index)`), `+0x98` is its integer variant. `moduleId = [module+8] = 0x113 = 275`
(set by the constructor at `0x1800E2C35`).

This is the complete answer to W1–W3: **there is no table filler in the ordinary sense**;
the module recomputes the tables every update and pushes them through the port interface.

### 2.3 What the tables contain (OBS/DER)

The source buffer is built in the same update from:

* the **static temperament table** at `0x1803BB690`:
  5 rows × 12 int32 cents values, stride 0x30:
  * `0x1803BB690`: `0,100,200,…,1100` — 12-TET
  * `0x1803BB6C0`: `0,112,204,316,386,498,590,702,814,884,996,1088` — just intonation
  * `0x1803BB6F0`, `0x1803BB720`, `0x1803BB750` — three further historical temperaments
* the degree maps at `0x1803BB60C` / `0x1803BB610` (int32, the 17-value map
  `0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11`) and the 12-entry interval patterns at
  `0x1803BB660` / `0x1803BB780` (`1,0,2,0,3,4,0,5,0,6,0,7`);
* `exp(cents·ln2/1200)` (`FUN_180351F50`), i.e. cents → frequency ratio;
* the current `scale` (1…18), `key` and the 12 `userscale` values.

Each of the 12 rows is 6 floats (`rea6c_1800E5160.txt` lines 1344–1357):

```
row[0] = (float)(int)puVar24[-4]        ; degree / step index
row[1] = (float)(int)puVar24[-2]        ; second index
row[2] = exp(cents·ln2/1200)            ; ratio
row[3] = (1/ratio_cur)·ratio_prev / tune
row[4] = (1/ratio_cur)·sqrt(ratio_next) / tune
row[5] = sqrt(ratio_next)
```

**DER** Therefore the 16-float table is the **reverse of the first 16 row floats**
(rows 0, 1 and the first four fields of row 2), not a semantically independent table.
The consumer `FUN_1800DFE80` scans it as eight stride-2 pairs
(`rea6b_1800DFE80.txt` lines 49–83); because the prefix is flat, those "pairs" are
consecutive fields `(row[0],row[1]), (row[2],row[3]), …`. Whether that pairing is
intentional is **UNK**; the arithmetic is reported, not a design claim.

**DER** `int[12]` is a single **stability threshold** repeated 12 times:
`0` if `|x| < 1`, else `0x7FFFFF`, where `x` is the clamped parameter at
`param_1[0x36]` (lines 1378–1403). The quantizer compares it with a stability
counter (`rea6b_1800DFE80.txt` line 42), so `0x7FFFFF` disables the "stable" path.

---

## 3. Q-6 — the state bit and `FUN_1800E7960` (CLOSED, interpretation corrected)

### 3.1 The bit is a parameter dirty bit

**OBS** `FUN_1800E3350` (module record → binding blocks) installs one 0x30-byte block per
parameter group. Each block stores a **flag pointer** at `+0x10` and a **bit index** at
`+0x18`. The sites and their flag words:

| block | parameter | flag word | bit | install site |
|---|---|---|---|---|
| `+0x1C0` | `minpitch` | `+0x3B8` | 0 | `0x1800E3834–0x1800E3842` |
| `+0x1F0` | `gatelevel` | `+0x3B8` | 1 | `0x1800E4C44–0x1800E4C4E` |
| `+0x…` | `shift` | `+0x3C0` | per voice | `0x1800E3EC8` |
| `+0x…` | `pcent` | `+0x3C8` | per voice | `0x1800E477D` |
| `+0x…` | `modamt` | `+0x3D0` | per voice | `0x1800E41AF` |
| `+0x…` | `delayamt` | `+0x3D8` | per voice | `0x1800E4496` |
| vector | `userscale` | `+0x3E0` | element index | `0x1800E4A64` |

The `minpitch` name is built with `movabs rax, 0x68637469706E696D` ("minpitch") at
`0x1800E385B`; the `gatelevel` name is the literal `0x180408568` (`"gatelevel"`, len 9)
passed to `FUN_1800C55A0` at `0x1800E4CB3`. The `gatelevel` block starts at
`+0x1F0`; `+0x200`/`+0x208` are its own `+0x10`/`+0x18`, which is why the flag
`{+0x3B8, 1}` belongs to it.

The bit is produced by the generic **publish** path used by every module: when the
bound source changes, the publisher ORs `1 << bit` into the flag word. The same
mechanism is visible in `FUN_18013A790` (`lock or qword ptr [flag], 1<<bit`,
`rea6_18013A790.txt`), and the consumer side is
`lock or qword ptr [rax], rdx` with `rdx = 1 << [r8+8]` at `0x1800E55E0–0x1800E55ED`.
The word is initialised to `-1` by the module constructor (`0x1800E2C00` at
`0x1800E2FA1`), which is why the first update always runs.

### 3.2 The consumer

**OBS** `FUN_1800E5160` dispatches on the dirty word at entry:

```
0x1800E51E0…0x1800E5220:
   mov  rax, [module+0x3B8]
   and  rcx, ~2                ; clear bit 1
   lock cmpxchg [module+0x3B8], rcx
   shr  al, 1
   test al, 1
   call 0x1800E7960            ; only if bit 1 was set
```

Bit 1 = `gatelevel`. Bit 0 (`minpitch`) is consumed separately at
`0x1800E5942` (`test al,1`), where it sets the "full recompute" flag
(`r14d = 1`, `[+0x3EC] = 0`, `r12d = 1`, `[rsp+0x44] = 1`).

**RETRACTED:** round 5 described `FUN_1800E7960` as a "state clear / tracker reset".
It is not. It is the **gatelevel applier**, and the actual rebuild trigger is the
`minpitch` bit.

### 3.3 What `FUN_1800E7960` does (OBS)

Decompile `rea6_1800E7960.txt`; function `0x1800E7960–0x1800E7B7A`.

```
value = live_source ? *live_source : block[+0x28]     ; +0x210 / +0x218
value = clamp(value, block[+0x00], block[+0x04])      ; +0x1F0 / +0x1F4
gain  = exp(value * 0.23025850929)                    ; = 10^(value/10)
gain  = min(gain, 1.0)
owner->setParam(moduleId, 2, gain)                    ; vtable +0x60
owner->setParam(moduleId, 4, (value == -99 || value == -97) ? 0.6666667f : 0.0f)
block[+0x420] = (value == -98 || value == -97) ? 1 : 0
```

`0.23025850929 = ln(10)/10`, so the conversion is **dB → linear power**, clamped at
unity. `-99`, `-98`, `-97` are sentinel values with dedicated branches.
Parameter 4 is `0x3F2AAAAA` = **2/3 = 0.6666667** (round 6 wrote 1.3333334; corrected
in the round-7 pass — see §10.1).

**Consumer of `[+0x420]` located in the round-7 pass (§10.1).**

---

## 4. Q-7 — read-rate → pitch (CLOSED structurally, calibration DER)

**OBS** `FUN_1800E2030` (prepare) computes the per-voice read step
(`dec2_dsp_prepare_1800E2030.txt` lines 121–125):

```
dVar33 = ( fVar1·fVar32·c3 + fVar1²·c4 + c2·fVar1 + c1·fVar32 + c0 ) · page[+0x4128] · 64.0
```

with `fVar1 = fVar32²` and the coefficients stored by `FUN_1800E18B0` at
`dec2_fn_1800E18B0.txt` lines 283–287:

| field | bits | value |
|---|---|---|
| `+0x40D8` | `0x3E800000` | 0.25 |
| `+0x40DC` | `0x3EB12E86` | 0.34630 |
| `+0x40E0` | `0x3E79C4BB` | 0.24410 |
| `+0x40E4` | `0x3DF569C6` | 0.11983 |
| `+0x40E8` | `0x3D0ED15E` | 0.03489 |

so `poly(1) = 0.99512`, `poly(0) = 0.25`, `poly(2) = 3.436` — `f` is a **normalised
control**, not the ratio itself.

The read loop then does:

```
dVar11 = position
for each output sample:
    dVar10 = dVar11 - dVar33
    if (dVar10 < 0) dVar10 = dVar11        ; guard
    y = interpolate(buffer, dVar11)
    dVar11 = dVar10
```

**DER** The position **decreases** by `dVar33` per output sample, therefore the output
frequency is `dVar33 × f_in`: the pitch multiplier is the **magnitude** of the step.
Reading backwards time-reverses each grain (phase inversion of the sinusoid) but does
**not** invert the perceived frequency, so there is no sign flip: a step > 1 is an
upward shift, a step < 1 a downward shift.

**DER (calibration)** `dVar33 = poly(f)·page·64`, so the unity condition is
`poly(f)·page·64 = 1`. The `page[+0x4128]` value is itself written by the module
update from a cents→ratio conversion; the exact unity pair of `(f, page)` was not
solved numerically (**UNK**). The *sign and structure* above do not depend on that
calibration.

---

## 5. Q-8 — `LatencySamples` (CLOSED, negative result)

**OBS** `FUN_180310F20` (VA `0x180310F20`, size 3254) serialises a settings object:

```
local_60[0] = *(undefined4 *)(param_1 + 0x24);      // the value
...
local_1a0 = "LatencySamples";
FUN_1801A8800(&local_228, lVar4, &local_1a0);
```

**OBS** A whole-`.text` scan (`0x180001000–0x1837D000`, 979 553 instructions) finds:

* **0** direct `call`/`jmp` to `0x180310F20`,
* **0** RIP-relative references to `0x180310F20`,
* **0** 8-byte pointers to `0x180310F20` anywhere in the image,
* **0** references to the `"LatencySamples"` literal (its only copy is at
  `0x1803B5BF0`; the function builds the key inline with `movabs`).

**DER** The routine is **unreferenced in this build**. The settings key therefore cannot
be produced by the live plug-in and is **not** evidence of a host-facing latency
interface. The value's immediate source is the cached 32-bit field `[obj+0x24]`; no live
writer of that field could be tied to this path.

**UNK** The VST3 `IAudioProcessor::getLatencySamples()` implementation was not located.
We make **no claim** about the target's reported latency; the PoC latency in
`OCTAVOX_ROUND6_MEASUREMENTS.md` is our own measurement.

---

## 6. C5 / Q-10 / Q-11 — gate scale, `sht` ranges, `tapdls1`

### 6.1 Gate scaling (OBS/DER)

`10^(x/10)`, clamped to 1.0 (§3.3). This is a **power** ratio, not an amplitude ratio
(`10^(x/20)` would be amplitude). The published parameter id is 2; the DSP-side
comparison that consumes it was not located (**UNK**, so the *threshold vs gain*
interpretation is **DER**, not OBS). Sentinel values −99/−98/−97 are special-cased.

### 6.2 `sht1` vs `sht2..sht8` (OBS → CONFLICT)

From `kernel_modules.json` (`mimo` profile):

| module | statement | min | max | res | default |
|---|---|---|---|---|---|
| `sht1` | `fshift: %f` | −2400 | 2400 | 1 | 0 |
| `sht2` … `sht8` | `fshift: %f` | −1 | 1 | 0.001 | 0 |

All eight bind to `pcent1…pcent8` and are parsed by the **same** vector/loop
(`param_1+0x298`, 0x30-byte blocks, flag `+0x3C8`), so the engine applies one unit
convention to all of them.

**OBS** `pcent` is in **cents**: the clamped value `auVar57._0_4_` is added directly to
the cents-domain sum before the ratio conversion
(`rea6c_1800E5160.txt` lines 1422, 1451, 1461:

```
exp( ( … + fVar58 + auVar57._0_4_ ) * 0.00057762265 )
```

where `0.00057762265 = ln(2)/1200` and `fVar58 = 1200·octave`).

**DER / CONFLICT (authoring):** with one unit (cents), `sht1 = ±2400` is ±2 octaves
(sensible) while `sht2..8 = ±1` is **±1 cent** (inaudible). The declared resolutions
(1 vs 0.001) are consistent with 1-cent steps in both cases, so the two ranges cannot
both be right. This is a **patch-authoring inconsistency**, not a DSP ambiguity.
Which one is "intended" cannot be decided statically; the engine itself is unit-consistent.
Voices 2–8 are effectively limited to ±1 cent unless the host re-scales the value.

### 6.3 `delay_xf.tapdls1` (OBS/DER)

From the graph records:

```
fbkd       : "fback dly: %f ms",  min 0, max 5000, default 0
c_minmax   : ninputs 2, in1 = fbkd-out, in2 = 50
delay_xf   : maxdelay=2400, xfadetime=20, tapdls1 = c_minmax-maximum, gain1=0
```

**DER** `tapdls1 = max(fbkd, 50)`, i.e. the **tap-1 delay time in milliseconds**, with a
50 ms floor, internally bounded by `maxdelay = 2400` ms. `xfadetime = 20` ms is the
crossfade used when the tap time changes (the "xf" in the name). `delay_xf1` is the
right-channel twin with `in = softclip2-out`.

---

## 7. What round 6 did **not** close

| item | status | note |
|---|---|---|
| Consumer of `[module+0x420]` | UNK | written by the gatelevel applier, no reader found in update/prepare |
| DSP-side gate comparison site | UNK | parameter 2 is published; the comparison itself is not located |
| Unity calibration `(f, page)` in the rate map | UNK | structure and sign are proven, numeric pair is not |
| Host-facing `getLatencySamples` | UNK | no live path found; negative result documented |
| Target two-tap comb (Q-14) | UNK | cannot be proven statically from one build; our PoC reproduces the *architecture*, not the target's exact coefficients |

No claim of full reconstruction is made. The PoC implements the reconstructed
*architecture*; it is not a bit-exact model of the target.

---

## 8. Evidence index for this document

| claim | artefact | locator |
|---|---|---|
| table objects, vtables, ports 25/26/27 | `rea6c_1800E0360.txt` | lines 330–356; code `0x1800E0BD8–0x1800E0C56` |
| per-block update call | `dis_proc` / `functions.csv` | `0x180106B24–0x180106B2F`, `0x1801206E4–0x1801206EF` |
| port 25/26/27 publishes | `rea6c_1800E5160.txt` | lines 1308–1317, 1364–1374, 1398–1403 |
| row construction | `rea6c_1800E5160.txt` | lines 1344–1357 |
| temperament table | raw image + `analysis\temperament_tables.txt` | `0x1803BB690` (5 × 48 bytes); degree maps `0x1803BB60C/610`; patterns `0x1803BB660/780` |
| dirty-bit blocks | `dis_config_str.txt`, `rea6c_1800E5160.txt` | install sites in §3.1; consume at `0x1800E51E0–0x1800E5220`, `0x1800E5942` |
| gatelevel applier | `rea6_1800E7960.txt` | whole function |
| rate polynomial | `dec2_dsp_prepare_1800E2030.txt` | lines 121–125 |
| coefficients | `dec2_fn_1800E18B0.txt` | lines 283–287 |
| quantizer / table consumer | `rea6b_1800DFE80.txt` | lines 18–110 |
| `LatencySamples` writer | `rea6d_180310F20.txt` | search `"LatencySamples"` |
| dead-code proof | `find_refs`/pointer scan | 0 refs (979 553 instructions + full image) |
| `pcent` in cents | `rea6c_1800E5160.txt` | lines 1422, 1451, 1461 |
| `sht` ranges | `kernel_modules.json` | modules `sht1`…`sht8` |
| `tapdls1` chain | `kernel_modules.json` | `fbkd`, `c_minmax`, `delay_xf`, `delay_xf1` |

---

## 10. Round-7 supplementary pass (2026-10-20)

Two of the five round-6 residuals are now closed, one is downgraded to a documented conflict, and one
is confirmed negative with a stronger scan. Nothing here contradicts §1–§9 except the explicit
corrections listed.

### 10.1 Consumer of `[+0x420]` — located (OBS)

* **Reader:** `FUN_1800E7B80` (`0x1800E7B80–0x1800E7DAA`), called from `FUN_1800E5160` at
  `0x1800E5AED` — i.e. *after* the gatelevel applier call at `0x1800E521B`, so a flag set in the
  same update is already visible.
* **Site:** `0x1800E7C14` `cmp dword ptr [rcx+0x420], 1`; `0x1800E7C1B` `mov eax, 0x3C`;
  `0x1800E7C2C` `cmove ebx, eax`. The switch above (`0x1800E7BB2–0x1800E7C0F`) selects a
  per-parameter index base (`0x1B`, `0x1C`, `0x1E`, `0x1F`, `0x23`, `0x24`, `0x26`, `0x30`); when the
  flag is 1, the base is **replaced by `0x3C` (60)**.
* **What the base does:** `0x1800E7C28–0x1800E7C94` (index-only) and `0x1800E7CA1–0x1800E7D0B`
  (`base + index`) compute `int(pow(1.05946309, -(index)) * 366.9500336370864 + 5.0)` and
  `int(pow(1.05946309, -(base + index)) * 366.9500336370864)`, clamp them to `[r15d, 243]` and
  `[4, 243]` respectively (`0x1800E7C8B` `mov eax,0xF3`; `0x1800E7D01` `cmp r9d,r15d` /
  `cmovl r9d,r15d`), and publish them as module int **parameters 0 and 1** through
  `call qword ptr [rax+0x58]` (`0x1800E7D0B` with `r8d=0`, `0x1800E7D21` with `r8d=1`).
  **Only parameter 0 uses the base**, so the flag changes parameter 0 alone.
  * `1.05946309` = 2^(1/12) (constant `0x180412340`, dumped); `366.9500336370864` = 48 000 / 130.81,
    the **C3 period in samples at 48 kHz** (DER, constant `0x180412500`); `+5.0` at `0x1804123F8`;
    the sign flip is the 16-byte mask at `0x180412C80`; `243` is the tracker lag bound (D3 §4).
  * So the function converts a **semitone index into a period in samples**; the flag adds
    **0x3C = 60 semitones (5 octaves)** to the index of parameter 0.
* **Interpretation (DER, not asserted):** `−98` / `−97` are not a mute of an audio gain; they shift
  the tracker's period parameter far above the lag window (the value saturates at the 4-sample floor).
  The audible intent is not claimed.

### 10.2 The gate field `[engine+0xA4E8]` = `T+0x5FD0`: comparison site located, units CONFLICT

* `FUN_1800E10B0` (m01, engine `setParam`) `case 2` stores to `param_1 + 0x149D` =
  **`[engine+0xA4E8]`**, which is exactly the field the applier writes via `setParam(id, 2, ·)`
  (§3.3).
* Exhaustive byte scan of `.text` (`analysis\find_disp_refs.py 0xA4E8`) finds **one store**
  (`0x1800E1112`, m01: `mov eax,[r8]` / `mov [rbx+0xA4E8],eax`) and **two loads**
  (`0x1800E1778`, `0x1800E1808`, both in `FUN_1800E1520` = m02). One further load uses the same
  field through the tracker's base (next bullet).
* **The field is the tracker's `T+0x5FD0` (OBS).** The tracker sub-object starts at `dsp_t + 0x4518`:
  `0x1800E20C4` `lea rcx,[rsi+0x4518]` immediately before `call 0x18013BBC0`, and the setup at
  `0x1800E0D1F` (`lea rdi,[r15+0x4518]`; `[rdi+0x5F58]=rdi`, `[rdi+0x5F60]=rdi+4`, `[rdi+0x5FC8]=·`)
  initialises the sub-object. `0xA4E8 − 0x5FD0 = 0x4518`, and the same offset reconciles every
  other m01 case with D5's `T`-relative notation (`case 1 → 0xA488/0xA48C = T+0x5F70/0x5F74`;
  `case 6 → 0xA450 = T+0x5F38`).
* **Audio-path comparison site (now located).** `FUN_18013BBC0` (`0x18013C85E–0x18013D1FB`), state 6,
  `0x18013D0DA–0x18013D107`:

  ```
  xmm0 = [T+0x5FD0] − [[T+0x5FC8]+4]          ; gatelevel(dB) − current level(dB)
  if (xmm0 < FLT_EPSILON)  *[T+0x5FC8] = fVar70 ; accept the new magnitude only above the gate
  ```
  `[T+0x5FC8]` is a pointer to a two-float record (`[0]` magnitude, `[1]` level). So the field
  parameter 2 writes **is** the audio-path gate threshold, and the comparison is in the **dB domain**.
  ⚠ **Superseded by §10.8 (round 8):** `[+4]` is a one-pole **power** envelope, not dB, so the
  comparison is power vs power and the branch fires above `10^(g/10)`.
* m02 `case 0xc` / `case 0xd` read the same field for the host read-outs:

  ```
  level = sqrt(*(float *)(page + 4) * 0.9999998812)
  thr   = exp(*(float *)(engine + 0xA4E8) * 0.11513)      ; 0.11513 = ln(10)/20  ->  10^(x/20)
  if (level < thr)  out = 0
  ```

* **CONFLICT (units, OBS).** Both consumers interpret the field as **dB**: the state-6 gate by direct
  subtraction, m02 by `10^(x/20)`. The applier (§3.3) writes `10^(g/10)` — a *power* ratio, clamped
  ≤ 1. At the shipped `gatelevel = −55` the stored value would be `3.16e-6`; then
  `3.16e-6 − (−20) ≈ 20 > ε`, so the state-6 branch would **never** accept a new magnitude (the
  tracker would freeze), and m02's threshold would saturate at `10^(3.16e-6/20) ≈ 1.0000004` instead
  of `10^(−55/20) = 1.78e-3`.
* **Consequence / status.** The comparison site, the field identity and both consumers are now
  proven, but the *effective* value cannot be settled statically: the only store site receives
  whatever its caller passes, and m01 is reachable only through the `dsp_t` vtable (`[01]`; no direct
  call or RIP reference exists). Either the host parameter path writes dB and the applier's write is
  transient/ordered differently, or the applier's `setParam(id,2,·)` does not reach this instance.
  The gate **location** is closed; the **effective units/ordering** remain **CONFLICT/UNK**. The
  round-6 wording "C5 resolved (arithmetic)" stays downgraded.

### 10.3 Host latency getter — still negative, with a stronger scan

* Byte scans for `mov eax, [rcx+0x24]` (26 hits), `mov eax, [rdx+0x24]` (3 hits) and
  `mov rax, [rcx+0x24]` (0 hits).
* The only `mov eax, [rcx+0x24]; ret` leaf (`0x18023A070`) is **not** a procedure in
  `functions.csv`, has **no** 8-byte pointer anywhere in the image and is not a vtable entry, so it
  is not a reachable getter.
* No vtable slot returning `[obj+0x24]` was found. Q-8 stays a **documented negative**; no latency
  value is claimed.

### 10.4 Method note (correction of a round-6 assumption)

`dis_config.txt` is a **partial** listing (it does not contain every function). "No hit in
`dis_config.txt`" is therefore not evidence of absence. All round-7 scans are raw-`.text` byte scans
(`find_disp_refs.py`), which are exhaustive for a fixed displacement; only they are used for the
negative claims in this section.

### 10.5 Q-7 — exact rate-map constants, float32 rounding and the unity *locus* (round 8)

**OBS — the five coefficients are immediates in the engine constructor**, not loaded from `.rdata`.
Five `C7 86 <disp32> <imm32>` stores at `0x1800E1F82–0x1800E1FB4`:

| store VA | field | imm32 | exact float32 value |
|---|---|---|---|
| `0x1800E1F82` | `+0x40D8` | `0x3E800000` | 0.25 |
| `0x1800E1F8C` | `+0x40DC` | `0x3EB12E86` | 0.346058071 |
| `0x1800E1F96` | `+0x40E0` | `0x3E79C4BB` | 0.24391453 |
| `0x1800E1FA0` | `+0x40E4` | `0x3DF569C6` | 0.119830653 |
| `0x1800E1FAA` | `+0x40E8` | `0x3D0ED15E` | 0.0348676369 |

The round-6 table printed 0.34630 / 0.24410 / 0.11983 / 0.03489 (5-decimal rounding of the same
bits). Exact sum: **0.994670891**.

**OBS — evaluation order and precision.** The five coefficients are **float32 members**; each is
loaded with `MOVSS` (`F3 0F 10 86 <disp32>`) and **widened to double** with `0F 5A` (`CVTPS2PD`) at
`0x1800E2371/2379` (`+0x40E4`), `0x1800E2395/239D` (`+0x40E8`), `0x1800E23A4/23AC` (`+0x40E0`),
`0x1800E23B3/23BF` (`+0x40DC`), `0x1800E23C6/23CE` (`+0x40D8`). `f` itself is a **double** product of
two widened float32 values (`MULSD` at `0x1800E236D`), and the quartic plus both multiplier steps are
`MULSD`/`ADDSD` in double (`0x1800E236D–0x1800E23DD`). There is **no** float32 rounding of `P`
(no `CVTPD2PS` occurs in the sequence; the `(double)(...)` cast printed by the decompiler is the
rendering of these widenings). The multiply constant is the **double 64.0** at `0x180412490` (double
pool: 48.0 `0x180412480`, 57.6000137669 `0x180412488`, 64.0, 96.0, 99.0, 100.0). Net:

```
f    = (double)F * (double)page[+0x40F8]          ; F = **(base + page[0x68] + 8)
P    = 0.25 + c1 f + c2 f^2 + c3 f^3 + c4 f^4     ; double; float32 coefficients widened once
step = P * (double)page[+0x4128] * 64.0           ; double; 1/256-sample units applied later
```

**OBS — the two slots are per-voice field *offsets***, installed as in-place-constructed objects
(`call 0x180010E30`) at `0x1800E7EF6` (`+0x4128`) and `0x1800E7F0E` (`+0x40F8`) and read back as
`page + [slot]`. They are not port pointers and not literals; the numeric values inside them are
fixed by the module-graph binding, not by the DSP code.

**OBS — writers** (prepare decompile lines 188/194): `page[+0x4128] = local_110[0] *
(pitchtracking ? engine[0x98 + ((i+2) & 0xFFF)*4] : 1.0)` with a fallback to `local_110[0]` when
`|x| <= 1e-14`; `page[+0x40F8] = local_110[10]`; `local_110` is a per-voice record base
(`[param_1+0x4280] + voice*0x2C*4`, second page `+0x42A8`).

**DER — unity is a curve, not a pair.** `step = 1` sample/sample ⇔ `P(f)·R = 1/64 = 0.015625`, where
`R = page[+0x4128]`. All five coefficients are positive, so `P` is strictly increasing on `[0,∞)`,
`P(0) = 0.25`, `P(1) = 0.994670891`, and `P(f) = 1` at **`f = 1.003988571`**:

| f | P(f) | R required for unity |
|---|---|---|
| 0 | 0.250000 | 0.062500000 (= 1/64) |
| 0.25 | 0.353768 | 0.044167397 |
| 0.50 | 0.501166 | 0.031177312 |
| 0.75 | 0.708331 | 0.022058885 |
| 1.00 | 0.994671 | 0.015708713 |
| 1.003989 | 1.000000 | 0.015625000 |
| 2.00 | 3.434302 | 0.004549688 |

Consequences (all DER, none runtime): (a) unity requires `R <= 1/64` for every admissible `f`;
(b) if the binding is the natural `R = ratio/64`, then `step = ratio·P(f)`, i.e. the map is
**−9.25 cents at `f = 1`** and exactly unity at `f = 1.003989`; (c) any claim of a single
`(f, R)` pair is **not supported by the code** — the pair is state-dependent.

**Status:** Q-7 calibration **PARTIAL** — formula, exact constants, rounding and the unity locus are
proven; the numeric binding of the two slots remains graph/runtime-dependent. No runtime value is
claimed.

### 10.6 Q-8 — host path mapped, "vtable slot returns latency" refuted (round 8)

Supersedes the *method* of §10.3 (its conclusion stands: no latency value is claimed).

**OBS — RTTI map.** 1273 valid MSVC complete-object-locators exist in `.rdata` (each COL points at
a type descriptor whose decorated name sits at descriptor `+0x10` in this image; the vtable is the
qword immediately after the COL pointer). The plugin classes:

| class | COLs (offset) | vtable |
|---|---|---|
| `.?AVVoxAudioProcessor@@` | `0x18041ACF8` (0), `0x18041A098` (0x150), `0x18041A928` (0x158) | **`0x1803F05D0`** (primary), `0x1803F04E8`, `0x1803F03B8` |
| `.?AVJuceVST3Component@juce@@` | `0x180417210` (0), `0x180419D30` (8), `0x1804182A0` (0x10), `0x180419B48` (0x18), `0x1804178E0` (0x20), `0x1804185C0` (0x28) | `0x1803EF8A0`, `0x1803EF580`, `0x1803EFDC0`, `0x1803EF0D0`, `0x1803F01E8`, `0x1803F0210` |
| `.?AVJuceAudioProcessor@juce@@` | `0x180418300` (0) | `0x1803EF950` |

The primary `VoxAudioProcessor` vtable is confirmed independently by its destructor
(`0x1800175D0`: `lea rax,[0x1803f05d0]; mov [rcx],rax`). The `JuceVST3Component` off-0 vtable
`0x1803EF8A0` is the 14-entry IComponent interface (`0x18000C230`, `0x18000C1F0`, `0x18000C200`,
`0x18000C320`, `0x18000C3D0`, `0x18000C570`, `0x180006AC0`, `0x18000DF30`, `0x18000DF60`,
`0x180006AC0`, `0x18000E160`, `0x18000C580`, `0x18000DA70`, `0x18000DB30`). Note that
`0x1803EF8A0 + 0x70` is followed immediately by COL `0x180419A88` (`EditControllerEx1`, offset 0x18)
— that is the next record in `.rdata`, **not** part of this vtable.

**OBS — the delegating sub-object.** `JuceVST3Component`'s `+0x10` COL `0x1804182A0` (vtable
`0x1803EFDC0`) is a secondary interface: entries 0–2 are `sub rcx,0x10; jmp rel32` adjustor thunks
(`0x180016F98`, `0x180016F00`, `0x180016CC4`), entries 3–14 are twelve stubs of the form
`mov rcx,[rcx+0x38]; mov rax,[rcx]; jmp qword ptr [rax+DISP]` with `DISP = 0x18, 0x20, 0x28, 0x30,
0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70` (`0x18000DD30`, `0x18000DD40`, `0x18000DD50`,
`0x18000DD60`, `0x18000DD70`, `0x18000DD80`, `0x18000DD90`, `0x18000DDA0`, `0x18000DDD0`,
`0x18000DDB0`, `0x18000DDE0`, `0x18000DDC0`). Three of them (`+0x40`, `+0x50`, `+0x68`) load the
target into `r10` instead of a direct tail jump — the by-value-return shape. So this interface
forwards every method from #3 upward to a vtable reached through the pointer at `+0x38`; the identity
of that target object is not resolved statically.

**OBS — `JuceAudioProcessor`.** Its single COL (`0x180418300`, offset 0) yields the 16-entry vtable
`0x1803EF950`: `[0] 0x1800066E0` = `mov qword ptr [r8],0; mov eax,0x80004001; ret` (an `E_NOTIMPL`
`queryInterface`), `[5] 0x1800068E0` and `[6] 0x180006900` read a pointer member at `+0x70` and call
its virtual `+0xC0`. It is an interface-style object; whether it is the delegate behind `+0x38` is
**not** proven by bytes alone.

**OBS — `VoxAudioProcessor` primary vtable `0x1803F05D0`, first slots:**

| slot | VA | body (first bytes / behaviour) |
|---|---|---|
| 0 | `0x1800175D0` | destructor; stores the four base vtable pointers at `+0`, `+0x150`, `+0x158`, `+0x2600` |
| 1 | `0x1801A72A0` | returns `String("Octavox")` (`0x18038EB60`) by value |
| 2 | `0x1801A7250` | returns `String("Ocxv")` (`0x18038EB68`) by value — the short name |
| 3 | `0x180313D20` | `(this, double, int)` → `prepareToPlay` |
| 4 | `0x180313BC0` | `mov byte [rcx+0x25C0],1; ret` → `releaseResources` |
| 24 | `0x180004660` | `mov eax,1; ret` |
| 46 | `0x180147A40` | `mov eax,[rcx+0x27DC]; ret` |
| 54 | `0x180319200` | `test edx,edx; js …; cmp edx,[rcx+0x27DC]; jae …; mov rax,[rcx+0x140]; …` → program-name lookup |
| 55 / 57 | `0x18025F6E0` / `0x18025F660` | same shape against `[rcx+0x14C]` |

**OBS (refutes the obvious latency candidate).** `+0x27DC` is the **program count**: slot 54 uses it
as the bound of the program-name array at `[rcx+0x140]`. A raw-`.text` scan for stores to
displacement `0x27DC` finds **exactly one** — the zero-initialisation
`*(u32*)(this+0x27DC) = 0` at `0x180319764` inside `tide::DigiPluginBase`'s constructor
`0x180319510` — and no `cmp [rcx+X],edx` / `mov [rcx+X],edx` test-and-set pair anywhere targets it.
So slot 46 is `getNumPrograms()` returning a constant 0, **not** `getLatencySamples`.

**OBS — exhaustive inline-getter scan.** Whole `.text` (3 650 048 bytes) scan for
`mov r64,[rcx+d]; mov eax,[r64+d]; ret`, all disp8/disp32 combinations, base `rcx`: **12 hits**
(a further 2 hits use zero displacement — `0x1802F2490` → `[[rcx]+0x10]`, `0x1802F2500` →
`[[rcx]+0x14]`, neither vtable-referenced), of which only 3 are vtable entries, and
each resolves (through the COL immediately preceding its vtable) to an unrelated JUCE class:

| getter | vtable slot | owning class |
|---|---|---|
| `0x180083410` `[[rcx+0x80]+0x148]` | `0x180403A98` | `FileListTreeItem@juce` (COL `0x18042CDB8`) |
| `0x180099AE0` `[[rcx+8]+0x2DC]` | `0x180405C80` | `TextEditorTextInterface@TextEditorAccessibilityHandler@juce` (COL `0x180427C78`) |
| `0x18014A110` `[[rcx+8]+0x138]` | `0x18040D970` | `CodeEditorComponent::TextInsert` accessibility interface (COL `0x180438628`, ctor `0x18031DAC0`) |

No slot of the 60-entry `VoxAudioProcessor` vtable is a getter of that shape, and the only plain
`mov eax,[rcx+d]; ret` slot is [46] (program count, above).

**DER.** In this JUCE build `AudioProcessor::getLatencySamples()` is a **non-virtual inline member
read**, so it cannot appear as a vtable slot. The host value is therefore produced by the wrapper /
adapter inlining that read — and that exact read shape does **not** occur anywhere in `.text`.
Remaining possibilities are (a) a cached latency stored in the adapter, (b) a different code shape,
or (c) constant-folding to 0 at the call site. None can be selected without a runtime observation.

**Status:** Q-8 stays **OPEN (negative)**. Newly closed: the exact `VoxAudioProcessor` vtable, the
complete `JuceVST3Component` / `JuceAudioProcessor` COL/vtable sets, the `+0x38` delegation shape of
the `+0x10` sub-interface, and the refutation of the "vtable slot returns the latency" hypothesis
(that slot is the program count and is always 0). The host-latency **value** is still UNK and none is
claimed.

**Attribute correction (same round).** An earlier draft of this section attributed COL
`0x1804182A0` to `JuceAudioProcessor` and its delegation stubs to that class. Reading the COL's own
type descriptor shows `0x1804182A0` is `.?AVJuceVST3Component@juce@@` at offset `+0x10`; the
`JuceAudioProcessor` COL is `0x180418300` (vtable `0x1803EF950`). The corrected attribution is what
is stated above; nothing about the negative depends on it.

### 10.7 Round-8 method note

Every new claim above rests on raw file/`.text` scans or on the COL→vtable chain resolved from the
binary itself (no symbol names from Ghidra are used as evidence). The COL enumeration
(`all_cols.py`) requires the type-descriptor name at `pTypeDescriptor + 0x10` in this image; using
`+0` or `+8` yields zero hits and is a known false negative of the earlier script.

### 10.8 Round-8b: the gate is power-domain; the units CONFLICT sits in the derived outputs (Q-15/C5)

This section supersedes two round-7 statements: "the audio gate at `0x18013D0DA` is dB-domain" and
"at the shipped `-55` the state-6 branch would never fire". Both were written before the envelope
that the gate compares against was read. That envelope is a **power** accumulator, so the gate is
domain-consistent and fires for every level above the threshold; the unit error is elsewhere.

**OBS - the routine is chunked, and the gate really is in case 6.** `functions.csv` lists three
"functions" at `0x18013BBC0` (282 B), `0x18013BCDA` (73 B) and `0x18013C85E` (2461 B). Control flows
linearly through the first two boundaries (no `ret`, no dispatch), so this is **one** routine. Its
switch is at `0x18013BE40`:

```
movsxd rax,[r15+0x5FF8]          ; case index
cmp    eax,9 / ja default        ; 0..9
lea    rdx,[rip-0x13BE5F]        ; => 0x180000000
mov    ecx,[rdx+rax*4+0x13D22C]  ; jump table at 0x18013D22C
add    rcx,rdx / jmp rcx
```

| case | target | case | target |
|---|---|---|---|
| 0 | `0x18013BE73` | 5 | `0x18013CD59` |
| 1 | `0x18013C1F3` | 6 | `0x18013CF3C` |
| 2 | `0x18013C85E` | 7 | `0x18013D127` |
| 3 | `0x18013CA08` | 8 | `0x18013D13F` |
| 4 | `0x18013CCD1` | 9 | `0x18013D157` |

The gate block `0x18013D0C0-0x18013D0F2` lies between the case-6 entry (`0x18013CF3C`) and the
case-7 entry (`0x18013D127`), and a whole-`.text` branch scan finds **only three** branches into it,
all local (`0x18013D0B0`, `0x18013D0B5`, `0x18013D0C8`). The round-6 "state 6" attribution is
therefore **confirmed**, and no other case can reach the block.

**OBS - the compared envelope (shared prologue, `0x18013BCEE-0x18013BD84`).**

```
xmm0 = (double)buf[i]                        ; raw input ring sample, +0x5DB0
xmm3 = (double)[T+0x5F1C]                    ; 0x3C000000 = 0.0078125 = 1/128
xmm1 = xmm0 * xmm3 * xmm0                    ; (1/128) * x^2
xmm0 = (double)[rcx+4] * (double)[T+0x5F18]  ; 0x3F7E0000 = 0.9921875 = 127/128
[rcx+4] = (float)(xmm1 + xmm0)
```

Both coefficients are **constructor immediates** (`0x1800E0E14` / `0x1800E0E1E`), and
`c/(1-d) = 0.0078125 / 0.0078125 = 1`, so `[rcx+4]` is a one-pole **power** envelope of the input
with unity steady-state gain for `x^2` (its square root is the amplitude). `[rcx+8]` is the same
recursion driven by the FIR output instead of the raw sample (`0x18013BD70-0x18013BD84`). The update
runs in the **shared prologue before the switch**, i.e. earlier in the same invocation than the
case-6 gate.

**OBS - the applier (`FUN_1800E7960`), now byte-exact.**

```
xmm0 = clamp(g, [rsi+0x1F0], [rsi+0x1F4])   ; 0x1800E79A1-0x1800E79BB
xmm0 = (double)xmm0 * 0.23025850929          ; 0x180412250 (ln 10 / 10 to 11 digits)
xmm0 = exp(xmm0)                             ; 0x180351F50
xmm3 = min((float)xmm0, 1.0)                 ; 0x18041213C
setParam(id, 2, xmm3)                        ; call [vtable+0x60], r8d = 2, 0x1800E79EE
```

The sentinel writes are pinned exactly (`0x1800E7A44-0x1800E7B60`):

| `g` | parameter 2 | parameter 4 | `[module+0x420]` |
|---|---|---|---|
| `-99` | `10^(-99/10)` | `0.666666627` (2/3, `0x180412084`) | 0 |
| `-98` | `10^(-98/10)` | `0` | 1 |
| `-97` | `10^(-97/10)` | `0.666666627` | 1 |
| any other | `min(1, 10^(g/10))` | `0` | 0 |

Parameter 2 still reaches `[engine+0xA4E8]` = `T+0x5FD0` through `m01` case 2 (`0x1800E1112`).
The module-side identity is now RTTI-confirmed (round 8 addendum): `FUN_1800E10B0` is **slot 1** of
`.?AUdsp_t@dtonicii_t@modules@modpp@@` (COL `0x180435D08`, vtable `0x180409F60`), the 18-entry table at
`0x1800E14D0` maps **index 2 -> `0x1800E110F`**, and the default path is slot 3 (`0x1800C5750`, a
base-class thunk), not a recursive self-call. The applier's side (`owner->[vptr+0x60]([applier+8],2,value)`,
`0x1800E79EE`) leaves the module index as a runtime value (**UNK** statically).

**OBS - the comparison, and what the store means (`0x18013D0C0-0x18013D0F2`).**

```
if ([T+0x5F30] >= 0) xmm8 = [T+0x5FD4];       ; 0x18013D0C0
rax  = *(void**)(T+0x5FC8);                   ; envelope block
xmm0 = [T+0x5FD0] - [rax+4];                  ; threshold - power envelope
if (xmm0 < 1.1920929e-07) [rax] = xmm8;       ; FLT_EPSILON, 0x180411E54
```

`threshold - power < eps` is true when the level is **above** the threshold, so the rule is
*update above / hold below* - a **freeze-on-silence gate**, not a mute. Same domain on both sides
(power ratio vs power envelope). The round-7 labels "dB-domain" and "at `-55` the branch would never
fire" are **retracted**: with `g = -55` the threshold is `3.16e-6`, so the branch fires for any input
above roughly `-55 dBFS` of power.

**OBS - where the conflict actually is: `m02` params 12/13.** `FUN_1800E1520` dispatches on
`edx - 7` with seven cases (table `0x1800E1890`); parameter **12** = `0x1800E1751`, parameter **13** =
`0x1800E17E1`. Both have the same shape:

```
corr  = engine[+0xA58C]  (p12)   /   engine[+0xA588]  (p13)
xmm6  = sqrt((double)[[engine+0xA4E0]+4] * 0.99999988)  ; 0x180412320
xmm0  = exp((double)[engine+0xA4E8] * 0.11513)          ; 0x180412200
if (xmm6 < xmm0) corr = 0;
out   = corr > 7.1e-15 ? (float)(ln(corr) * -1731.234) : 0;   ; 0x180412888
```

`-1731.234 = -1200/ln 2`, so the output is `1200*log2(1/corr)` - a **cents** figure - and the level
test is a voicing gate on that read-out. The mismatch: `[engine+0xA4E8]` already holds a **linear
power ratio**, but it is fed to a **dB -> linear** map (`10^(x/20)`). Since `c/(1-d) = 1` the
envelope is normalised, so `sqrt([+4]) <= 1` for `|x| <= 1`, while `10^(field/20) >= 1.0` for every
`field <= 1` (equality only at `field = 0`). **DER: params 12/13 therefore emit 0 for every
normalised internal signal** - a testable prediction, not a runtime observation. (If the internal
signal exceeds full scale the test can pass, so the claim is conditional on normalisation.)

The three constants are hand-typed decimal literals rather than exact doubles: `0.23025850929`
(rel. err. `4.1e-11` vs `ln 10 / 10`), `0.11513` (`6.5e-6` vs `ln 10 / 20`) and `1731.234`
(`2.8e-8` vs `1200/ln 2`) - the `m02` constant is the least precise.

**Status.** Q-15/C5: applier units **CLOSED** (linear power), audio gate **CLOSED** (power vs power,
freeze-above-threshold), **in-call ordering CLOSED** (envelope update in the shared prologue, gate in
case 6 of the same invocation); cross-thread write ordering stays unproven (the applier runs on the
parameter-update path, the voice routine on the audio path). The remaining **CONFLICT is localised to
`m02` params 12/13**, where a linear power is re-mapped as if it were dB. Evidence:
`rea8_gate_units.txt` (disassembly, jump table, constants), produced by `rea8_gate_units.py`.
