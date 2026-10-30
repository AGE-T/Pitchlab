# R18 — static DSP findings (P2/P3/P4/P5)

Environment: same as R17 — the preserved target `original/Ox.vst3`
(SHA-256 `7bb260f6…`, **never executed**) analysed read-only with
`petk.py` (capstone 5.0.9, reinstalled this round) plus the shipped
decompile/record evidence. All VAs static. Evidence classes: OBS (observed
in the binary), DER (derived), UNK (open).

---

## P4 — the remaining scale/key interpretation rules

### P4.1 The per-interval mux design rule — **CLOSED (OBS rule, DER intent)**

The 29-entry `int table N` muxes (record [46] etc., `add=0x18013A8D0`,
`select=intN-out`) carry the authored values

```
in1..in29 = {0,1,3,3,4,5,7,7,8,10,10,11,12,14,14,15,17,17,18,19,21,21,22,24,24,25,26,28,28}
```

and the choice modules `int1..int8` (record [45] etc.) have exactly 29
choices in the order `−2 oct, −14th, −13th, …, −2nd, unison, +2nd, …,
+14th, +2 oct` (default 14 = unison, 0-based). The choice string found at
`0x1803BC140` with 43 names (`-3_oct…+3_oct`) belongs to a different
record context (not the int1..8 chain; the int records' own choices text
is the 29-entry list above — record dump `evidence/records/full_records.txt`
[45], verified against the record's string-pointer array at `0x1804B05D0`).

**The rule.** Let `s` be a choice's signed diatonic step count
(−14…+14, the label's own direction; unison = 0). Then the mux value is

```
v = 14 + s + δ,   δ = 1 if s mod 7 ∈ {2, 6} else 0
```

i.e. every 3rd- or 7th-class interval (and every octave compound of them)
is bumped by exactly **+1 mux unit**. The bumped choices (OBS):
`+3rd, +7th, +10th, +14th, −2nd, −6th, −9th, −13th` — precisely the
set {s mod 7 ∈ {2,6}}.

**Why the bump exists — the consumer.** The engine walk is
`shiftN = 14 − v = −s − δ` (the sign inversion already recorded in R17:
"−" labels walk up, "+" labels walk down). The walk arithmetic in
`FUN_1800E5160` (disassembly this round):

* octave/step split: `0x1800E6ED5` (`imul 0x92492493`, the ÷7 magic),
  `0x1800E6F0B` `xmm7 = (shift/7)·1200` (the octave cents);
* `0x1800E6F10` `test r9d,r9d; js 0x1800E6FFA` — the two directions;
* **positive branch** (shift ≥ 0, walks **up**): start = `min(shift%7,
  size−1)` (`0x1800E6F19–2F`), row0 = `local_480[start] + 1200·oct +
  pcent` (`0x1800E6F2F–52`); per-row j: target = `local_480[(start+j) %
  size]`, octave-wrapped **upward** (`0x1800E6F9B` `lea eax,[rcx+0x4b0];
  cmovge` — if target < base add 1200), cents = `(target ± 1200) +
  pcent − local_480[j] + 1200·oct`, ratio = `exp(cents·ln2/1200)/16`
  (`0x1800E6FD3` `call 0x180351f50` = `exp`, `xmm10 = 0.00057762265`
  loaded at `0x1800E538B` from `0x1804120C0`; `xmm9 = 0.0625` = the
  1/16 page scale);
* **negative branch** (shift < 0, walks **down**): step = `(shift/7)·7 −
  shift` (`0x1800E6FFA`), start = `(1 − shift/7)·size − step`
  (`0x1800E700C–13`), row0 = `(local_480[start % size] − 1200) % 1200 +
  pcent + 1200·oct` (`0x1800E7016–63`, the ÷1200 magic `0x1B4E81B5`);
  per-row j: target octave-wrapped **downward** (`0x1800E709C`
  `lea eax,[rcx−0x4b0]; cmovle` — if target > base subtract 1200).

`local_480` is the 7-position (per octave) step-grid array built by the
zone-run fill (emission loop `0x1800E65B0–0x1800E6790`): for each
non-zero pattern slot, `local_480[run positions] = temperament_cents[slot]`,
with run length = `pattern_value − previous_value` (gap doubling) and
single positions for 99-fillers; `scaleSize` = the final position count
(7 for the 7-degree patterns, 12 for the chromatic row).

**The consequence (the answer to "why do some interval selections map to
identical integers").** Working out the target position for every choice,
the set of reachable positions is exactly

```
{0, 2, 3, 4, 6} (mod 7)   — the 1st, 3rd, 4th, 5th, 7th scale degrees
```

The 2nd (position 1) and 6th (position 5) of the scale are **never walk
targets**: the labels whose natural target is one of them are exactly the
bumped set, and the bump moves the target one grid position **down**
(1→0, 5→4). The resulting collapsed label pairs (OBS — identical mux
values): `(+3rd, +4th)→17`, `(+7th, +1 oct)→21`, `(+10th, +11th)→24`,
`(+14th, +2 oct)→28`, `(−2nd, unison)→14`, `(−5th, −6th)→10`,
`(−9th, −1 oct)→7`, `(−12th, −13th)→3`.

In interval terms the engine's realised vocabulary (just ionian, from
the tonic): upward `{P1, M3(386¢), P4(498¢), P5(702¢), M7(1088¢), P8}` and
downward `{P8, m6(814¢), P5(702¢), P4(498¢), m2(112¢)}` — the mirror
inversions. **No 2nd or 6th is ever produced upward; no 3rd or 7th is
ever produced downward.** A "3rd" choice literally sounds a 4th-class
shift; a "−2nd" literally sounds the unison (OBS at the data level;
which side of the label/applier sign convention the audible direction
falls on remains the standing sign question below).

**Pentatonic scales use the raw index path.** `c_cmp2`/`c_cmp3`
(records [17]/[18]) test the scal-table output for 14/15 — the mux
outputs of the **chromatic and whole-tone choices** (R17's parenthetical
was right in choice terms: the scal-table mux `in10=14` for `chromatic`,
`in11=15` for `whole tone`, record [16]); `c_or` (record [19]) then makes
`int table switch N` (record [47]) select `in1 = intN-out` — the **raw
choice index** — instead of the mux value. On those scales the walk uses
the un-bumped step count and the scale's own zone-run doubling does the
snapping (e.g. "+3rd" → 2 steps on the penta array `[C,C,D,E,G,G,A]`
lands on D — R17's "a +3rd in penta-major snaps to the 2nd").

**A second authored irregularity (OBS, bounded).** The scal-table mux
(record [16], values `{1,2,3,4,5,6,7,10,11,14,15,16,17,18,13}`) maps the
last six engine scale choices to pattern rows shifted +2 from their
name-matching rows: `chromatic→14` (the penta-maj pattern!), `whole
tone→15` (penta-min), `penta maj→16` (enigmatic), `penta min→17`
(neapolitan), `enigmatic→18` (hungarian), `neapolitan→13` (whole-tone);
`hungarian` (choice 16) exceeds the 15-entry table (out-of-range
behaviour of the table module: UNK). The row contents are re-dumped this
round (`petk.i32s(0x1803BB780 + row·0x30, 12)`) and are unambiguous
(row 12 = the 99-filler chromatic, 13 = whole-tone, 14/15 = pentas,
16 = enigmatic, 17 = neapolitan, 18 = hungarian). We record this as an
authored data inconsistency in the target (OBS); its audible effect is
UNK (the target was never executed).

**Design intent (DER, unproven):** the realised grid `{1st, 3rd, 4th,
5th, 7th}` = the notes {C, E, F, G, B} — the I-chord plus the 4th and
the leading tone (equivalently the major scale minus its 2nd and 6th).
No static evidence in the binary states *why*; hypotheses (grid avoiding
the two degrees that are a whole step above another member / keeping
targets on the strongest consonances) are DER only.

### P4.2 The key-ratio ascent/descent "asymmetry" — **CLOSED (R17 record corrected)**

R17 recorded: "keys below the reference read `−temperament[9−s]`, keys
at/above read `+lookup[(s+3) mod 12]`" and left the asymmetry open. The
disassembly proves the second read is the **same raw temperament cents
array**, not the zone-run lookup:

```asm
0x1800E6557:  cmp   ebx, 9                  ; ebx = keymap[key] (0..11)
0x1800E655A:  jg    0x1800E6574
0x1800E655C:  mov   eax, 9
0x1800E6561:  sub   eax, ebx                ; 9 − key
0x1800E6565:  mov   ecx, [rbp + rax*4 + 0xF8]   ; cents[9 − key]   (local_450)
0x1800E656C:  neg   ecx                     ; NEGATED
0x1800E656E:  movd  xmm0, ecx
0x1800E6572:  jmp   0x1800E6580
0x1800E6574:  movsxd rax, ebx
0x1800E6577:  movd  xmm0, [rbp + rax*4 + 0xD4]   ; = rbp+0xD4 + key*4
0x1800E6580:  cvtdq2pd xmm0, xmm0
0x1800E6584:  mulsd xmm0, xmm10             ; × ln(2)/1200
0x1800E6589:  call  0x180351F50             ; exp()
```

The stack layout (proven from the zone-fill stores `[rbp+rax*4+0xC8]` at
`0x1800E65F8` and the temperament stores `[rbp+0xF8]…[rbp+0x124]` at
`0x1800E5ECE…0x1800E6254`) puts `local_480` at `rbp+0xC8` (12 ints) and
the cents array `local_450` at `rbp+0xF8` immediately after it. The
`≥10` read `[rbp + key*4 + 0xD4]` for the only two valid keys
(10 = A#/Bb → `rbp+0xFC` = cents[1]; 11 = B → `rbp+0x100` = cents[2])
therefore reads **cents[key − 9]** — the adjacent cents array, *not* the
zone-run lookup (the decompiler's `local_480[key+3]` was a stack-layout
artifact). The key slot itself is read at `0x1800E5D64–0x1800E5DB2`
(live `[rsi+0x60]` / default `[rsi+0x68]`, min `[rsi+0x40]`, max
`[rsi+0x44]`) and mapped through the 17-entry key map at
`0x1800E5DB2` (`mov ebx, [r11 + rcx*4 + 0x3BB610]`; r11 = image base,
set at `0x1800E5D8D`; the map `[0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11]`,
A = 9).

**The law (CLOSED):** `keyRatio = exp(±cents·ln2/1200)` with
`cents = temperament[|key − 9|]`, negated below the reference A and
positive above it — **symmetric in |key−9|**; the "asymmetry" was a
decompiler artifact, not a design property. The reference A=9 is
consistent with the round-9 m02 radix record (A4-440).

### P4.3 Temperament/scale/key/pcent interaction — composition law (OBS)

The full composition at the per-voice ratio table (this round's
disassembly):

```
row_j ratio = exp( (Δcents_j + pcentN + 1200·oct) · ln2/1200 ) · (1/16)
Δcents_j   = local_480[(start + j) mod size] (±1200 octave wrap) − local_480[j]
```

with `pcentN` added as plain cents (`0x1800E6FAB` `addss xmm0, xmm12`;
the `shtN` knobs "fshift: %f", range [−1,1], res 0.001, records [50] —
a ±1-cent-class fine shift), the key ratio multiplying uniformly
(upstream), and the temperament entering through `local_480` (the
zone-run fill copies the temperament cents per slot — `0x1800E65CB`).
The interval snapping is the walk itself (P4.1); the scale degree
boundaries are the pattern values; the key transposition is the
uniform key-ratio factor; temperament and snapping interact only
through `local_480` (one array, filled once per update).

**Residue:** the design *intent* of the 6-slot target grid (P4.1) stays
DER/UNK; all runtime values UNK (target never executed).

---

---

## P2 — the producer of the output-stage mode flag `[obj+0x8C]` — **CLOSED (R17 hypothesis refuted)**

R17 left the mode flag's producer UNK with the DER hypothesis "mod input
connected". The producer is found this round:

**The writer.** `0x1800DBCC9: mov dword ptr [rdi+0x8C], eax` inside the
core constructor `FUN_1800DBAD0` — the constructor invoked by the module
add thunk `0x1801374D0` (the `add=` of records [342]/[348] "just for the
filter2"/"just for the filter") at `0x180137549` (`lea rcx,[rbx+0x80]` —
the delay-reader core object is the sub-object at wrapper+0x80; the same
object whose `[+0x90]` the binder `FUN_1800DBEF0` zeroes at
`0x1800DC295` and whose `[+0xD0]` receives the kernel table at
`0x1800DC28E`).

**The value source.** The ctor builds the field name **`modsw`** (the
string constant at `0x1804084A0`) and reads the module record through
the field getter `0x1800C3D70(record, "modsw", default)` — with the
default obtained from the **class-defaults record** via `0x18032B140`
(the class-default machinery: `0x18032A050` walks the static
class-defaults object at `0x1803B9918`, whose entry list starts at
`0x1803B9940` with stride `0x68`). The class-defaults entries (dumped
this round):

```
entry[0] @0x1803B9940: tag=1  'maxdelay'  (alt 'max_delay')  default '0'
entry[1] @0x1803B99A8: tag=1  'modsw'     (alt 'mod_select') default '0'  max '1'
entry[2] @0x1803B9A10: tag=4  'in'
entry[3] @0x1803B9A78: tag=4  'mod'
entry[4] @0x1803B9AE0: tag=5  'out'
entry[5] @0x1803B9B48: tag=6  'delayamt'  (alt 'delay')      default '0'  ref '@maxdelay'
entry[6] @0x1803B9BB0: tag=6  'modamt'    (alt 'moddelay')   default '0'  ref '@maxdelay'
```

So **`[obj+0x8C]` = the module record's `modsw` ("mod select") field —
a construction-time per-instance constant with range 0..1 and class
default 0** — NOT a host parameter, NOT a module-graph runtime
connection, NOT a per-block automation path, NOT an internal state
transition. R17's "mod input connected" DER hypothesis is refuted by the
field name and the class-default entry.

**Exhaustiveness.** A scan of all 13 116 `.pdata` functions finds 35
`[reg+0x8C]` stores in the whole binary; the only one reachable for this
object class is `0x1800DBCC9` (the others belong to unrelated classes
sharing the offset). The class's own setParam dispatcher (method-table
entry at `0x180409B58` → `0x1800DC770`) writes only `[obj+0x118]`
(`0x1800DC781`), never the mode. The binder writes `+0x90`/`+0xD0` only.

**Consequence for the shipped patch.** The Octavox patch text (records
[342]/[348]) sets `maxdelay/in/mod/delayamt/modamt` but **no `modsw`** →
the getter falls back to the class default **0** → the mode-1
"externally steered 4-float phase" trajectory decoded in R17 is
**statically unreachable in the shipped plugin** (dead code unless a
patch text supplies `modsw=1`). The glide path (mode 0, K = 0.25) is the
only live trajectory in this patch. All runtime observations remain UNK
(the target was never executed).


---

## P3 — tracker `T+0x5F14` initialisation and the parameter ordering — **the setter identified; the delivery site bounded UNK; R17's state-field record corrected**

**The writer of `T+0x5F14` (instruction-level, this round).** The field
is addressed with the FOLDED displacement: the tracker `T` is the
embedded member at `dsp+0x4518` (the dsp itself at `wrapper+0x430`, per
the add_fn `FUN_180137E20` at `0x180137E4D/E6A`), so `T+0x5F14` =
`dsp+0xA42C`. The only writes in the whole binary:

```asm
0x1800E1151: mov dword ptr [rbx + 0xa42c], 2      # rate < 50000
0x1800E11EA: mov dword ptr [rbx + 0xa42c], 4      # 50000 <= rate < 100000
0x1800E129B: mov dword ptr [rbx + 0xa42c], 8      # rate >= 100000
```

inside **`FUN_1800E10B0` — the dsp module's setParam**, i.e. the "m01"
method (the object's method table is `0x180409F60`, set at
`0x1800E03B3` in the tracker ctor; slot layout: `[+0]` dtor
`0x1800E7DB0`, `[+8]` setParam `0x1800E10B0`, `[+0x10]` getParam
`0x1800E1520` — the pointer to the setParam sits at `0x180409F68`).
**Case 5 of the setParam switch is the system sample rate**: it stores
the rate at `dsp+0xA524` (`0x1800E10B0` case 5 prologue), the cadence
class `{2,4,8}` at `T+0x5F14`, and the rate-class detector constants
(`dsp+0xA3CC..0xA41C`, the per-class coefficient tables). The getParam
(m02, `0x1800E1520`) cases 7/8 read `dsp+0xA524` as the rate when
computing the published pitch/frequency — confirming param 5 = the rate.

**Param 5 is not a host parameter and not a record field.** The
'dtonicii' class-default record (the static object at `0x1803BD080`,
"class name dtonicii, version 1.7"; entries at stride 0x68 from
`0x1803BC380`) lists exactly: `maxdelay, nvoices, steps, in, scale, key,
pitchtracking, hysteresis, correctrate, mincorrect, intervalglide,
tuning, tune, quantization, minpitch, gatelevel, xfadetime, mod, out,
shift, pcent, modamt, delayamt, userscale, pitch, period, freq, amp,
tonality, timbre` — **no rate field**. The rate is a system-reserved
param id delivered by the module-graph framework (the same machinery
that evaluates the class-default formulas referencing the `#speed`
system variable, e.g. maxdelay's default `22000/32500/#speed/#if` —
proving the setup pass knows the rate at graph-construction time).

**A correction to the R17 init-site record.** The tracker ctor DOES
initialise the state machine state: `0x1800E0DC1: mov qword ptr
[rdi+0x5FF8], r8` (r8 = 0) — the state starts at **0** (and the
hold-override `[T+0x5F30]` = 0 at `0x1800E0DB3`, `[T+0x5F2C]`,
`[T+0x5FEC]`, `[T+0x5FF4] = 0.0078125` etc. in the same r8-zeroing
block `0x1800E0D80..0x1800E0E10`). R17's "no immediate store covers
them" statement was true only for the immediate stores — the
register-zeroed fields include the state. **`T+0x5F10` (the countdown)
and `T+0x5F14` (the cadence) remain uninitialised by any ctor store**
(re-verified: the only `[T+0x5F10]` write is the reload
`0x18013BE30`, fed from `[T+0x5F14]` read at `0x18013BDF7`; the only
`T+0x5F14` writers are the three setParam stores above).

**The ordering mechanics (OBS).** The countdown gates the tracker's
frame cadence: per tracker frame `ecx = [T+0x5F10]; ecx--; if (ecx > 0)
{ [T+0x5F10] = ecx; return; }` else run the state machine (states 0..9
via the jump table at `0x18013D22C`, dispatch at `0x18013BE40`) and
**reload `[T+0x5F10] = [T+0x5F14]`** (`0x18013BDF7 → 0x18013BE30`).
So `T+0x5F14` is consumed at the FIRST countdown expiry during audio
processing — never before.

**The five phases distinguished (the task's list):**
1. *Constructor init* — the tracker ctor zeroes the state/hold/envelope
   fields and seeds the constants (BUT not 0x5F10/0x5F14);
2. *Module init* — the graph instantiation (`0x1800F00A0`/
   `0x180109D40`/`0x180122E00` call `FUN_180137E20` with the module id
   `0x113` and the record) binds the record fields;
3. *Default parameter publication* — the wrapper ctor `0x1800E2C00`
   publishes the slot-vector min/max/default triples per voice
   ({−32768, 32767, 1.0} … at `0x1800E2C3C..0x1800E2D36`), and the
   engine reads every parameter **live-or-default per block**
   (`FUN_1800E5160`'s slot reads) — the host parameters have structural
   defaults, so **no host parameter event is required before
   processing** for them (direct structural evidence, answering the
   task's caveat);
4. *Host parameter restoration* — reaches the slot LIVE pointers via
   the plugin's parameter dispatch (orthogonal to the rate);
5. *First audio processing* — the countdown decrements per frame; at
   expiry the state machine runs and consumes `T+0x5F14`.

**Verdict.** The setter of `T+0x5F14` is the framework's rate delivery
(setParam case 5) — not a host parameter event. The tracker's detection
arithmetic depends on the rate-class constants that ONLY case 5 writes,
so a functioning plugin structurally requires the rate delivery before
the first audio block; the exact static call site of the broadcast was
not located (the dispatch is fully virtual: no direct callers of
`FUN_1800E10B0`; the caller sits in the graph framework's generic
module-param machinery) — bounded UNK. The runtime residue question
(what `T+0x5F10`/`T+0x5F14` hold before the first expiry in the
original) stays **UNK**: the decisive Windows experiment is to break
after the tracker ctor (log `[T+0x5F10]`/`[T+0x5F14]`), break at
`FUN_1800E10B0` (log every (id, value) in call order relative to the
first process call), and break at the first `0x18013BE30` reload (log
the reloaded cadence). The target was never executed here.


---

## P5 — the interpolation kernel design question — **CLOSED as UNK-generator / DER-characterisation (with two record corrections)**

**Adjacency evidence (new this round).** The 256×6 table occupies exactly
`0x1803B6170..0x1803B7970` (256·24 = 0x1800 bytes). The preceding .rdata
holds the product strings (`'Eventide'` `0x1803B6100`, `'Octavox'`
`0x1803B6110`, **`'3.8.1'`** `0x1803B6118`) and the metadata debug string
`'cannot find metadata for … in module '` (`0x1803B6140`) — no other
coefficient tables; the data immediately after is the module-machinery
list sentinel at `0x1803B7970` (25+ code references — all list-head
initialisations). A whole-binary scan (all 13 116 `.pdata` functions,
rip-relative AND r11/image-base forms) finds **exactly two code
references to the table** — `0x1800DC287` (the filter-reader binder
storing the pointer at `[obj+0xD0]`) and `0x1800E1FB4` (the engine
installing it at `engine+0x40F0`) — **both readers; no writer exists**:
the table is a build-time constant (a design-tool artifact, not
runtime-generated). This is the strongest available closure evidence:
no static code computes the values.

**The exact phase law (correction 1).** The centroid of every row
equals `2 + r/256` to within ±0.003 (computed over all rows this
round) — row `r` realises the fractional delay `2 + r/256` (the phase-0
row IS the integer-delay-2 row). This refines the round-12 extraction
comment "row r corresponds to phase (r+0.5)/256 (derived)" — the +0.5
was an alignment-relative guess; the centroid law is measured.

**The consumer phase laws differ (correction 2).** The two consumers
index the table differently:

* the **engine** path (`FUN_1800E2105`, kernel pointer read at
  `0x1800E24C4`): `phase = (int(pos · 2048.0) >> 3) & mask`
  (`0x1800E24A1–AD`; the 2048.0 double at `0x180412558`, loaded into
  xmm14 at `0x1800E21AB`) = `floor(frac·256) & 255` — **all 256 rows
  reachable**;
* the **filter readers** (`FUN_1800DC430`): `phase = int(frac ·
  255.0)` (`0x1800DC616–1A`; the **255.0f** at `0x180412710` —
  byte-verified; the neighbouring `0x180412714` holds 256.0f, a
  different constant) — **rows 0..254 only; row 255 is unreachable in
  this consumer**.

The standing record "row = int(frac·256)" is therefore true only for
the engine path. Both consumers pair tap `k` with `ring[(base+k) &
mask]` and accumulate in **double** (`cvtps2pd`/`mulsd`/`addsd`:
`0x1800DC629–6F2` and `0x1800E24DD–5BF`) — our core's `ObsKernel6`
mode (float table, double accumulation, tap k ↔ ring[base+k−2] with
the window centred) matches the engine's law and orientation; the
Lagrange-6 default is unchanged.

**The frequency-response characterisation (refined).** Per-phase
magnitude responses (8192-point FFT, this round): the −3 dB point
varies **0.60·Nyquist (mid-phase 128, the half-sample delay) to
0.77·Nyquist (the integer phases)**; the Nyquist gain varies from
3.9e−3 (phase 128) to 0.64 (phases 0/255). The shaping is **monotonic**
(no stopband lobes) — refining R17's "cutoff ≈ 0.82–0.84·Nyquist"
single-number estimate into the per-phase band above.

**Design families tested and rejected.** R17: Lagrange, Farrow
(degree ≤ 7), windowed-sinc × {Blackman, Blackman-Harris, Nuttall,
Hann, Kaiser(β 2–13), Lanczos, 3-term cosine LS, root-raised-cosine,
general cosine window}. R18 additions: **equiripple / Parks-McClellan**
(rejected — the stopband shaping is monotonic, no equiripple lobe
structure) and **flat-passband least-squares** (rejected — the LS
optimum at the integer phases is a pure delta with 0.16 max-tap error
vs the table's deliberately shaped rows).

**Verdict: CLOSED as `UNK-generator / DER-characterisation`.** No
static or adjacent evidence identifies the generating algorithm; the
table is build-time data with no runtime writer. Our independent core
keeps the extracted table ONLY as the opt-in `ObsKernel6` comparison
mode (default `Lagrange6` unchanged, per the standing rule).

