# R26 Appendix B — ENGINE MEMORY MAP (dtonicii9 dsp_t)
Source: fresh R26 disassembly (prepare FUN_1800E2030, ctor FUN_1800E0360, writer FUN_1800E0190,
state-7 FUN_1800DFE80, m01/m02) + r22 corpus. All offsets engine-relative (`engine` = the dsp_t
object at module-wrapper +0x430); tracker fields additionally given as T-relative where T =
tracker = engine+0x4518. Classification per row: OBS (instruction/byte-level this round),
OBS-r (reconfirmed), DER.

## B.1 Object identity
| item | value | class |
|---|---|---|
| engine object | wrapper+0x430 (factory FUN_180137E20; ctor FUN_1800E0360; dtor FUN_1800E7DF0) | OBS |
| vtable | 0x180409F60 = {0x1800E7DB0 dtor, 0x1800E10B0 m01/setParam, 0x1800E1520 m02/getParam, 0x1800C5750, 0x1800C5770, 0x1800C5790, 0x1800C57B0} | OBS-r |
| base class | modpp::dsp_shared_t, vtable 0x180409F20 | OBS-r |
| tracker | engine+0x4518, called once per prepare (0x1800E20CE) with r8b=1 (masked-mode flag) | OBS |

## B.2 Rings and buffers
| offset | contents | notes (VA of a fresh site) |
|---|---|---|
| +0x98 | record ring data, 4096 floats, mask 0xFFF | consumer sites 0x1800E2600/2630/264A; writer 0x1800E0276..034B |
| +0x409C | work-ring stride (4096/8192/16384 floats by SR class <50k/50–100k/≥100k) | OBS-r |
| +0x40A0 | work-ring data | writes 0x1800E226B..0x1800E231F (`−(g_v·input[(D_v+k')&M])`, −0.0 xorps) |
| +0x40B8 | work-ring mask | OBS-r |
| +0x40C0 | per-voice work-ring write-ptr array (wptr[v] at [base+4·v]; read 0x1800E2249, updated 0x1800E2329) | OBS |
| +0x4140 | int_delay per voice, init 280 | OBS-r |
| +0x4158 | crossfade ramp state per voice (init 0, settles −1) | OBS-r |
| +0x4170 | auxiliary index array (per voice) | read 0x1800E23E3/0x1800E23F2 |
| +0x4188 | read position double[nv] (`pos`) | OBS-r |
| +0x41A0 | input-history ring data | 4-sample writes per prepare 0x1800E204A..0x1800E20C0 |
| +0x41B8 | input-ring write index (−4/block) | OBS-r |
| +0x41C0 | input-ring mask (pow2 ≥ sr·0.001·maxdelay_ms; FUN_18013E200) | OBS-r |
| +0x41E8 | meter block {period, e_raw, e_filt} (m02 ids 7..13 read [a4e0]→meter ptr) | OBS-r |
| +0x4208/+0x4230 | gain current/target | OBS-r |
| +0x4258 | D_v = int[nv] voice delays (read 0x1800E2200–0x1800E2222 in prepare) | OBS |

## B.3 Voice parameter pages and slots
| offset | contents |
|---|---|
| +0x4280 | page A base (44 floats × 8 voices; port 21) |
| +0x42A8 | page B base (port 22) |
| +0x42D0 | page selector array (port 23; 1 → B; prepare select 0x1800E2444–0x1800E2456) |
| +0x42F8 | record read index recidx[v] (port 24; consumer 0x1800E25DB/0x1800E25F0) |
| +0x40F8 | f-slot vector<float> (page[10] per voice; read [begin+4·v] 0x1800E2348) |
| +0x4110 | crossfade-decrement slot (page[12+idx]·2⁻²³; write 0x1800E269F region) |
| +0x4128 | rate-slot vector<float> (page[0]·correction; write 0x1800E269F) |
| +0x40DC..+0x40E8 | quartic P coefficients c4..c0 (Horner chain 0x1800E2371–0x1800E23D9) |

