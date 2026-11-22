# R25 — Tracker decimation FIR coefficients + per-rate-class detector constants (OBS dump)

**Target:** `/home/z/my-project/octavox-work/original/Ox.vst3` (Eventide Octavox VST3, PE32+ x86-64, image base `0x180000000`)
**SHA-256 (verified before + after):** `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
**Method:** static reverse engineering only — Python 3 + capstone 5.0.7 + pefile-style manual PE parsing. The binary was **read and disassembled only; never executed, never modified.**

**Scripts/artifacts (all in this directory):**

| file | purpose |
|---|---|
| `r25_fir_dump.py` | full disassembly of `FUN_1800E10B0` (0x1800E10B0..0x1800E14D0), ctor region 0x1800E0360..0x1800E1FF0 scan, dispatch-table dump, `.rdata` table dumps |
| `r25_fir_dump.txt` | raw annotated disassembly output (6500 lines) |
| `r25_extract_tables.py` | precision re-extraction of the three coefficient blocks + ctor tracker-init constants + analysis |
| `r25_fir_tables.json` | machine-readable dump of everything below |

---

## 1. Executive summary — the mechanism

**The FIR coefficients are NOT in `.rdata`. They are inline `float32` immediates baked into the code of the `m01` setparam function `FUN_1800E10B0`, inside its case-5 handler (sample-rate parameter), stored directly into the live tracker object via `mov dword ptr [rbx+disp32], imm32` (`rbx` = engine `dsp`).**

Three class blocks exist (one per host-SR class):

| class | SR range (OBS from `comiss` branches) | code block | cadence `T+0x5F14` | fwd taps `T+0x5F04` | bwd taps `T+0x5F08` | coefficients written |
|---|---|---|---|---|---|---|
| **A** | `SR < 50000.0`  | 0x1800E1151..0x1800E11DC | **2** | **5** | **6** | **6**  (`c[0..5]`) |
| **B** | `50000.0 ≤ SR < 100000.0` | 0x1800E11EA..0x1800E129A | **4** | **10** | **11** | **11** (`c[0..10]`) |
| **C** | `SR ≥ 100000.0` | 0x1800E129B..0x1800E1395 | **8** | **20** | **21** | **21** (`c[0..20]`) |

Every store address is `engine + disp` where `engine+0xA3C8 == T+0x5EB0` (tracker T = engine+0x4518, known from prior rounds):

- coefficient array `c[0..20]` = **T+0x5EB0 .. T+0x5F00** = engine+0xA3C8..0xA418 — the destination of the stores
- fwd/bwd tap counts = **T+0x5F04 / T+0x5F08** = engine+0xA41C/0xA420
- cadence reload = **T+0x5F14** = engine+0xA42C
- sample rate = **T+0x600C** = engine+0xA524 (`movss [rbx+0xa524], xmm0` at 0x1800E1143)

**Correction to the prior round's model:** "rate-class detector constants at `dsp+0xA3CC..0xA41C` (per-class coefficient tables)" are **not a separate source table** — those engine-relative displacements are the *destination*: the very same tracker fields seen through the engine base register (`dsp+0xA3CC = T+0x5EB4 = c[1]`, …, `dsp+0xA41C = T+0x5F04`). The prior-round note "m01 case-5 stores at 0x1800E1151 / 0x1800E11EA / 0x1800E129B" is confirmed exactly — those are the first instruction of each of the three class blocks.

The only `.rdata` data used by case 5 are the two **SR threshold constants** (MSVC CRT float pool):

| VA | raw u32 | float32 | used by |
|---|---|---|---|
| `0x180412770` | `0x47435000` | **50000.0** | `comiss` at 0x1800E113C (case 5) and 0x1800E0649 (ctor) |
| `0x180412774` | `0x47C35000` | **100000.0** | `comiss` at 0x1800E11DD (case 5) and 0x1800E062F (ctor) |

---

## 2. `m01` setparam `FUN_1800E10B0` — structure (OBS)

```
1800E10B0  push rbx ; sub rsp,0x40 ; movaps [rsp+0x30],xmm6
1800E10BB  mov  rbx, rcx                     ; rbx = engine (dsp)
1800E10BE  cmp  edx, 0x11                   ; param id 0..17
1800E10C1  ja   0x1800e14b3                 ; default: tail call [vtable+0x18]
1800E10C7  lea  rcx, [rip-0xe10ce]          ; rcx = 0x180000000 (image base)
1800E10D0  mov  r9d, [rcx + rax*4 + 0xe14d0]; jump table @ 0x1800E14D0, 4-byte offsets
1800E10D8  add  r9, rcx ; jmp r9             ;   relative to image base
```

Dispatch table at **0x1800E14D0** (18 dword entries, image-base-relative; OBS bytes):
`case0→0x1800E10DE, case1→0x1800E10F2, case2→0x1800E110F, case3→default(0x1800E14B3), case4→0x1800E1123, case5→0x1800E1137, case6→0x1800E1396, case7..13→default, case14→0x1800E13D5, case15→0x1800E13E9, case16→0x1800E13FD, case17→0x1800E1460`.

**Case 5 handler (SR parameter) — full disassembly (OBS):**

```asm
1800E1137  movss xmm0, dword ptr [r8]                    ; SR value (param 5)
1800E113C  comiss xmm0, dword ptr [rip+0x33162d]         ; -> .rdata 0x180412770 = 50000.0f
1800E1143  movss dword ptr [rbx+0xa524], xmm0            ; eng+0xA524 == T+0x600C: rate store
1800E114B  jae  0x1800e11dd                              ; SR >= 50000 -> class B/C
; ---------------- CLASS A: SR < 50000 ----------------
1800E1151  mov   dword ptr [rbx+0xa42c], 2               ; T+0x5F14 cadence reload = 2
1800E115B  xorps xmm0, xmm0
1800E115E  mov   dword ptr [rbx+0xa41c], 5               ; T+0x5F04 fwd taps = 5
1800E1168  xor   eax, eax
1800E116A  mov   dword ptr [rbx+0xa420], 6               ; T+0x5F08 bwd taps = 6
1800E1174  movups xmmword ptr [rbx+0xa3e0], xmm0        ; zero c[6..9]
1800E117B  movups xmmword ptr [rbx+0xa3f0], xmm0        ; zero c[10..13]
1800E1182  movups xmmword ptr [rbx+0xa400], xmm0        ; zero c[14..17]
1800E1189  mov   qword ptr [rbx+0xa410], rax            ; zero c[18..19]
1800E1190  mov   dword ptr [rbx+0xa418], eax            ; zero c[20]
1800E1196  mov   dword ptr [rbx+0xa3c8], 0x3c7faa00     ; c[0] = 0.015604496
1800E11A0  mov   dword ptr [rbx+0xa3cc], 0x3d44c140     ; c[1] = 0.0480358601
1800E11AA  mov   dword ptr [rbx+0xa3d0], 0x3db18f00     ; c[2] = 0.0866985321
1800E11B4  mov   dword ptr [rbx+0xa3d4], 0x3dfaf420     ; c[3] = 0.122535944
1800E11BE  mov   dword ptr [rbx+0xa3d8], 0x3e170420     ; c[4] = 0.147476673
1800E11C8  mov   dword ptr [rbx+0xa3dc], 0x3e203ab8     ; c[5] = 0.156473994
1800E11D2..C3   epilogue/ret
; ---------------- CLASS B: 50000 <= SR < 100000 ----------------
1800E11DD  comiss xmm0, dword ptr [rip+0x331590]         ; -> .rdata 0x180412774 = 100000.0f
1800E11E4  jae  0x1800e129b                              ; SR >= 100000 -> class C
1800E11EA  mov   dword ptr [rbx+0xa42c], 4               ; cadence = 4
1800E11F7  mov   dword ptr [rbx+0xa41c], 0xa             ; fwd taps = 10
1800E1203  mov   dword ptr [rbx+0xa420], 0xb             ; bwd taps = 11
1800E120D  movups [rbx+0xa3f4], xmm0 ; 0xa404 ... qword [rbx+0xa414], rax   ; zero c[11..20]
1800E1222..1286  11 x mov dword ptr [rbx+0xa3c8+4k], imm32               ; c[0..10]
1800E1290..C3    epilogue/ret
; ---------------- CLASS C: SR >= 100000 ----------------
1800E129B  mov   dword ptr [rbx+0xa42c], 8               ; cadence = 8
1800E12A5  mov   dword ptr [rbx+0xa41c], 0x14            ; fwd taps = 20
1800E12AF  mov   dword ptr [rbx+0xa420], 0x15            ; bwd taps = 21
1800E12B9..1381  21 x mov dword ptr [rbx+0xa3c8+4k], imm32              ; c[0..20]
1800E138B..C3    epilogue/ret
```

---

## 3. THE COEFFICIENT TABLES (OBS — bytes read from .text immediates)

Address convention: `code VA` = instruction address in `.text`; `eng+disp` = store displacement off `rbx` (engine); `T+0x…` = tracker-relative address of the array slot.

### Class A — `SR < 50000` (e.g. 44.1/48 kHz) — 6 coefficients, taps 5/6, cadence 2

| i | eng disp | T addr | code VA | bit pattern (LE u32) | float32 |
|---|---|---|---|---|---|
| 0 | 0xA3C8 | T+0x5EB0 | 0x1800E1196 | `0x3C7FAA00` | 0.015604496 |
| 1 | 0xA3CC | T+0x5EB4 | 0x1800E11A0 | `0x3D44C140` | 0.0480358601 |
| 2 | 0xA3D0 | T+0x5EB8 | 0x1800E11AA | `0x3DB18F00` | 0.0866985321 |
| 3 | 0xA3D4 | T+0x5EBC | 0x1800E11B4 | `0x3DFAF420` | 0.122535944 |
| 4 | 0xA3D8 | T+0x5EC0 | 0x1800E11BE | `0x3E170420` | 0.147476673 |
| 5 | 0xA3DC | T+0x5EC4 | 0x1800E11C8 | `0x3E203AB8` | 0.156473994 |

Full precision (repr): `0.015604496002197266, 0.04803586006164551, 0.08669853210449219, 0.12253594398498535, 0.1474766731262207, 0.15647399425506592`. Unused tail `c[6..20]` explicitly zeroed at 0x1800E1174..0x1800E1190.

### Class B — `50000 ≤ SR < 100000` (e.g. 88.2/96 kHz) — 11 coefficients, taps 10/11, cadence 4

| i | eng disp | T addr | code VA | bit pattern | float32 |
|---|---|---|---|---|---|
| 0 | 0xA3C8 | T+0x5EB0 | 0x1800E1222 | `0x3BFFAA00` | 0.007802248 |
| 1 | 0xA3CC | T+0x5EB4 | 0x1800E122C | `0x3C795520` | 0.0152180493 |
| 2 | 0xA3D0 | T+0x5EB8 | 0x1800E1236 | `0x3CC4C140` | 0.02401793 |
| 3 | 0xA3D4 | T+0x5EBC | 0x1800E1240 | `0x3D09811B` | 0.0335703902 |
| 4 | 0xA3D8 | T+0x5EC0 | 0x1800E124A | `0x3D318F00` | 0.0433492661 |
| 5 | 0xA3DC | T+0x5EC4 | 0x1800E1254 | `0x3D5829A9` | 0.0527741052 |
| 6 | 0xA3E0 | T+0x5EC8 | 0x1800E125E | `0x3D7AF420` | 0.061267972 |
| 7 | 0xA3E4 | T+0x5ECC | 0x1800E1268 | `0x3D8C060D` | 0.0683709159 |
| 8 | 0xA3E8 | T+0x5ED0 | 0x1800E1272 | `0x3D970420` | 0.0737383366 |
| 9 | 0xA3EC | T+0x5ED4 | 0x1800E127C | `0x3D9DE343` | 0.0770936236 |
| 10 | 0xA3F0 | T+0x5ED8 | 0x1800E1286 | `0x3DA03AB8` | 0.0782369971 |

Unused tail `c[11..20]` zeroed at 0x1800E120D..0x1800E121B.

### Class C — `SR ≥ 100000` (e.g. 176.4/192 kHz) — 21 coefficients, taps 20/21, cadence 8

| i | eng disp | T addr | code VA | bit pattern | float32 |
|---|---|---|---|---|---|
| 0 | 0xA3C8 | T+0x5EB0 | 0x1800E12B9 | `0x3B7FAA00` | 0.003901124 |
| 1 | 0xA3CC | T+0x5EB4 | 0x1800E12C3 | `0x3BB8F340` | 0.00564423203 |
| 2 | 0xA3D0 | T+0x5EB8 | 0x1800E12CD | `0x3BF95520` | 0.00760902464 |
| 3 | 0xA3D4 | T+0x5EBC | 0x1800E12D7 | `0x3C1FA8B6` | 0.00974481367 |
| 4 | 0xA3D8 | T+0x5EC0 | 0x1800E12E1 | `0x3C44C140` | 0.012008965 |
| 5 | 0xA3DC | T+0x5EC4 | 0x1800E12EB | `0x3C6B60B9` | 0.0143663222 |
| 6 | 0xA3E0 | T+0x5EC8 | 0x1800E12F5 | `0x3C89811B` | 0.0167851951 |
| 7 | 0xA3E4 | T+0x5ECC | 0x1800E12FF | `0x3C9D8EC0` | 0.0192331076 |
| 8 | 0xA3E8 | T+0x5ED0 | 0x1800E1309 | `0x3CB18F00` | 0.021674633 |
| 9 | 0xA3EC | T+0x5ED4 | 0x1800E1313 | `0x3CC53279` | 0.0240719188 |
| 10 | 0xA3F0 | T+0x5ED8 | 0x1800E131D | `0x3CD829A9` | 0.0263870526 |
| 11 | 0xA3F4 | T+0x5EDC | 0x1800E1327 | `0x3CEA2A89` | 0.0285847355 |
| 12 | 0xA3F8 | T+0x5EE0 | 0x1800E1331 | `0x3CFAF420` | 0.030633986 |
| 13 | 0xA3FC | T+0x5EE4 | 0x1800E133B | `0x3D05278E` | 0.0325084254 |
| 14 | 0xA400 | T+0x5EE8 | 0x1800E1345 | `0x3D0C060D` | 0.034185458 |
| 15 | 0xA404 | T+0x5EEC | 0x1800E134F | `0x3D120099` | 0.0356451012 |
| 16 | 0xA408 | T+0x5EF0 | 0x1800E1359 | `0x3D170420` | 0.0368691683 |
| 17 | 0xA40C | T+0x5EF4 | 0x1800E1363 | `0x3D1AFF56` | 0.0378411636 |
| 18 | 0xA410 | T+0x5EF8 | 0x1800E136D | `0x3D1DE343` | 0.0385468118 |
| 19 | 0xA414 | T+0x5EFC | 0x1800E1377 | `0x3D1FA436` | 0.0389749631 |
| 20 | 0xA418 | T+0x5F00 | 0x1800E1381 | `0x3D203AB8` | 0.0391184986 |

Int constants of the same handler (OBS):

| field | T addr | eng disp | code VA (class A / B / C) | value (A/B/C) |
|---|---|---|---|---|
| cadence reload | T+0x5F14 | 0xA42C | 0x1800E1151 / 0x1800E11EA / 0x1800E129B | 2 / 4 / 8 |
| fwd tap count | T+0x5F04 | 0xA41C | 0x1800E115E / 0x1800E11F7 / 0x1800E12A5 | 5 / 10 / 20 |
| bwd tap count | T+0x5F08 | 0xA420 | 0x1800E116A / 0x1800E1203 / 0x1800E12AF | 6 / 11 / 21 |
| sample rate | T+0x600C | 0xA524 | 0x1800E1143 (single store, all classes) | SR float |

---

## 4. What the coefficients are (DER — analysis, not bytes)

* **Same filter, three lengths.** Bit-level: `classB[2k] == classA[k] / 2` exactly (e.g. `0x3C7FAA00 → 0x3BFFAA00`), and `classC[2k] == classB[k] / 2` exactly (e.g. `0x3BFFAA00 → 0x3B7FAA00`). The three tables are the *same windowed-sinc prototype* sampled at 1×/2×/4× tap density with amplitude ÷2 per class — i.e. the same absolute-Hz cutoff implemented at the higher host rate (tap count ≈ SR-proportional: 11/21/41 total symmetric taps).
* **Shape:** monotonically increasing to the center `c[N-1]` → the stored array is one half of a **symmetric FIR** kernel `[c[N-1] … c[0] | c[0] … c[N-2]]` of `2N−1` taps (11 / 21 / 41), consistent with fwd/bwd counts `N−1` / `N` (5/6, 10/11, 20/21).
* **DC gain** of that symmetric kernel: A ≈ **0.997177**, B ≈ **0.992643**, C ≈ **0.989551** (slightly < 1; windowed-sinc design).
* **Cutoff (assuming 48/96/192 kHz class representatives):** −3 dB ≈ **1.6–1.7 kHz**, −6 dB ≈ **2.2–2.3 kHz**, constant across classes in absolute Hz (normalized cutoff halves per class: 0.045 / 0.0237 / 0.0122 × SR). Pure-sinc fit gives sinc argument ≈ 0.189 / 0.094 / 0.047 per tap (ratio exactly 2 per class), residual ≤ 0.6 % shows a mild window term.
* Prior-round DER stands: with 2 input samples pushed per tracker call, cadence {2,4,8} ⇒ detection-ring advance rate = SR/(2·cadence) ≈ 12 kHz for all classes.

---

## 5. `.rdata` constants referenced by m01 / case 5 (OBS)

Case 5 references **only** the two threshold floats (part of the MSVC CRT constant pool at 0x180412770):

```
0x180412770  0x47435000  50000.0     ; comiss 0x1800E113C (case 5), 0x1800E0649 (ctor)
0x180412774  0x47C35000  100000.0    ; comiss 0x1800E11DD (case 5), 0x1800E062F (ctor)
0x180412778  0x47C35080  100001.0    ; (pool neighbours, unreferenced by these fns)
0x18041277C  0x47C35100  100002.0
0x180412780  0x47C35180  100003.0
0x180412784  0x47C35200  100004.0
0x180412788  0x47C35280  100005.0
0x18041278C  0x4AFFFFFE  8388607.0   ; CRT pool continues (8388608, 1e7, 1e9, FLT_MAX, -0.125, -1.0 …)
```

Other `.rdata` constants referenced by *other* cases of the same `FUN_1800E10B0` (for completeness, OBS): `0x18041213C = 1.0f` (case 6, `divss`), doubles `0x1804120C0` (cases 14/17), `0x1804121A8` / `0x180412818` (case 17), xmm mask `0x180412C90` (case 14). **No FIR coefficient data exists in `.rdata`.**

---

## 6. Engine ctor `FUN_1800E0360` — tracker construction (OBS)

`lea rdi, [r15+0x4518]` at **0x1800E0D1F** confirms T = engine+0x4518 inside the ctor. Relevant tracker front-end init (all OBS):

```
1800E0D1F  lea  rdi, [r15 + 0x4518]                   ; rdi = T (tracker)
1800E0D51  mov  dword ptr [rdi+0x5fe0], 0xfff           ; T+0x5FE0 = 4095
1800E0D5B  mov  dword ptr [rdi+0x6008], 0x45000000     ; T+0x6008 = 2048.0
1800E0D65  mov  byte  ptr [rdi+0x5fe8], 1              ; T+0x5FE8 = 1
1800E0D6E  mov  r8d, 0x5eb0 ; mov rcx, rdi ; call memset ; memset(T, 0, 0x5EB0)
           ; NB: memset covers T+0x0..T+0x5EAF — the coefficient array T+0x5EB0..T+0x5F03
           ;     is NOT covered; it is written later by setparam case 5 (host sets SR first)
