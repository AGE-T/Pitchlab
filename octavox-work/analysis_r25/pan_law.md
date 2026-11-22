# stereomixer3 pan law — static RE findings (r25)

Target: `/home/z/my-project/octavox-work/original/Ox.vst3` (PE32+ x64, image base 0x180000000)
SHA-256 verified: `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
Method: read-only static disassembly with Python3 + capstone 5.0.7 (no execution, no modification).

---

## 1. Headline results

| Item | Value |
|---|---|
| **stereomixer3 audio PROCESS function** | **FUN_1800d7f10** @ `0x1800d7f10 .. 0x1800d8222` (0x312 bytes) |
| Called from (audio worker tree) | FUN_180103a10 @ `0x180103a10`, call site `0x180104cbd` (`lea rcx,[rbx+0x1c4b0]; call 0x1800d7f10`) — the FIRST call of the master chain, right after engine prepare `0x1800e2030` @ `0x1801043db` and the 8 voice chains (`0x1800e8b50/0x1800ea650/0x180137e90/0x1800d38b0` on `rbx+0x16a88…`) |
| Object | engine+0x1c4b0 (and a 2nd instance in the worker’s second master chain) |
| ninputs | `[this+0x50]` (int, param **“ninputs”**, fetched by ctor 0x1800d7400 @ 0x1800d74fa) |
| clipvalue | `[this+0x54]` (float, param **“clipvalue”**, ctor 0x1800d7400 @ 0x1800d75fa) |
| **L/R pan application** | per-input gain arrays: current `[this+0x108]` (L) / `[this+0x120]` (R); targets `[this+0xb0]` (L) / `[this+0xd8]` (R); smoothing coef `[this+0x100]`; mix accumulators are **64-bit doubles**; output written to ports `[this+0x80]` (left) / `[this+0x98]` (right), clamped to ±clipvalue and multiplied by a declick fade `[this+0x168]` |
| Smoothing (25 ms) | coefficient computed in FUN_1800d8380 @ 0x1800d8e12–0x1800d8e7f: **coef = 10^(−1/(f·c)) − 1** (negative, convergent one-pole); ramp law everywhere (incl. worker FUN_180103a10) is `val ← val + (val − target)·coef` |
| Pan-law shape | **DECODED — LINEAR law** in FUN_1800d9b20: `L = 0.5·(1−p)·g`, `R = 0.5·(1+p)·g`, with `g = 10^(dB/20)` (0.11512925465 = ln10/20 exponent, powf10 helper 0x180352350); p clamped to [−1, panY]; results clamped ≤1 and pushed per channel via `call [[engine]+0xa0](engine, id, ch, val)` (0x1800d9ce7/0x1800d9d1f) |

---

## 2. Module identification chain (how the process was found)

- The 20-qword dump at `0x180409E20` is **not** an audio vtable: it is a module-registration table of 8-qword records
  `{ctor, 6 shared stubs (0x1800c5730..0x1800c57b0), module-id blob}`:
  record A: ctor `0x1800d9e80`, id @0x1804362a0; B: `0x1800ce090`, id @0x1804357c8; C: `0x1800d4ac0`, id @0x180435820; D: `0x1800c9180`… (STEP 1/2 dead-ended here — no process slots.)
- stereomixer3 module construction chain (record A family):
  wrapper `0x180137720` (parent+0xf0) → ctor1 `0x1800d8230` (creates “obj” sub-node, child links; defaults −10000.0f/32766.0f/1.0f at +0x40/+0x44/+0x48) → ctor2 `0x1800d7400` (**ninputs**/`[+0x50]`, **clipvalue**/`[+0x54]`; child-vector array `[+0x58]`; registers ports “left” @0x1800d7ae0 and “right” @0x1800d7b53; allocates L-gain array `[+0x108]` and R-gain array `[+0x120]` sized ninputs) → ctor3 `0x1800d7c40` → ctor4 `0x1800d8fc0` (declares params `%n_gain`, `%n_pan`, `%n_ins`, `nsamples`, `sr`, `%n_pans`, `n_gains`, `%n_pans` count params).
- Audio worker tree FUN_180103a10 (0x180103a10..0x18010523a, full call map in `worker_calls.txt`):
  ramps → 7× voice chains (0x180139970/0x180139790/0x180139480) → master chain (0x180138e70/0x180138b00/0x180138870/0x1800d0d50/0x180138870/0x180138870/**0x1800ee420**) → **0x1800e2030 (engine prepare)** → 8× (0x1800e8b50, 0x1800ea650, 0x180137e90, 0x1800d38b0) → **0x1800d7f10 = stereomixer3 mix (this=rbx+0x1c4b0)** → 0x1800d0d50(+0x1c6a0) → 0x180137f70(+0x1c780) → 0x1800cec80(+0x1cdf8) → 0x1800dc430(+0x1d2b8) → 0x1801370b0(+0x1d3d8) → 0x180136ee0(+0x1d538) → second instance chain (…+0x1dd80/+0x1e240/+0x1e360/+0x1e4c0).

## 3. The process function, decoded (FUN_1800d7f10)

```asm
0x1800d7f1e  movss xmm0,[rcx+0x160]
0x1800d7f44  addss xmm0,[rcx+0x168]           ; fade counter + step
0x1800d7f5f  movss [rax],xmm0
0x1800d7f72  minss xmm8, [1.0f @0x18041213c]  ; fade = min(fade+step, 1.0)
0x1800d7f7b  movss [rdx+0x168],xmm8          ; store back ("declick" envelope)
; ---- outer loop: r8 = 0, 8  (4-sample sub-blocks; pass pairs (r8, r8+4)) ----
0x1800d7f90  xor r11d,r11d ; i=0
0x1800d7f93  xorps xmm6,xmm6 ; Lacc = 0.0 (double)
0x1800d7f96  xorps xmm7,xmm7 ; Racc = 0.0 (double)
0x1800d7f99  cmp [rdx+0x50],r11d              ; i < ninputs
0x1800d7fa3  r10=[rdx+0x108]  r9=[rdx+0x120] rdi=[rdx+0x58] rsi=[rdx+0xb0] rbp=[rdx+0xd8]
; ---- inner loop per input i ----
0x1800d7fd0  movss xmm4,[r10+r9]      ; Lgain_i (current)   from [+0x108] array
0x1800d7fd6  movss xmm0,[rsi+i*4]     ; Ltarget_i          from [+0xb0] array
0x1800d7fdc  movss xmm5,[r9]          ; Rgain_i (current)   from [+0x120] array
0x1800d7fe1  movss xmm3,[rdx+0x100]   ; smoothing coefficient
0x1800d7fe9  rax=[rdi+rbx+8] rcx=[rax]; input buffer of input i
0x1800d7ffe  xmm1 = Lgain−Ltarget (double)
0x1800d8018  xmm2 = Rgain−Rtarget (double)
0x1800d801c/20  mulsd xmm1,xmm3 ; mulsd xmm2,xmm3
0x1800d8024/28  addsd → Lgain += (Lgain−Ltarget)·coef ; Rgain += (Rgain−Rtarget)·coef
0x1800d8028  movss xmm1,[rcx+r8]     ; s = input_i sample (pass 1)
0x1800d8038  mulsd xmm0,xmm4 ; addsd xmm6,xmm0          ; Lacc += s·Lgain_i
0x1800d803c  mulsd xmm1,xmm5 ; addsd xmm7,xmm1          ; Racc += s·Rgain_i
0x1800d804c  movss [r10+r9],(float)Lgain  ; store back  ([+0x108])
0x1800d8056  movss [r9],    (float)Rgain  ; store back  ([+0x120])
0x1800d805f  cmp r11d,[rdx+0x50] ; jl 0x1800d7fd0
; ---- outputs after inner loop ----
0x1800d8069  movss xmm0,[rdx+0x54]        ; clipvalue
0x1800d8075  comiss xmm1(Lacc),xmm0 ; ja keep clip else max(−clip, Lacc)  → clamp(Lacc, −clip, +clip)
0x1800d808c  mulss xmm0,xmm8                ; × fade (declick)
0x1800d8095  movss [r8+rax],xmm0 ; rax=[rdx+0x80]  → LEFT output port
0x1800d809b..0x1800d80c2  same for Racc → [r8+[rdx+0x98]]  → RIGHT output port
; ---- pass 2 identical with [rcx+r8+4] and out offsets +4; then r8 += 8; cmp r8,0x10; jl 0x1800d7f90
```

**Pseudocode of the mix (per 4-sample sub-block, called per block from the worker):**
```
fade = min(fade + step[+0x168], 1.0)                       // declick envelope
for each sample pair (r8, r8+4):
  L = R = 0.0 (double)
  for i in 0 .. ninputs-1:
     Lg = Lg + (Lg - Lt[i]) * coef                        // one-pole toward target
     Rg = Rg + (Rg - Rt[i]) * coef
     s   = in[i][r8]                                       // input port of voice/slot i
     L  += s * Lg ; R += s * Rg                            // 64-bit accumulate
     Lgain[i]=Lg ; Rgain[i]=Rg                             // store back (float)
  outL[r8]  = clamp(L, -clipvalue, +clipvalue) * fade      // [+0x80] port
  outR[r8]  = clamp(R, -clipvalue, +clipvalue) * fade      // [+0x98] port
