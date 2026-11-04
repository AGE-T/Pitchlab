# OCTAVOX ROUND 10 FINDINGS

**Validate the round-9 mathematics; resolve the tracker read-outs 12/13 (`m02` params `0xc`/`0xd`)**

Target `F:\VST\OV_DIS\Ox.vst3` (SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`).
Static analysis only: fresh capstone decodes (`r10_decode.py`, `r10_m02.py`, `r10_scan2.py`,
`r10_dispatch.py`, `r10_wrapper.py` — all read-only against the shipped image bytes), one REA/Ghidra
verification decompile (`FUN_1800E1520`, evidence `ev_d6c819335966ed7e731a698a5d17702c686de6f68dae2353003eb95f24b8048d`,
provider Ghidra 12.1.4, target digest re-verified), and one independent numerical verification script
(`r10_verify.py`) with exact float32 constants decoded from the target's bit patterns.
**The target was never executed and never modified.** Labels: **OBS** observed · **DER** derived ·
**UNK** open.

---

## 1. Verdict

| question | answer |
|---|---|
| Are the round-9 example values 50.13 ¢ (depth 50) / 201.68 ¢ (depth 200) correct? | **NO — calculation errors.** The exact values are **49.939 ¢** and **199.923 ¢** (target-exact arithmetic, OBS constants + DER evaluation). |
| Is the underlying law "the modulation is `walk·depth` cents" correct? | **Yes, within the quartic's own fit error**: `|cents − walk·depth| ≤ 0.101 ¢` over the whole shipped range (`F ∈ [0,1]`, depth `∈ [0,200]`). The `ratio/16` conclusion of round 9 is **unchanged** — re-derived independently here. *(Round 11 corrected this row's earlier "always one-sided upward" tail: the **modulation direction** is upward — `4·P(f) ≥ 1` raises the pitch above the base ratio — but the **signed approximation error** is negative across the bulk of the domain; see §8.)* |
| Where did 50.13 / 201.68 come from? | **Neither a measurement, nor any formula, nor any evidence artifact.** They appear only in the round-9 documentation itself; they are internally inconsistent with round 9's own `4·P` column and are not reproduced by any candidate formula (§2.3). Classification: **DER-labeled arithmetic that was never actually computed from the stated inputs** — a documentation calculation error, now corrected. |
| Is the random modulation range `F ∈ [0,1)` correct? | **Refined.** Arithmetic per-sample bound: `noise ∈ [−(1+2^-23), +1.0]`, so `F ∈ [0, 1+2^-23]` (the abs stage *can* exceed 1 by `2^-23`). Full-period orbit computation (default config): the emitted noise spans **exactly `[−1.0, +1.0]`**, so the orbit-actual walk is `F ∈ [0, 1]` — **`1.0` is attainable**, `[0,1)` was wrong at the endpoint. The `2^-23` excess has no audible effect on the output pitch (§3.3; round 11: bit-exact at depths 50/200, at worst a one-`P`-ulp cents change of `2.0e-4 ¢` at 11 of 201 integer depths). |
| Is the noise scale `2^-23`? | **No — `0x34000001 = 2^-23·(1+2^-23)`** (one float32 ulp above `2^-23`). This is what allows `|noise|` to exceed 1 arithmetically. |
| Does an additional correction factor affect the modulation cents? | **No.** The modulation's contribution to the output ratio (relative to `ratio·correction`) is exactly `4·float32(P(f))`, independent of the correction. But the **total** output ratio is `ratio · correction · 4·P(f)` — three distinct factors that must not be conflated (§4.6). |
| Tracker outputs 12/13 (`m02` params `0xc`/`0xd`): what is the exact data path? | **Fully established this round** (§5): upstream writers, both handlers' conversions with verified constants, the sign-inverted twin semantics, and the virtual-only consumer surface. The round-8 "double conversion" defect prediction stands as **DER** (runtime-unverified). |

---

## 2. The numerical verification (script: `r10_verify.py`, results: `r10_verify_results.txt`)

### 2.1 Method (OBS inputs, DER evaluation)

The script decodes the quartic coefficients **from the shipped bit patterns** (coefficient stores
`[rsi+0x40D8..0x40E8]`, re-decoded fresh at `0x1800E1F82..0x1800E1FAA`, `r10_decode.txt`):

| coefficient | bits | value |
|---|---|---|
| c0 | `0x3E800000` | `0.25` |
| c1 | `0x3EB12E86` | `0.34605807065963745` |
| c2 | `0x3E79C4BB` | `0.24391452968120575` |
| c3 | `0x3DF569C6` | `0.1198306530714035` |
| c4 | `0x3D0ED15E` | `0.03486763685941696` |

and emulates the **target's exact arithmetic** (re-verified at byte level this round): `f` and the
coefficients widened `cvtps2pd`, the quartic evaluated in **double** with the observed operation
order (`0x1800E236D–0x1800E23D9`), `P` rounded to **float32** by `cvtpd2ps` at `0x1800E2408`
(`cvtps2pd` back), then `·slot_4128·64.0` in double. The page[10] path is included
(`page10 = float32(double(live)·(1/2400))`, the double `1/2400` at `0x1804120B0` re-read this
round). The modulation cents is then `1200·log2(4·float32(P(f)))` — the slot/64 and ratio/correction
factors cancel exactly.

### 2.2 The corrected table (round-9 doc §3, claimed vs exact)

| f | `4·P` (round-9 doc) | `4·P` (exact) | cents (round-9 doc) | **cents (exact)** |
|---|---|---|---|---|
| 1/96 (walk 1, depth 25) | 1.014568 ✗ | 1.014526 | 25.02 ✗ | **24.966** |
| 1/48 (walk 1, depth 50) | 1.029268 ✓ | 1.029266 | 50.13 ✗ | **49.939** |
| 1/24 (walk 1, depth 100) | 1.059047 ✗ | 1.059405 | 100.55 ✗ | **99.906** |
| 1/12 (walk 1, depth 200) | 1.122414 ✓ | 1.122412 | 201.68 ✗ | **199.923** |
| 0.5 | 2.004663 ✓ | 2.004663 | 1204.0 ✓ | 1204.032 |
| 1.0 | 3.978684 ✓ | 3.978683 | 2386.6 ✗ | **2390.749** |

The task grid (`1200·log2(4·P(F·depth/2400))`, target-exact):

| F | depth 50 → cents | depth 200 → cents |
|---|---|---|
| 0 | 0.0000 | 0.0000 |
| 0.25 | 12.4823 | 49.9391 |
| 0.5 | 24.9662 | 99.9056 |
| 0.75 | 37.4518 | 149.9001 |
| 1 (the limit) | **49.9391** | **199.9231** |

The law: `max |cents − walk·depth| = 0.101 ¢` over `f ∈ [0, 1/12]` — an **interior** maximum at
`f ≈ 0.0561`, i.e. `F ≈ 0.673` at depth 200 (equivalently depth ≈ 134.6 at `F = 1`), **not** at
the `F = 1`/depth-200 corner, where the error is only `0.0769 ¢` *(round 11 correction: this
document originally said "near `F ≈ 0.75`"; the exact argmax is grid-sensitive within a
`~1e-4`-wide `f` plateau because the `float32` rounding of `P` flattens the minimum — see
`OCTAVOX_ROUND11_FINDINGS.md` §2)*; `max |4·P/4^f − 1| = 5.85e-05` for `f ≤ 1/12` (round-9's
"≤ 6e-5" ✓);
at `f = 1`, `4·P(1) = 3.9786835` (−0.53 % vs 4). `P(0) = 0.25` **exactly**, so unity at `f = 0`
survives the float32 rounding — round 9's structural conclusion stands.

### 2.3 Provenance of 50.13 / 201.68 (task §1.4)

1. **Not from any evidence artifact**: the values `50.13`, `201.68`, `100.55`, `2386.6` occur
   *only* in the round-9 doc updates themselves (grep over the full research tree); no `r9_*`
   evidence file, decompile, or record contains them.
2. **Not a measurement**: the target was never executed (rounds 8–10 invariant), and no PoC output
   was cited for them.
3. **Not from any formula**: candidate sources tested in the script — (a) the round-6 *transcribed*
   coefficients `0.34630/0.24410/0.11983/0.03489` → 24.98/49.97/99.97/200.06 ¢; (b) `2400f·(1+f/10)`
   (matches 4 of 6 rows to ≤0.13 ¢ but fails at `f = 0.5/1` by 56/253 ¢); (c) `ln 2` typo variants
   (0.69/0.693/0.7); (d) the round-9 doc's *own* `4·P` column → 25.04/49.94/99.32/199.93/1204.03/2390.75.
   **None reproduces the claimed column.**
4. **Internally inconsistent**: `1200·log2(1.029268) = 49.94 ≠ 50.13` — the doc's cents column was
   not computed from the `4·P` column next to it.

**Conclusion: calculation error** (plausible-looking numbers written in place of an actual
evaluation). The corrected values are those of §2.2. The error does **not** propagate anywhere
else: the `ratio/16` scale, the unity-at-`f=0` construction, the `64.0` multiplier and the
coefficient bit patterns were all independently re-derived and stand.

---

## 3. The proven random modulation range

### 3.1 The arithmetic chain (OBS, freshly decoded — `r10_decode.txt` C/D, `r10_verify.py` [6])

* **Noise generator** `FUN_180139B40` (defaults `FUN_180139C00`): lagged-Fibonacci-style additive
  recurrence on a masked state pair (mask `0xFFFFFF00`, shift 8, seeds `0xD00/0x1100`), output
  `noise = float32(float32(sar(state,8)) · float32(0x34000001))`. The **scale is
  `0x34000001 = 2^-23·(1+2^-23)`**, not `2^-23` (round-9 refinement).
  With `sar ∈ [−2^23, 2^23−1]`: exact products span `[−(1+2^-23), 1−2^-46]`; after the float32
  `mulss` rounding: **`noise ∈ [−(1+2^-23), +1.0]`** — the minimum is *below* −1 by `2^-23`
  (attained at `sar = −2^23`, exactly representable), and **+1.0 is attainable**
  (`sar = 2^23−1` gives the exact product `1−2^-46`, which rounds **up** to 1.0).
  Round-9's "`noise ∈ [−1,1)`" was wrong at both ends.
* **abs** (`andps 0x7FFFFFFF`): `|noise| ∈ [0, 1+2^-23]` — **can exceed 1** by `2^-23` (≈1.19e-7).
* **S&H** (`FUN_180139970`): value-preserving.
* **slew** (`FUN_180139790`, freshly decoded): per sample, if `state > target`: `state += [this+0x120]`
  (the negative step), else `state += [this+0x108]` (the positive step); the crossing check
  (`0x1801397D3–0x1801397DD`: `seta al; cmp al,dl; je keep; movaps xmm0,xmm3`) **snaps the state to
  the target** — so the output can equal the target exactly. With targets `∈ [0, 1+2^-23]`:
  **`F ∈ [0, 1+2^-23]`** (arithmetic bound), not `[0,1)`.

### 3.2 The orbit computation (DER, default config — `r10_verify.py` [7])

The decoded recurrence is exactly: per call, from the state pair `(a,b)` (units of 256):
emitted `s2 = 2a+b`, `s4 = 4a+3b`, `s6 = 10a+7b`, `s8 = 24a+17b` (all mod `2^24`), and
`(a,b) ← (17a+12b, 24a+17b)`. The step matrix `M = [[17,12],[24,17]]` satisfies `M ≡ I (mod 4)`
and has **order `2^22 = 4,194,304`** over `Z/2^24`; the seed vector `(13,17)` has the full period.
Scanning **the entire orbit** (4 outputs per call):

* emitted `sar` range: **`[−8,388,607, +8,388,607]`** — the extreme `sar = −2^23` (`0x800000`) is
  **never visited**; `sar = +2^23−1` **is** visited;
* emitted float32 noise over the orbit: **exactly `[−1.0, +1.0]`** (both endpoints attained via
  rounding — `sar = −8,388,607` gives `−(1−2^-46) → −1.0`);
* therefore the **orbit-actual** `|noise| ≤ 1.0` and **`F ∈ [0, 1]` with `1.0` attainable**
  (round-9's `[0,1)` corrected at the endpoint).

Caveat (OBS+inference): the orbit result assumes the default config — the `rdm noise` module's
record is **empty** (why it was missing from the round-4 dump), so no record links reconfigure it.

### 3.3 Pitch effect of the endpoint excess (DER)

At `F = 1+2^-23` the resulting `ΔP` is below half a float32 ulp of `P` over the whole shipped
range, so the `cvtpd2ps` rounding of `P` absorbs it wherever `P` does not sit within `|ΔP|` of
a rounding midpoint: the cents at `F = 1+2^-23` equals the cents at `F = 1` *bit-exactly* at
the two depths verified in the script (50 and 200). *(Round 11 audit: over the full shipped
depth range 0…200 the equality is bit-exact at 190 of 201 integer depths; at the remaining 11
depths — 61, 87, 101, 134, 141, 144, 151, 160, 162, 182, 192 — `P` straddles a rounding
midpoint and the rounded value flips by exactly one ulp, a `2.0e-4 ¢` cents change, i.e. one
`P`-ulp — `~2×10⁻⁷` relative pitch, utterly inaudible and ~500× below the quartic's own fit
error. "Zero effect" therefore means "zero at the tested depths, ≤ one `P`-ulp everywhere
else", not bit-exact universality.)* The refined range thus changes no *audible* output value:
the maximum modulation at the shipped depths remains **49.939 ¢ (depth 50) / 199.923 ¢
(depth 200)**.

---

## 4. Corrections to round 9 (explicit; originals preserved, assumptions recorded)

1. **Round-9 §3 table** (also quoted in `OCTAVOX_OPEN_QUESTIONS.md` Q-7, D4 line ~623, D6 R-50,
   and the package README): the cents column `25.02/50.13/100.55/201.68/1204.0/2386.6` is
   **replaced** by `24.97/49.94/99.91/199.92/1204.03/2390.75`, and the two mistyped `4·P` cells
   (`1.014568 → 1.014526`, `1.059047 → 1.059405`) are fixed. The law statement
   ("≈ `walk·depth` cents") is retained with the quantified bound `≤ 0.101 ¢`; the round-11
   audit replaced the former "one-sided upward" tail of that statement with the precise
   direction/error distinction (§8, item 2).
2. **"`noise ∈ [−1,1)`"** → arithmetic `[−(1+2^-23), +1.0]`; orbit-actual `[−1.0, +1.0]`.
3. **"`F ∈ [0,1)`"** → orbit-actual `F ∈ [0, 1]` (`1.0` attainable); arithmetic bound `[0, 1+2^-23]`
   with no audible pitch effect (§3.3, as qualified by the round-11 audit).
4. **"scale `0x34000001` = `2^-23`"** → `2^-23·(1+2^-23)`.
5. **Round-8 precision note** (`evidence/records/rea8_rate_constants.txt`: "No CVTPD2PS / no
   float32 rounding of P"): **confirmed wrong** by the byte-level decode — `cvtpd2ps xmm0,xmm3` at
   `0x1800E2408` + `cvtps2pd xmm13,xmm0` at `0x1800E240C` are present (round 9 was right; the
   original evidence file is preserved unchanged, the correction is recorded here and in D6).
6. **Output-ratio wording** (task §2): the total output ratio is
   **`ratio · correction · 4·P(f)`** — the *configured* ratio (page[0], from the temperament
   tables), the *tracker correction* (T+0x6070, the running multiplicative walk — exactly the
   value read out by `m02` param 13), and the *random modulation* (`4·P(f) ≥ 1`, so the
   modulation moves the pitch **upward** from the base — a statement about direction, not
   about the sign of the quartic's approximation error).
   The output equals the configured ratio **only** when `correction = 1` (in particular: with
   pitch tracking off, the correction is hard-set to `1.0` at `0x1800E5`-region and the zero-
   modulation output is exactly the configured ratio). All doc statements implying "output =
   configured ratio" are re-worded accordingly.
7. **Round-9 §2.5 slew description** refined: the clamp is *at* the target (state can equal it),
   and with `dl = ([+0x108] == [+0x120])` the equal-steps case (rate 0) snaps instantly to the
   target — consistent with the corrected endpoint.

---

## 5. Tracker outputs 12 and 13 — the exact data path (`m02` params `0xc`/`0xd`)

### 5.1 The toucher set (OBS — raw-byte `.text` displacement scan, `r10_scan2_results.txt`)

| field | touchers |
|---|---|
| `T+0x6074` (clamped correction **target**) | written only by `FUN_1800DFE80` (`0x1800E0154` value / `0x1800E016E` 1.0); read by `FUN_1800E0190` (`0x1800E019A`) and by `m02` param 12 (`0x1800E1758`, engine-relative `+0xA58C`) |
| `T+0x6070` (running correction **gain**) | `FUN_1800DFE80` reset-to-1.0 on period jumps (`0x1800DFF33`); `FUN_1800E0190` walk (read `0x1800E01FB`, write `0x1800E024E`); tracker-init reset (`0x1800E0E94`); read by `m02` param 13 (`0x1800E17E8`, engine-relative `+0xA588`) |

`T = dsp_t + 0x4518`; the engine-relative displacements `0xA58C/0xA588` are the only
engine-relative uses of the two fields anywhere in the image (1 raw hit each, both in `m02`).

### 5.2 Upstream values (OBS, from the existing decompiles + fresh decode of the init region)

* `FUN_1800DFE80` (tracker analysis, per frame): computes the period estimate (`T+0x606C`), does
  the temperament/degree table lookups, and stores the **correction target**
  `fVar12 = pfVar4[−3]/fVar9` to `T+0x6074` — but only if the level field `T+0x5FEC > 6e-6`
  (otherwise `T+0x6074 = 1.0`). On period jumps (`|Δ| ≥ 4e-4`) it resets `T+0x6070 ← 1.0`.
* `FUN_1800E0190` (per-block record writer): the **effective target** is `T+0x6074` only if it
  lies inside the window `[T+0x6050, T+0x604C]` (stored descending), else `1.0` (and `1.0` with
  pitch tracking off); the per-step ratio `target/running` is clamped to
  `[T+0x6048, T+0x6044] = 2^(±1/(12·correctrate))`; then
  **`T+0x6070 ← clamped_ratio · T+0x6070`** and the record ring is written
  `{correction = T+0x6070, i4, i5, 2·period}` (round 9 §2.3).
* The tracker-init region (freshly decoded, `0x1800E0DF0–0x1800E0EB0`) resets `T+0x6070 = 1.0`
  (`0x1800E0E94`), installs the envelope constants (`0x3F7E0000/0x3C000000` at `T+0x5F18/0x5F1C`)
  and wires the cached table pointers (`T+0x6018/0x6020/0x6028/0x6030`).
* **Connection to the pitch engine (round 9)**: the same `T+0x6070` value is the record's
  correction field, consumed by the prepare as `ring[(idx+3)]` → `slot_4128[v] = page[0]·correction`
  → `step = 4·P(f)·ratio·correction`. **The two read-outs report exactly the live pitch-engine
  correction** (its target and its running value).

### 5.3 The conversions (OBS — fresh decode `r10_m02_results.txt` D1/D2; cross-verified by REA/Ghidra, evidence `ev_d6c81933…`)

Dispatch: `m02` = `FUN_1800E1520` = engine vtable `0x180409F60` slot **`+0x10`**; the handler
selection is `lea eax,[rdx−7]; cmp eax,6; ja default` with the jump table at **`0x1800E1890`**
(read this round): paramIds `7..13 → 0x1800E1554/0x1800E15CB/0x1800E161E/0x1800E16A5/0x1800E16DB/
0x1800E1751/0x1800E17E1` — **param 12 → `0x1800E1751`, param 13 → `0x1800E17E1`** (verified).
The default path tail-jumps to vtable slot `+0x20` (`0x1800C5770`, the base-class getparam).

Both handlers (identical structure, different field + different final constant):

```
e   = [[T+0x5FC8] + 4]                      ; the one-pole POWER envelope S[+4] (round 8)
t   = float32(sqrt(double(e) · 0.9999998812035873))       ; K1 @ 0x180412320
thr = exp(0.11513 · double(T+0x5FD0))       ; K2 @ 0x180412200 = 10^(gatelevel_linear/20)
if t < thr: field = 0                        ; the gate (comisd/jae @ 0x1800E1793 / 0x1800E1823)
if |field| ≤ 7.105427357601002e-15: out = 0  ; the 2^-47 guard (comiss @ 0x1800E17A9 / 0x1800E1839)
else: out = float32(K3 · logf(field))        ; logf = 0x180352DC0, K3 · double
```

| param | field read | K3 (double, verified) | output semantics |
|---|---|---|---|
| **12** (`0xc`, `0x1800E1751`) | `[engine+0xA58C]` = **`T+0x6074`** (the clamped *target*) | **`−1731.234`** @ `0x180412888` | `1200·log2(1/corr_target)` cents |
| **13** (`0xd`, `0x1800E17E1`) | `[engine+0xA588]` = **`T+0x6070`** (the *running* gain) | **`+1731.234`** @ `0x180412540` | `1200·log2(corr_running)` cents |

The twins are **sign-inverted** and read **different stages** of the same correction: param 12
reports the cents of the inverse *target*, param 13 the cents of the *running* multiplier that the
pitch engine is actually applying. (The round-6 control-flow matrix's `−`/`+` claim is confirmed;
`1731.234` is the hand-typed `1200/ln 2` — note that param 7 of the same function uses the more
precise `1731.234049`, so the two read-outs carry a `2.8e-8` relative scale error.)

**The double conversion (round-8 DER prediction, unchanged):** `T+0x5FD0` already holds the
*linear* power `10^(g/10)` (clamped `≤ 1`), but the handler maps it through `10^(x/20)` again, so
the threshold is `≥ 1.0` for every setting; with the normalised power envelope
(`sqrt(S[+4]) ≤ 1`) the gate `t ≥ thr` cannot open for normalised audio — the predicted output of
params 12/13 is **0** for normalised signals (the read-outs would only wake for over-unity
sample peaks). This is a **statically derived possible defect**, runtime-unverified (UNK), and no
PoC result is substituted for it.

### 5.4 Final consumers (OBS)

* `FUN_1800E1520` occurs **exactly once** in `.rdata` — the engine vtable slot `0x180409F70`
  (`= 0x180409F60 + 0x10`); there are **zero** direct `E8/E9` calls to it anywhere in `.text`
  (verified scan) — `m02` is reachable **only** through virtual dispatch `call [vptr+0x10]`.
* The vtable family around `0x180409E60…0x180409F60` shows the module-class layout
  `{dtor, m01 (+0x08), m02 (+0x10), base methods}`: generic modules carry the base
  `m01/m02 = 0x1800C5730/0x1800C5740`; the `dtonicii9` engine **overrides both**. `m02` is the
  module getparam slot — the framework's generic parameter-read dispatch (`call [vptr+0x10]`),
  consistent with the control-flow matrix's "engine parameters (tracker config, read-outs)".
* The exact framework call site that polls the engine's `m02` (a generic indirect call on the
  module object) and the VST3-side binding of ids 12/13 (host meter vs. internal read-out) were
  **not** re-derived to the plugin boundary this round — recorded as a limitation (§6).

---

## 6. Remaining evidence limitations (explicitly retained as open)

* **Runtime behaviour (UNK):** the target was never executed. The walk statistics, the audible
  character of the one-sided (upward-from-base) modulation, the correction's convergence with real audio, whether the
  `m02` 12/13 gate ever opens with real material, and the polling cadence of the read-outs are all
  unverified. **No PoC result is substituted for any target measurement.**
* **The double conversion defect stays DER** (a statically derived possible defect, not a runtime
  confirmed behaviour) — as required by the task.
* **The orbit result assumes the shipped default config** (seeds `0xD00/0x1100`, mask, shift,
  scale); the empty `rdm noise` record is the evidence the module runs on defaults, but a generic
  config path (if any exists) was not exhaustively excluded.
* **The `m02` consumer chain above the engine vtable** (the framework's polling site and the
  host-side binding of the read-out ids) was not traced to the VST3 boundary.
* **The window bounds `T+0x604C/0x6050`** (the plausible-target window of the correction walk) were
  not read this round — the mechanism is documented, the constant values are not.
* Q-14 (two-tap unity-ratio comb) and Q-8 (host latency value) remain untouched, as documented in D7.

---

## 7. Status

* The round-9 pitch model is now **mathematically self-consistent**: the modulation cents law
  `1200·log2(4·float32(P(walk·depth/2400))) ≈ walk·depth` (≤ 0.101 ¢ error; achieved cents
  slightly **below** nominal across the bulk of the domain — §8),
  exact unity at `f = 0`, proven ranges (`noise`/`F`), and the three-factor output ratio
  `ratio · correction · 4·P(f)` with the correction read out host-side by the sign-inverted twin
  read-outs `m02` 12/13.
* **Q-7's numeric layer is CLOSED** (the example values corrected; the law quantified).
  **The tracker read-outs 12/13 data path is CLOSED at the static level** (upstream, conversions,
  consumers, toucher set), with the double-conversion defect retained as a DER/open runtime
  question (Q-15's residual, now precisely scoped).
* Round-10 artifacts (all read-only, `/home/z/octavox/analysis/`): `r10_decode.py/.txt`,
  `r10_verify.py` / `r10_verify_results.txt`, `r10_m02.py` / `r10_m02_results.txt`,
  `r10_scan2.py` / `r10_scan2_results.txt`, `r10_dispatch.py` / `r10_dispatch_results.txt`,
  `r10_wrapper.py` / `r10_wrapper_results.txt`, `r10_tracker_init.txt`,
  `r10_decompile_1800E1520.json` (+ `.err`, REA evidence `ev_d6c81933…`).
  Originals unchanged: `Ox.vst3` `7bb260f6…`, round-8 zip `04fe8f94…`.

---

## 8. Round-11 numerical-audit corrections (changelog)

A final audit (`OCTAVOX_ROUND11_FINDINGS.md` §2, script `r11_numeric_audit.py`, 5e-10-resolution
sweep of the target-exact residual) found two wrong statements in this document and in the
round-10 verification script. Both are corrected in place; the original script output is
preserved as `r10_verify_results_v1_superseded.txt`. The **numbers** were never wrong — only
these labels/locations:

1. **Location of the maximum error.** §2.2 said "the interior maximum near `F ≈ 0.75`, depth
   200", and `r10_verify.py` [4] printed "(at F=1, depth=200 corner)" — mutually inconsistent
   and both inaccurate. Correct: the maximum `0.10131 ¢` is an **interior** one at
   `f ≈ 0.05606–0.05612` → `F ≈ 0.6727–0.6734` at depth 200 (equivalently depth ≈ 134.5–134.7
   at `F = 1`); the `F = 1`/depth-200 corner carries only `0.0769 ¢`. The exact argmax is
   grid-sensitive within a `~1e-4`-wide `f` plateau (the `float32` rounding of `P` flattens the
   minimum); the deepest point on a 5e-10 grid is `f = 0.0560624`, residual `−0.101311 ¢`.
2. **"One-sided upward" as a description of the approximation error.** The signed residual
   `1200·log2(4·float32(P(f))) − 2400·f` over the shipped domain `[0, page10(200)]` is
   **negative across the bulk** (achieved cents slightly *below* nominal), zero at `f = 0`,
   with positive excursions bounded by half a `P`-ulp (`+1.03e-4 ¢`) only for `f < 2.9e-5`;
   beyond the shipped range it changes sign twice (`+4.03 ¢` at `f = 0.5`, `−9.25 ¢` at
   `f = 1`). "Upward" correctly describes only the **modulation direction** (`4·P ≥ 1`), a
   different concept from the sign of the approximation error.
3. **(qualification, not an error)** §3.3's "changes no output value" / "bit-exactly": true at
   the depths the script tested (50/200); over the full 0…200 depth range the `F = 1+2^-23`
   excess flips the rounded `P` by one ulp at 11 of 201 integer depths (max cents change
   `2.0e-4 ¢`). Corrected wording in §3.3 above.

No raw evidence was altered; `r10_verify.py` carries the same correction in its docstring and
output labels, and its regenerated output differs from the original only in those two lines.
