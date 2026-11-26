# R26 Appendix C — CONTROL DATAFLOW (parameter/record/port plumbing)
Sources: fresh R26 disassembly (m01/m02 jump tables decoded; record extraction; prepare/update
sites) + r22 corpus (dirty-bit installers in FUN_1800E3350). Every row carries its evidence.

## C.1 m01 setParam (FUN_1800E10B0) — vtable slot 1 — ids 0..0x11 (jump table @0x1800E14D0)
| id | case VA | action (store target) | semantics |
|---|---|---|---|
| 0 | 0x1800E10DE | `[engine+0xA484] = value` (int) | tracker T+0x5F6C — **reader not found: UNK field** |
| 1 | 0x1800E10F2 | `[+0xA488]=v; [+0xA48C]=v+2` | **Minimum Pitch** pair (T+0x5F70/74) |
| 2 | 0x1800E110F | `[+0xA4E8] = v` | gate level (T+0x5FD0) |
| 3 | — | default (tail-call [rbx+0x18]) | none |
| 4 | 0x1800E1123 | `[+0xA440] = v` | adaptive-gate multiplier (T+0x5F28); written by the gatelevel applier FUN_1800E7960 (2/3 or sentinels −99/−98/−97) |
| 5 | 0x1800E1137 | `[+0xA524]=rate` + SR-class blocks: cadence {2,4,8} → +0xA42C; taps {5/6, 10/11, 20/21} → +0xA41C/0xA420; FIR coeff blocks 6/10/20 floats → +0xA3C8..+0xA3F0 (thresholds 50000/100000) | **system sample rate** (m02 ids 7/8 read it back) |
| 6 | 0x1800E1396 | `[+0xA448]=18; [+0xA450]=v·243; [+0xA44C]=1/v` | tracker analysis window (243 lags) |
| 7–13 | — | default | none |
| 14 | 0x1800E13D5 | `[+0xA558] = v` | steps (12; T+0x6040) |
| 15 | 0x1800E13E9 | qword `[+0xA57C] = 0` | hysteresis pair zero (T+0x6064/68) |
| 16 | 0x1800E13FD | `[+0xA564]=exp(+x·ln2/1200); [+0xA568]=exp(−x·ln2/1200)` | walk window (T+0x604C/50) |
| 17 | 0x1800E1460 | `[+0xA55C]=exp(+0.057762265/x); [+0xA560]=exp(−0.057762265/x)` | per-step clamp 2^(±1/(12·correctrate)) — **ln2/12 = 0.057762265 exact** |

## C.2 m02 getParam (FUN_1800E1520) — vtable slot 2 — ids 7..0xD (jump table @0x1800E1890)
| id | case VA | output | notes |
|---|---|---|---|
| 7 | 0x1800E1554 | pitch in cents: (rate·2048/period)·(1/261.6256) → log → ×1731.234049; −65535.0 if period ≤ eps | C4 = 261.6256 radix; log @0x1801887D0 |
| 8 | 0x1800E15CB | rate·2048/period (0 if ≤ eps) | frequency-domain raw |
| 9 | 0x1800E161E | the multi-scale period read ([a4b8/a4a8/a498]·K three-way min) | meter-family |
| 10 | 0x1800E16A5 | sqrt(meter[1]·K) | amplitude/energy |
| 11 | 0x1800E16DB | composite (field3/field1 chain with 2048/rate) | tonality-family |
| 12 | 0x1800E1751 | correction TARGET: gated logf(T+0x6074)·(−1731.234) | gate = sqrt(e_raw·K) vs gatelevel·K2 via 0x180351F50; "double conversion" DER stands |
| 13 | 0x1800E17E1 | correction GAIN: logf(T+0x6070)·(+1731.234) | twin of id 12 |

m01/m02 have **zero direct callers** in the whole .text (1,065,880 instructions scanned); both are
vtable-only (0x180409F68 / 0x180409F70) — the framework polling site is UNK.

## C.3 Graph-record plumbing (the 55-entry dtonicii9 record)
- Record entries (fresh extraction, verbatim): maxdelay=2400; nvoices=8; steps=12; in=softclip-out;
  scale=scal table-out; key=keys-out; pitchtracking=ptrk-out; hysteresis=10; correctrate=1;
  mincorrect=0; intervalglide=1; tuning=0; tune=tune-out; quantization=0; minpitch=minp-out;
  gatelevel=−55; xfadetime=xfad-out; mod1..8 = rdm slew 1..8-out; shift1..8 = c_multiply 1..8-out;
  pcent1..8 = sht1..8-out; modamt1..8 = rdep-out; delayamt1..8 = c_adder8..15-out;
  **userscale1..12 = 0** (declared, unwired in the shipped patch).
- Publishers: record-driven initializer FUN_1800E18B0 (binds mod<N> links into the +0x68 port
  table); pitch-config initializer FUN_1800E3350 installs the dirty-bit blocks
  (minpitch 0x1800E3834 [key "minpitch" built at 0x1800E385B], gatelevel 0x1800E4C44, shift
  0x1800E3EC8, pcent 0x1800E477D, modamt 0x1800E41AF, **delayamt 0x1800E4496** [key string via
  0x180017B40, lookups 0x18032B520/420/320], userscale 0x1800E4A64).
- Per-block update FUN_1800E5160 (called 0x180106B2F miso / 0x1801206EF siso): publishes the
  16/72/int12 tables via the host port vtable (owner->setPortValue +0xA0 / setIntPort +0x98,
  sites 0x1800E6C10/24, 0x1800E6DDF/F4, 0x1800E6E89/97 — W1–W3 stay closed), publishes the
  44-float pages element-by-element then flips the selector (0x1800E7749…0x1800E7859+), calls
  the gatelevel applier FUN_1800E7960 (0x1800E521B) and the period-law publisher FUN_1800E7B80
  (0x1800E5AED).
- Tracker-side caches: ctor installs T+0x6018/20 (port 25), T+0x6028/30 (port 26), T+0x6038
  (port 27 int), T+0x6058/60 (port-26 data ptr + steps=12) — 0x1800E0E54..0x1800E0E8D;
  state 7 advances T+0x6058 to the matched harmony row (0x1800E00F2).
- Control primitives (record-level): c_multiply (0x18013A250), c_adder8 (0x1800C7460),
  scal table (0x18013A790), c_fltr_a (add 0x18013AAC0; tc 0/10/20/25 classes), c_switch,
  c_minmax/c_minmax2, c_power (10^x), c_divide, c_subtract, iswitch6/7/9/11, stereomixer3
  composite (0x180137720) — all with record-driven wiring (see Appendix D).

## C.4 Dataflow invariants (static)
1. The engine consumes graph-module outputs exclusively through the record/port surface — no
   direct memory writes from graph modules into engine fields (W1–W3 negative, reconfirmed).
2. The record ring is touched only by ctor/writer/prepare (fresh: writer FUN_1800E0190 via
   tracker state 8; consumer sites 0x1800E25DB–0x1800E269F; nothing else in .text references
   engine+0x98 with 0xFFF masking).
3. F = element [0] of the mod port data for voice v: [engine[0x68] + 8 + 0x10·v] → [0]
   (fresh: 0x1800E2343–0x1800E2351 with the port-table base [rsi+0x68]).
4. Live Mode plumbing: live → c_switch(select=live) = {in1 = live mode delay comp (40) | in2 = 0}
   → c_adder8..15 = dlyN + c_switch → dtonicii9 delayamtN; the same 40 drives delay comp 1/2
   (maxdelay=51) which the dry-path iswitch6/7 select when live.