```

- L/R target arrays `[+0xb0]`/`[+0xd8]` (allocated in ctor2 0x1800d7400; R array at 0x1800d797a–0x1800d79ea, L array at 0x1800d775b–0x1800d784a) hold the **per-input L and R gain targets**; current arrays `[+0x108]/[+0x120]` are ramped toward them **per sample** with coefficient `[+0x100]`.
- The per-input gain+pan refresh that produces the targets is **FUN_1800d8380** (`0x1800d8380..0x1800d8f2f`), invoked per block via worker M-fn4 0x1801370b0 on `rbx+0x1d3d8`/`rbx+0x1e360`:
  - resizes the per-slot vector to `n = count("gain")` (0x18032b620) and to `count("pan")`;
  - for each slot reads the three components of **“gain”** (X→`0x18032b520`, Y→`0x18032b420`, Z→`0x18032b320`) into child struct {+0,+4,+8} of vector `[r15+0x10]`, then of **“pan”** into vector `[r15+0x28]` (0x1800d8925..0x1800d8959 and 0x1800d8c01..0x1800d8c35);
  - publishes ports via 0x1800c4930 (name “gain”, id, Z) and queues (child+0x10, id) into the engine input-port queue at `[rax+8]` (0x1800d898f..0x1800d89d0);
  - reads **“sr”** (string @0x1804082f8 — reused constant; parsed via 0x1800c3fb0/0x180179220, float → `[r15+0xe8]`), and **“processoff”** (@0x180408368) via 0x1800c55a0 on `[r15+0x40]`;
  - computes the smoothing coefficient (§4) and hands it to the engine: `call [[engine]+0x60](engine, [r15+8], 2, coef)`.

## 4. The 25 ms smoothing (one-pole coefficient) — decoded

At `0x1800d8e12..0x1800d8e7f` (FUN_1800d8380):

```asm
0x1800d8e12  movss xmm6,[r15+0xe8]        ; c_ms (from "sr"-string param slot)
0x1800d8e1b  movss xmm1,[0.001f @0x180411eb4]
0x1800d8e23  mulss xmm6,xmm1              ; c = c_ms * 0.001   (ms → s)
0x1800d8e2a  comisd vs 0.001 (0x1804120e0); jae ok
0x1800d8e34  movaps xmm6,xmm1             ; c = max(c, 0.001)
0x1800d8e37  rdi=[r15]  rax=[rdi]  rbx=[rax+0x60]
0x1800d8e44  call [rax+8]                 ; f = engine.method+8()  → float (rate; sample rate / block rate)
0x1800d8e47  mulss xmm0,xmm6              ; t = f * c
0x1800d8e4e  cvtss2sd xmm1,xmm0           ; (double)t
0x1800d8e52  movsd xmm0,[-2.302585093 @0x180412850]   ; -ln(10)
0x1800d8e5a  divsd xmm0,xmm1              ; -ln10 / t
0x1800d8e5e  call 0x180351f50             ; exp10-family CRT call (xmm0 arg; xmm1 = t also set)
0x1800d8e63  subsd xmm0,[1.0 @0x180412328]
0x1800d8e6e  cvtsd2ss xmm3,xmm0           ; coef (float)
0x1800d8e72  mov r8d,2 ; mov edx,[r15+8] ; mov rcx,rdi(engine)
0x1800d8e7f  call rbx                     ; engine.method+0x60(engine, id, 2, coef)
```

**coef = 10^(−1/(f·c)) − 1** (c = smoothing seconds clamped ≥ 1 ms, f = rate from engine method+8). For a 25 ms window at 48 kHz (t = 1200): coef ≈ −1.92e−3. This matches the convergent ramp used **everywhere** in the engine (`val ← val + (val − target)·coef`, e.g. worker 0x180103a27..0x180103aaa ramps and the per-sample gain ramp in FUN_1800d7f10): with coef < 0 the value converges exponentially to target; 10^(−1/t) reaches −20 dB after t steps.
Note: 0x180351f50 was previously labelled “pow”; at this site the base is negative (−ln10/t), so the only consistent interpretation is a 10^x-style exp (the sibling call site in 0x1800c76c0 computes `exp(r·ln2)=2^r`). If it is really 2-arg pow, the pair (base=−ln10/t, exp=t) is also mathematically = 10^(−1/t) − 1 only when read as exp10 — flagged as a minor UNK in §5.

The **25 ms** number itself is not hard-coded as a constant; it arrives via the “sr”-named float param (`[r15+0xe8]`, default from ctor1 was 32766.0f→31999f region 0x46fffe00 — i.e. the smoothing-ms field has a large default; the actual 25 ms default is set by the parameter layer / preset, not by code).

## 5. The pan law — DECODED (FUN_1800d9b20, the per-slot pan→L/R converter)

**FUN_1800d9b20** (`0x1800d9b20..0x1800d9e80`, immediately before the module-table record-A ctor 0x1800d9e80 — same compilation unit) is the stereomixer3 pan-law converter. It iterates the slot vector (`rcx+rdi`, `rdi += 0x30` per slot, `i < [this+0xc]`) that FUN_1800d8380 filled with the X/Y/Z of the “gain” and “pan” parameters, and for each slot computes the pair of L/R gain arguments that it pushes to the engine via `call [[engine_vtbl]+0xa0](engine, id, ch(0|1), float)` at 0x1800d9ce7 / 0x1800d9d1f — i.e. it is the writer that feeds the mixer’s L/R target arrays.

Constants held in registers (VA = source):
- `xmm7 = 0.5f` ← `0x180412058` @0x1800d9b5a
- `xmm8 = 0.11512925465` ← `0x1804121f8` @0x1800d9b67
- `xmm9 = 1.0f` ← `0x18041213c` @0x1800d9b75
- `xmm10 = −1.0f` ← `0x18041280c` @0x1800d9b83
- `0xbf800000 = −1.0f` stack immediates @0x1800d9c7a / 0x1800d9c8c
- per-slot source floats: gain X = `[slot+0]` (0x1800d9bd1), gain Y = `[slot+4]` (min bound), override port value `[slot+0x20]→[rax]` or fallback `[slot+0x28]`; pan X = `[slot+0]` @0x1800d9c27, pan Y = `[slot+4]` @0x1800d9c30, pan override `[slot+0x20]` / fallback `[slot+0x28]` (same layout in the pan vector `[this+0x28]`).

**Slot A (per-slot dB→linear gain):**
```asm
0x1800d9bc5  xmm1 = slotval([slot+0x20]?:[slot+0x28] , slotX)   ; pick current-or-default
0x1800d9bf3  xmm0 = min(picked, slotY)                           ; clamp to Y (max)
0x1800d9bf9  cvtps2pd; mulsd xmm1, 0.11512925465                 ; × ln(10)/20 = dB→linear exponent
0x1800d9c01  cvtpd2ps; call 0x180352350                           ; == powf(10, v·ln10/20) → xmm4
; 0x180352350 = powf(10,x): movd edx,xmm0; |x| compare; cvtss2sd; ×[rip+0xc2a5f](ln10) etc. — 3 callers total,
; incl. 0x180066774/0x1800667c0 which use the same 10^x idiom.
; ⇒ linear gain g = 10^(dB/20)  (dB = clamped slot value, X or override [slot+0x20])
```

**Slot B (pan → L/R pair, pure linear law with 0.5 weighting):**
```asm
; xmm2 = panX ([pan_slot+0]), xmm3 = panY ([pan_slot+4]) (clamp bound), xmm1 = 0.5, xmm4 = g (linear gain)
0x1800d9c1b  xmm1cur = ([slot+0x20]?:[slot+0x28])                ; current pan value (or default)
0x1800d9c4e  xmm0 = min(xmm1cur, xmm3)                            ; pan p clamped to [?, panY]
0x1800d9c56  mulss xmm0, xmm7                                    ; p·0.5
0x1800d9c5a  subss xmm1, xmm0                                    ; xmm1 = 0.5 − 0.5·p   ← LEFT weight
0x1800d9c5e  mulss xmm1, xmm4                                    ; × linear gain g      → L
0x1800d9c73…0x1800d9cb8  q = max(min(p,panY), −1.0)              ; clamp p ≥ −1 (min vs −1.0f)
0x1800d9cca  addss xmm6, xmm9                                    ; q + 1.0
0x1800d9cd4  mulss xmm6, xmm7                                   ; ×0.5                 → (q+1)/2
0x1800d9cde  mulss xmm6, xmm4                                    ; × g                  → R
0x1800d9ce7  call [[engine]+0xa0](engine, [this+8], 0, clamp(L,≤1))   ; push LEFT  gain (ch 0)
0x1800d9d1f  call [[engine]+0xa0](engine, [this+8], 1, clamp(R,≤1))   ; push RIGHT gain (ch 1)
; clamp: `minss xmm0, xmm9` (≤1.0) applied to each pushed value (0x1800d9ccf / 0x1800d9d14)
```

**Decoded pan law (pan p ∈ [−1,+1], clamped to [−1, panY] before use):**

```
g  = 10^(dB/20)                         // dB = per-slot gain value (X or port override), clamped to Y
L  = (0.5 − 0.5·p) · g  = 0.5·(1 − p)·g
R  = (0.5 + 0.5·p)·g  = 0.5·(1 + p)·g   // p = clamped pan position; both clamped ≤ 1.0 on push
```

i.e. a **linear (constant-sum, −6 dB-at-center) pan law**: `L = 0.5(1−p)·g`, `R = 0.5(1+p)·g`, gain `g = 10^(dB/20)`. This is the classical “0.5·(1∓p)” law — explicitly NOT equal-power (no sin/cos/π/4, no √((1±p)/2), no table), confirmed by the exhaustive negative scans below.

**Distinguishing marks for re-verification:** constants 0.5 @0x180412058 (0x1800d9b5a), 1.0 @0x18041213c (0x1800d9b75), −1.0 @0x18041280c (0x1800d9b83), 0.11512925465 @0x1804121f8 (0x1800d9b67), 0xbf800000 immediates (0x1800d9c7a/0x1800d9c8c), powf10 helper 0x180352350 (0x1800d9c05), engine push `call [r10+0xa0]` (0x1800d9ce7/0x1800d9d1f), slot stride 0x30, spin-locks on [this+0x70]/[this+0x78] (0x1800d9ba0/0x1800d9d76).

### Negative evidence (what the law is NOT)
- **sin/cos equal-power (p+1)·π/4**: zero references to π/4 in plugin code — the only π/4 refs (0x180350825…0x18035352e) are CRT sinf/cosf internals.
- **√((1±p)/2)**: zero refs to 0.70710678; no sqrtss/sqrtsd in the family.
- **table lookup**: pan table `0x180416360` (16 floats 0.3408..0.4209) has ZERO references anywhere (byte-level rip-relative scan of all of .text + pointer scan of .rdata/.data) — it belongs to some other constant blob (note: 0x41205c..0x41206c holds an unrelated run 0.5, 0.5236, 0.5412, 0.55, 0.5613, 0.60).
- no dB math inside the process function itself — dB→linear happens here in FUN_1800d9b20 via 10^(dB/20).

## 6. dB→linear gain (per-voice gain application)

Decoded in FUN_1800d9b20 slot A: per-slot dB value (gain parameter X, or port override via `[slot+0x20]` with fallback default `[slot+0x28]`), clamped to the Y bound, then
`g = 10^(dB/20)` — exponent formed as `(double)dB × 0.11512925465` (VA 0x1804121f8; 0.11512925465 = ln(10)/20) and evaluated by **powf10 helper 0x180352350** at 0x1800d9c05 (helper internals: `movd edx,xmm0; btr edx,0x1f; |x| range-check; cvtss2sd; ×ln10 …` — i.e. the CRT `pow`-family 10^x specialization; only 3 call sites in the whole binary: 0x180066774, 0x1800667c0, 0x1800d9c05). The linear gain g multiplies both pan weights (see §5).

## 7. Where the L/R sums go (output ports)

- ctor2 0x1800d7400 registers the mixer’s ports by name **“left”** (string @ 0x1804083d0-region, registration at 0x1800d7af9 via `call [[host]]+8`, list anchor rbx+0x70) and **“right”** (5-byte string @0x180408478-region “right”, registration at 0x1800d7b7b, list anchor rbx+0x88).
- FUN_1800d7f10 writes finished samples: left → `[[this+0x80] + r8]`, right → `[[this+0x98] + r8]` (0x1800d8095, 0x1800d80c2; second pass +4 at 0x1800d81c6/0x1800d81eb).

## 8. Smoothing summary (constants + VAs)

| constant | VA | role |
|---|---|---|
| 0.001f | 0x180411eb4 | ms→seconds (×0.001) and clamp floor |
| 0.001 (double) | 0x1804120e0 | clamp floor compare |
| −2.3025850929999998 (−ln10) | 0x180412850 | numerator of −ln10/t |
| 1.0 (double) | 0x180412328 | final −1 |
| 1.0f | 0x18041213c | declick fade clamp (min(fade,1)) |
| −0.0f | 0x180412c90 | sign flip helper (clip negation) |
| 2.0f | 0x1804123b0 | pass-2 fold (2−x) in voice DSP 0x1800e8b50 |
| 0.5f | 0x180412058 | pan-law half-weight (FUN_1800d9b20 @0x1800d9b5a) |
| 1.0f | 0x18041213c | pan clamp (p+1), push clamp ≤1 (FUN_1800d9b20) |
| −1.0f | 0x18041280c / 0xbf800000 imm | pan clamp ≥ −1 (FUN_1800d9b20) |
| 0.11512925465 (ln10/20) | 0x1804121f8 | dB→linear exponent (FUN_1800d9b20 @0x1800d9b67) |
| powf10 helper | 0x180352350 | 10^x evaluation (call @0x1800d9c05) |

## 9. Artifact files (all under analysis_r25/)

`step1_vtable.py/step1_out.txt` (module table dump) · `step2_scan.py/step2_out.txt` (slot scans) ·
`step2b_strings.py` (pan-table floats, id-blobs) · `step4_worker.txt`, `step4_ctor.txt` (first looks) ·
`worker_full.txt`, `worker_calls.txt` (full worker disasm + call map) · `step5*`, `step6*`, `step7*` (xrefs, pow/log callers, module table) · `fun_d8380_full.txt` (full param-refresh), `fun_d8230.txt` (ctor1), `ctor2_arrays.txt`, `ctor2_end.txt`, `ctor2_half2.txt`, `fun_ctor2_tail2.txt`, `fun_d8fc0.txt`, `fun_d8fc0_tail.txt` (ctor chain) · `fun_mfn1.txt`, `fun_d7f10_rest.txt` (**full process disasm**) · `fun_d9b20.txt`, `step22*.txt` (**pan-law converter disasm**) · `fun_e8b50.txt` (voice DSP) · `step13b_consts.txt`, `step15_scan.txt`, `step16*` (negative-evidence scans) · `step18b.txt` (string decoding).

## 10. Bottom line

- Process VA: **0x1800d7f10** (call site 0x180104cbd in worker 0x180103a10, object engine+0x1c4b0).
- Pan law (decoded, FUN_1800d9b20): **L = 0.5·(1−p)·g, R = 0.5·(1+p)·g, g = 10^(dB/20)** — linear/constant-sum law, −6.02 dB at center; NOT equal-power (no sin/cos/π/4, no √((1±p)/2)), NOT table-based (0x180416360 unreferenced).
- Smoothing: one-pole `val ← val + (val−target)·coef` with `coef = 10^(−1/(rate·ms/1000)) − 1` (the ~25 ms parameter smoothing; 25 ms arrives via the “sr”-named param, not hard-coded).
- Mix: per-input L/R gains ramped per sample, samples accumulated in doubles, sum clamped to ±clipvalue and scaled by the declick fade, written to the “left”/“right” ports.
- Remaining UNKs (minor): exact identification of engine methods +8 (rate source) and +0xa0 (target push consumer), and the “sr”-string param’s default value wiring — none affect the decoded formulas.