1800E0D86  mov  dword ptr [rdi+0x6004], 1               ; detection-ring index = 1
1800E0DA5  mov  qword ptr [rdi+0x5f0c], 0               ; LP ring index + cadence ctr = 0
1800E0DAC  mov  qword ptr [rdi+0x5f24], 0              ; peak = 0
1800E0DCF  mov  dword ptr [rdi+0x5ff4], 0x3c000000     ; T+0x5FF4 = 0.0078125  (1/128)
1800E0E03  mov  dword ptr [rdi+0x6000], 0x3d924925     ; T+0x6000 = 0.0714285746 (1/14)
1800E0E14  mov  dword ptr [rdi+0x5f18], 0x3f7e0000     ; T+0x5F18 = 0.9921875   (energy leak)
1800E0E1E  mov  dword ptr [rdi+0x5f1c], 0x3c000000     ; T+0x5F1C = 0.0078125   (1/128)
1800E0E28  mov  dword ptr [rdi+0x5f20], 0x3f7e0002     ; T+0x5F20 = 0.992187619 (peak decay)
1800E0E32  mov  dword ptr [rdi+0x5fc], 0xbe000000      ; T+0x5FC  = -0.125
1800E0E3C  movss xmm0, [rdi+0x6008]                    ; 2048.0
1800E0E44  mulss xmm0, dword ptr [rip+…]               ; -> .rdata 0x18041268C = 50.0
1800E0E4C  movss dword ptr [rdi+0x5fd4], xmm0          ; T+0x5FD4 = 2048.0*50.0 = 102400.0
1800E0E94  mov  dword ptr [rdi+0x6070], 0x3f800000     ; T+0x6070 = 1.0
```

The ctor contains **no** stores into `engine+0xA3C8..0xA418` (the whole 0x1800E0360..0x1800E1FF0 scan found all 47 immediate stores to the class area inside the setparam case-5 blocks only).

Related ctor rate-class logic (the "0x1000/0x2000/0x4000" of earlier rounds — this is the *engine analysis-window* class, not the FIR):

```
1800E062F  comiss xmm0, [0x180412774]   ; SR vs 100000.0
1800E0638    mov eax, 0x4000             ; SR >= 100000 -> 16384
1800E063F    mov eax, 0x1000 ; mov ecx, 0x2000
1800E0649  comiss xmm0, [0x180412770]    ; SR vs 50000.0
1800E0650    cmovae eax, ecx             ; 50000 <= SR < 100000 -> 8192, else 4096
1800E0653  mov [r15+0x409c], eax         ; engine analysis window size
```

---

## 7. Uniqueness of the writer (OBS)

A byte-level scan of the **entire `.text`** (0x180001000..0x18037D000) for `mov [reg+disp32], imm32` (`C7 /0`, incl. REX.B forms) with disp in `[0xA3C0..0xA440] ∪ {0xA524}` found **exactly 47 sites — all inside `FUN_1800E10B0` case 5 (0x1800E1151..0x1800E1381)**. No other function initializes the coefficient array, tap counts or cadence via immediate stores. (The SR store at 0xA524 is a `movss` at 0x1800E1143, seen in the disassembly.)

---

## 8. Classification

* Everything in §1–§7 marked OBS = bytes read directly from the binary (file reads + capstone disassembly; no execution).
* §4 (kernel symmetry, DC gain, cutoff, 12 kHz ring rate, cross-class design relationship) = **DER** (computed interpretation of the OBS values).
* Prior-round claim "T+0x5F18 energy leak = 0.99182129" is superseded by the ctor immediate `0x3F7E0000 = 0.9921875` (OBS).
* Prior-round framing of "dsp+0xA3CC..0xA41C per-class coefficient tables" as a *source* is corrected: those addresses are the destination tracker fields (engine-relative view), and the values are inline code immediates.

## 9. Not recovered / caveats (honesty)

1. **The FIR summation loop itself** (which `c[i]` multiplies which `x[n±k]`, i.e. the exact fold of fwd/bwd taps) lives in the tracker run function and was **not re-disassembled this round**; the symmetric-kernel reconstruction in §4 is DER from the monotone shape + tap counts, consistent with prior rounds' pseudocode.
2. DC-gain / cutoff numbers depend on the assumed kernel layout and class-SR representatives (48/96/192 kHz); thresholds are OBS, the representative rates are assumption.
3. The ctor `memset(T,0,0x5EB0)` stops just below the coefficient array, so `c[]` holds uninitialized memory until the host sets the sample rate (case 5). No code path writing `c[]` *before* case 5 was found, but a non-immediate writer (e.g. SSE copy) outside the scanned functions can't be 100 % excluded by the C7-immediate scan alone; the full disasm of ctor+setparam shows none.
4. Only `FUN_1800E10B0`, `FUN_1800E0360` and the 0x1800E14D0 table were disassembled this round; other functions were covered only by the targeted byte scan of §7.