## B.4 Published tables (ports 25/26/27)
| offset | contents |
|---|---|
| +0x4320 | port 25 = 16-float table (reverse prefix of port-26 rows) |
| +0x4370 | port 26 = 72-float table (12 rows × 6: [degreeIdx, idx2, ratio, walkA, **interval**, walkB]) — row field 4 = the interval index (record i5 = int(field4); field 5 = i4 dead value) — R26 refinement |
| +0x44A0 | port 27 = int[12] stability mask (write gate bit (i4+1); stability constant read at [table+4]) |
| tracker caches | T+0x6018 (+8) port 25; T+0x6028 (+0x18) port 26; T+0x6038 port 27; **T+0x6058 = port-26 data ptr (state-7 matched-row position, steps=12 at T+0x6060)** |

## B.5 Tracker field map (T-relative; engine = T−0x4518)
| T-offset | contents (freshly verified unless noted) |
|---|---|
| +0x000 | 243-float analysis array (cursor +0x5F40) |
| +0x800 | 512-float detection ring (mask 0x1FF) |
| +0x11C0 | per-lag previous array |
| +0x5DB0 | 64-float raw ring (mask 0x3F) |
| +0x5EB0 | FIR coefficients (6/10/20 by SR class; blocks +0xA3C8..+0xA3F0 via m01 id 5) |
| +0x5F14 | cadence reload {2,4,8} (engine+0xA42C) |
| +0x5F20 | peak decay 0.99219 |
| +0x5F28 | adaptive-gate multiplier (m01 id 4 target; applier writes 2/3 or sentinels) |
| +0x5F30/+0x5F34/+0x5F38 | 18 / 1/v / v·243 (m01 id 6) |
| +0x5F6C | **m01 id 0 target — no reader located: UNK field** |
| +0x5F70/+0x5F74 | minp pair v / v+2 (m01 id 1; acceptance window) |
| +0x5FD0 | gate level (m01 id 2; engine+0xA4E8) |
| +0x5FD4 | live period (bit-copied into record slot W−4; state-6 scaling upstream) |
| +0x5FD8/+0x5FE0/+0x5FE4/+0x5FE8 | record ring ptr / mask 0xFFF / write idx (engine+0xA4FC) / masked-mode flag (=1 from engine call) |
| +0x5FEC | write-gate threshold (6e-6) |
| +0x5FF8/+0x5FFC | FSM state / per-state countdown (init 81) |
| +0x6008 | 2048.0 (ω = 2π·2048/T̂) |
| +0x6018..+0x6060 | port caches (above) |
| +0x606C/+0x6070/+0x6074/+0x6078 | ω / running gain (reset 1.0 on |Δω|≥4e-4) / correction target (writers: state 7) / stability counter (≤0x7FFFFF) |
| +0x6044/+0x6048 | per-step clamp pair 2^(±1/(12·correctrate)) (m01 id 17; ln2/12 = 0.057762265) |
| +0x604C/+0x6050 | walk window exp(±x·ln2/1200) (m01 id 16) |
| +0x6064/+0x6068 | hysteresis tolerances (m01 id 15 zeroes; state-7 uses 1.6e-4/4e-4 scales) |

## B.6 Other engine fields
+0x54 (nv), +0x60/+0x68 (input buffer / port table; mod port = [engine[0x68]+8+0x10·v], element [0] = F),
+0x80 (voice-output port table, stride 0x18), +0x44E0/+0x44F8 (glide scalars), +0x4510
(pitch-tracking enable; =0 ⇒ correction 1.0, interval 0), +0xA4E0 (meter ptr),
+0xA50C (1/128), +0xA51C (1), +0xA520 (2048.0), +0xA524 (sample rate; m01 id 5),
+0xA558 (steps; m01 id 14), +0xA564/+0xA568, +0xA55C/+0xA560, +0xA57C (m01 ids 16/17/15),
+0xA58C/+0xA588 (correction target/gain = m02 ids 12/13 sources), +0xA484 (m01 id 0, UNK reader).
