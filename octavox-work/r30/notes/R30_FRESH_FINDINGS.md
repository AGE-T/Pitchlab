# R30 FRESH FINDINGS — The Last Two UNKs Closed (U-2 numeric, U-3 poller chain)

Target: `upload/Ox.vst3` (PE32+ x64, 5,307,392 bytes)
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
— verified before AND after every probe (s8a..s8k, s9a..s9u; 22 evidence
files; all passes read-only). The target was never executed. Claim grammar:
OBS / DER / UNK / CONFLICT (vs. an earlier round's verdict — earlier
documents stay byte-intact; corrections live here and in the R30 report).

Evidence base: `octavox-work/r30/evidence/` — r30_s8a_tdnames.txt,
r30_s8b_rtti.txt, r30_s8c_vst3ce_dead.txt, r30_s8d_latency.txt,
r30_s8e_producer.txt, r30_s8f_callers.txt, r30_s8g_slot75.txt,
r30_s8h_flag.txt, r30_s8i_vtmember.txt, r30_s8j_callctx.txt,
r30_s8k_s84100.txt, r30_s8l_modes.txt, r30_s8m_staticmodes.txt,
r30_s9a_poller.txt, r30_s9b_cand1.txt, r30_s9c_enginehome.txt,
r30_s9d_enginectl.txt, r30_s9e_430.txt, r30_s9f_430b.txt,
r30_s9g_a588.txt, r30_s9h_a588wide.txt, r30_s9i_m02cases.txt,
r30_s9j_a5region.txt, r30_s9k_a508.txt, r30_s9l_a508ctx.txt,
r30_s9m_mod.txt, r30_s9n_d2b10.txt, r30_s9o_shape.txt,
r30_s9p_pollhelper.txt, r30_s9q_pollvt.txt, r30_s9r_envt.txt,
r30_s9s_holder.txt, r30_s9t_newslots.txt, r30_s9u_poller.txt.
Scripts: r30_lib.py (adds MSVC x64 RTTI walkers: TypeDescriptor / COL / CHD /
BCD, all-RVA-relative) + 22 probe scripts.

New in r30_lib: `parse_type_descriptor`, `parse_col`, `parse_chd`,
`parse_bcd`, `vtable_col`, `class_bases`, `demangle_msvc`, `td_matches`.

---

## F1 — U-2 CLOSED: the host latency, numerically

**F1.1 [OBS, s8a] The outer qI's "aggregation walk" is a
`dynamic_cast<juce::VST3ClientExtensions*>(juce::AudioProcessor*)`.**
The outer qI 0x18000C230 @ 0x18000C272 calls 0x180160D5C with
(rcx = [broker+0x38] = the engine, rdx = 0, r8 = 0x1804B7448,
r9 = 0x1804B73E0, isRef = 0) — the exact MSVC `__RTDynamicCast`
signature. Both .data arguments are TypeDescriptors sharing the type_info
vftable 0x18037FE80; their names: **SRC = `juce::AudioProcessor`**,
**DST = `juce::VST3ClientExtensions`** (s8a (b)). The dispatch thunk
0x180016E14 (`mov rax,[rcx]; jmp [rax+0x10]`) then calls the cast result's
vtable slot +0x10 with (sub, iid, &out) — JUCE's
`VST3ClientExtensions::queryIidExtensions(const TUID, void**)`.

**F1.2 [OBS, s8c] THE CAST IS DEAD CODE — no class in the image derives
VST3ClientExtensions.** A BCD scan for the DST TypeDescriptor's RVA
(0x4B73E0) across .rdata/.data/_RDATA finds ZERO BaseClassDescriptors
referencing it: the TD exists only as the __RTDynamicCast argument. The
Eventide processor class does not implement JUCE's client-extensions hook,
so the aggregation path always returns NULL and the outer qI always falls
through to the inner 7-IID chain (0x18000EC90).
**[CONFLICT vs R29 F1.7]** — R29's "the host-facing IAudioProcessor is
served by the aggregation path" is WITHDRAWN: the live path is the fallback,
IAudioProcessor → broker+8.

**F1.3 [OBS, s8b] The engine class is `VoxAudioProcessor` — full RTTI
hierarchy decoded.** Primary vtable 0x1803F05D0 (installed at engine+0x00 by
ctor 0x1801A7620) → COL 0x180417210-ish → TypeDescriptor 0x1804BB8D0 =
`.?AVVoxAudioProcessor@@`. Base list (CHD attrs=1, nbases=10):

| # | offset | class | note |
|---|--------|-------|------|
| 0 | +0x000 | VoxAudioProcessor | most-derived |
| 1 | +0x000 | tide::DigiPluginBase | |
| 2 | +0x000 | tide::ExtendedAudioProcessor | |
| 3 | +0x000 | tide::JuceAdapter | |
| 4 | +0x000 | juce::AudioProcessor | the __RTDynamicCast SRC type |
| 5 | +0x150 | tide::MeterInterface | |
| 6 | +0x158 | juce::Timer | |
| 7 | +0x170 | DSPStatusBroadcaster | |
| 8 | +0x2600 | juce::ActionBroadcaster | |
| 9 | +0x2810 | juce::ChangeListener | |

**F1.4 [CONFLICT, s8c/s8d — the R26→R29 "slot 17" premise was a phantom.]**
The vendored SDK's `IAudioProcessor` (ivstaudioprocessor.h) declares EIGHT
own methods — setBusArrangements, getBusArrangement, canProcessSampleSize,
**getLatencySamples**, setupProcessing, setProcessing, process,
getTailSamples — so an IAudioProcessor vtable has exactly **11 slots**
(3 FUnknown + 8) and **getLatencySamples is slot 6 (+0x30)**, NOT slot 17
(+0x88). The broker's +8 view (vtable 0x1803EF580) has EXACTLY 11 code
slots — it IS the IAudioProcessor vtable (R29 had mis-assigned IComponent
names to its bodies and mis-read the +0x88 fallthrough). The qword at
0x1803EF580+0x58 (0x180419D58) is the COL of the ADJACENT
`Steinberg::String` vtable (owner TD verified, s8c (c)) — the linear
+0x88 read lands in String's vtable, which is why every "+0x88" hunt since
R26 dead-ended.

**F1.5 [OBS, s8d] The host-facing getLatencySamples = 0x18000E540
(slot 6 of vtable 0x1803EF580):**

```asm
0x18000E540  mov  rax, qword ptr [rcx + 0x30]   ; [broker+8+0x30] = broker+0x38 = the engine
0x18000E544  mov  ecx, dword ptr [rax + 0x3c]   ; int32 at VoxAudioProcessor+0x3C
0x18000E547  xor  eax, eax
0x18000E549  test ecx, ecx
0x18000E54B  cmovg eax, ecx                     ; return max(0, [engine+0x3C])
0x18000E54E  ret
```

All 11 slots of the view cross-identify cleanly (setBusArrangements 390 B,
getBusArrangement 82 B, canProcessSampleSize 50 B, setupProcessing 271 B,
setProcessing 28 B, process 88 B, getTailSamples 96 B — s8d (c)).

**F1.6 [OBS, s8e/s8f] The producer = JUCE-shape `setLatencySamples`
0x180260220 — with exactly ONE caller in the image.**
`cmp [rcx+0x3c], edx; je ret; mov [rcx+0x3c], edx; … {int,bool} …
call 0x18025DA50 (listener fanout, 0x18025DA50..6E reads [this+0x24]
count and iterates)`. Sole call site 0x1801A619D, inside fn
0x1801A5F40..0x1801A6464 = **VoxAudioProcessor primary vtable slot 58
(+0x1D0)** (s8i) — a prepare/mode-dispatch method:

```asm
0x1801A616A  movsd  xmm0, [rdi+0x30]        ; double sampleRate (DER: only
0x1801A6172  cvttsd2si edx, xmm0            ;   reading that fits the formula)
0x1801A6179  comiss xmm6, 0.5               ; the flag float (see F1.8)
0x1801A6182/8B  mov byte [rdi+0x2864], 1/0
0x1801A6192  call [rax+0x258]               ; slot 75 = the latency formula
0x1801A619D  call 0x180260220               ; setLatencySamples(this, result)
```

The tag dispatch at the fn head selects the mode: 'live' (0x6C697665) →
the latency block; 'inst', 'keys', 'klck', 'mctl', 'mclk' → other paths
(none calls setLatencySamples — s8f: the ONLY call site in .text is
0x1801A619D, in the 'live' branch).

**F1.7 [OBS, s8g] The formula — vtable slot 75 = 0x1801A32D0:**

```asm
0x1801A32D0  cmp  byte [rcx+0x2864], 0     ; the flag from F1.6
0x1801A32D9  mov  eax, 8                    ; flagged → 8 samples
0x1801A32DE  ret
0x1801A32DF  cvtdq2ps xmm0, edx             ; (float)sampleRate
0x1801A32E6  mulss xmm0, 0.04f              ; 40 ms
0x1801A32EE  addss xmm0, 0.5f               ; rounding
0x1801A32F6  cvttss2si eax, xmm0
0x1801A32FA  add  eax, 8                    ; + 8 samples
0x1801A32FD  ret
```

**U-2 ANSWER: host latency = max(0, [VoxAudioProcessor+0x3C]) where the
'live'-mode prepare sets it to `flag ? 8 : (int)(sampleRate·0.04 + 0.5) + 8`
— i.e. 40 ms of pitch-shifter window + 8 samples of pipeline overhead
(44.1 kHz → 1772; 48 kHz → 1928; 96 kHz → 3848), or a flat 8 samples when
the flag is set.**

**F1.8 [DER, s8h/s8j/s8k/s8l] The flag's plumbing.** Slot 58 is called
from three trampolines (0x180316B20, 0x1803188B0/900, 0x1801E2CD2 — the
last passes float 0.0, likely the state-restore path). The 0x180316B20
trampoline first calls slot 100 (+0x320) = 0x1803187B0 — a
**mode-object dispatch**: `children[[this+0x2698]+idx*8]->vtable[0]`
returns the float (index validation against [this+0x27dc], tag compare at
[entry+0x10] via slot 84 = 0x1803180A0 `findModeIndex(tag)`). The mode
names are string-table built ("live" @0x18040867C, "keys" @0x1804086AC,
"inst" @0x1804093C8 — s8l/s8m; the objects themselves are runtime-built,
not static). Residual semantics of the >0.5 float: the per-mode virtual's
"value" — consistent with a dry/bypass-style indicator (flagged → only 8
samples of pipeline latency, no 40 ms window needed). R30 leaves the
float's exact parameter identity as a NAMED, non-blocking residual
(was: the whole of U-2).

**F1.9 [OBS] Cross-checks.** (a) The DebugReport "LatencySamples"
serialiser (0x180311A4D region) reads the R28-documented NEVER-written
field (env+0x25F4) — R28's "diagnostic = vestigial" stands; the LIVE
value is the +0x3C path. (b) The VoxAudioProcessor ctor does not
explicitly initialise +0x3C (s8b grep: no [rsi+0x3c] store; only [rsp+0x3c]
stack ops) — before the first 'live' prepare the field holds
constructor-path residue; in the normal host lifecycle setupProcessing →
prepareToPrepare → 'live' prepare runs before any latency query.

---

## F2 — U-3 CLOSED: the m02 ids-12/13 poller chain, end to end

**F2.1 [OBS, s9c/s9d] The dsp engine is PER-VOICE.** The engine ctor
0x1800E0360 (installs vtable 0x180409F60 at [this]; child vector at
+0x10..0x20; owner back-pointer at +0x28) is called — via embedder
0x180137E20 (`lea rcx,[rsi+0x430]`) — from the VOICE processors
0x1800F00A0, 0x180109D40, 0x180122E00 (and their twins): **each voice
object owns its engine at voice+0x430**. The R28 "engine" (m01/m02,
vtable 0x180409F60) is the per-voice module-tree root.

**F2.2 [OBS, s9g–s9n] The ±cents twin fields are real but write-hidden.**
`engine+0xA588/+0xA58C` are read ONLY by m02's case 12/13 bodies
(0x1800E17E8 / 0x1800E1758) — no literal write exists anywhere. The writes
go through the **interior alias at engine+0xA508** (12 `lea [base+0xA508]`
sites, s9k): the A508 sub-module is processed per block via
`0x180138D30(alias, [engine+8], buffer, 0xFB)` → `0x1800D2B10(alias, …)`
→ a virtual `param->slot3(param, alias+0x48)` whose callee writes through
the alias — the module is the pitch/correction stage whose outputs are the
twin fields. (Named, non-blocking residual: the exact store instruction
inside the param-object virtual.)

**F2.3 [OBS, s9o–s9r] THE POLLER HELPERS — `modpp::simple::env_t`
vtable 0x18040AE10 slots 15–18:**

```asm
0x1801445D0  mov  rax, [rcx+0x58]        ; env_t's child array
             mov  r9d, r8d               ; id  ← arrives in r8d
             mov  edx, edx               ; idx (voice index)
             lea  r8, [rsp+0x38]         ; &out
             mov  rcx, [rax+rdx*8]       ; children[idx] = the voice's ENGINE
             mov  edx, r9d               ; id
             mov  rax, [rcx]             ; engine vtable 0x180409F60
             call [rax+0x10]            ; ← m02(id, &out)  (slot 2)
             movss xmm0, [rsp+0x38]     ; return the float
```

Slot 15 (0x1801445A0) returns the int; slot 16 (0x1801445D0) the float;
slots 17/18 (0x180144600) the double. RTTI names the class
**`modpp::simple::env_t`** — the "environment" host whose children are the
per-voice engines. The env_t view is installed at [holder+0x260] by ctor
0x180142B60 (primary vtable 0x18040AB28; +0x26C = 120.0f).

**F2.4 [OBS, s9t — the missing immediate surface] THE IDS ARE IMMEDIATE —
at a displacement the earlier censuses never scanned.** A scan of
`call [reg+{0x78,0x80,0x88,0x90}]` (env_t slots 15–18) with backward
r8d def-slices finds, in fn **0x1800E5160..0x1800E5999**:

| site | call | r8d (id) |
|------|------|----------|
| 0x1800E552D | [+0x80] slot 16 | 7 |
| 0x1800E5545 | [+0x80] slot 16 | 8 |
| 0x1800E555D | [+0x80] slot 16 | 9 |
| 0x1800E5576 | [+0x80] slot 16 | 10 |
| 0x1800E558F | [+0x80] slot 16 | 11 |
| 0x1800E571C | [+0x80] slot 16 | **12** |
| 0x1800E5779 | [+0x80] slot 16 | **13** |

The R28 census scanned `call [reg+{0x8,0x10,0x18,0x20}]` with immediate
rdx; the R29 census added the r8d demux shift but kept the same four
displacements. **The poller calls env_t slot 16 (+0x80) with the id as the
THIRD argument (r8d)** — a fifth dispatch shape that no earlier scan
covered. The "non-immediate id" premise of R28 F1.4/R29 F2.3 was an
artefact of the uncovered surface. [CONFLICT resolution: both rounds'
negative results stand for THEIR surfaces; the id was immediate all along.]

**F2.5 [OBS, s9u] THE POLLER fn 0x1800E5160 — full behaviour.** Called from
the voice processors (0x180106B2F in fn 0x1801061A0, 0x1801206EF in fn
0x18011FD60 — block-rate, per voice). It holds {env_t* at [this+0], voice
index at [this+8], cached results at +0x2F8/+0x378/+0x398, listener arrays
at +0x300/+0x380/+0x3A0}. Sequence: ids 7..11 polled unconditionally
(results into xmm7..xmm9 with a 1000.0/v9-style conversion and a
change-detect on id 7 against [this+0x2F8]); **ids 12/13 polled only under
a gate** (`cvttss2si; test eax,eax; jns skip` @ 0x1800E5705 — the
correction-active condition computed from [this+0x190/0x194/0x1B8]); each
new value is change-detected (`ucomiss xmm0, [this+0x378/0x398]`) and on
change **fanned out as dirty bits**: `for l in [this+0x380..0x388]:
lock or [l->bits], rbx << l->shift` — the R26-era atomic dirty-bit UI
refresh, now tied to its producer.

**F2.6 — U-3 VERDICT: CLOSED.** The m02 ids-12/13 feed is the per-voice
telemetry poller 0x1800E5160 → `modpp::simple::env_t::get_child_float
(voiceIndex, id)` (slot 16, id immediate in r8d) → the voice's engine m02
case 12/13 (∓cents of engine+0xA58C/+0xA588) → change-detect → dirty-bit
listener fanout. Every earlier round's "publisher" framing is resolved:
m02 is a PULL interface; the "publisher" is this block-rate poll loop
inside the voice processors.

---

## Standing-UNK table (R29 → R30 delta)

| id | item | R29 | R30 |
|---|---|---|---|
| U-2 | host latency | architecture closed to the last hop | **CLOSED (numeric)**: max(0,[VoxAP+0x3C]); 'live'-prepare sets `flag ? 8 : round(0.04·sr)+8`; getLatencySamples = 0x18000E540 (slot 6, NOT 17) (F1.5–F1.7) |
| U-3 | m02 12/13 publisher | immediate surface exhausted (negative); 1337-site census | **CLOSED**: poller 0x1800E5160 via env_t slot-16 (+0x80, id=r8d IMMEDIATE — the uncovered 5th dispatch shape) → per-voice engine m02 → dirty-bit fanout (F2.4–F2.6) |
| U-7 | userscale | CLOSED (R28) | unchanged |
| U-10 | c_minmax | CLOSED (R28) | unchanged |
| U-1, U-4, U-5, U-6, U-8, U-9, U-11, U-12 | — | — | unchanged (U-11's double-conversion is now explained by the poller's gate+convert usage) |

## Named non-blocking residuals (new, small)
- R-1: the exact store instruction for engine+0xA588/+0xA58C inside the
  param-object virtual called from 0x1800D2B10 (the A508 module's writer).
- R-2: the semantic identity of the >0.5 flag float (F1.8) — the per-mode
  virtual's value; behaviour (8 vs 40 ms+8) is fully specified regardless.
- R-3: the writer of [VoxAudioProcessor+0x30] (the sampleRate double) —
  read-side formula confirms it; the store is somewhere in the
  setupProcessing/prepareToPlay chain.

## New architecture facts (not in R26–R29)
- The processor is `VoxAudioProcessor` (RTTI), built on
  tide::DigiPluginBase → tide::ExtendedAudioProcessor → tide::JuceAdapter →
  juce::AudioProcessor (+ MeterInterface, Timer, DSPStatusBroadcaster,
  ActionBroadcaster, ChangeListener) — the JUCE/tide layering named.
- The JUCE VST3 wrapper's IAudioProcessor view is vtable 0x1803EF580, 11
  slots, getLatencySamples at slot 6 (+0x30); the broker's six views and
  their COLs fully enumerated (all owned by juce::JuceVST3Component).
- The VST3ClientExtensions dynamic_cast in the JUCE wrapper qI is DEAD
  CODE in this build (no class derives it).
- `modpp::simple::env_t` = the named environment/container class whose
  slots 15–18 are typed child-value pollers (int/float/double) — the
  module-query protocol's consumer side, now named and located
  (vtable 0x18040AE10, child array at +0x58).
- The m02 dispatch protocol has a FIFTH call shape:
  `env_t->slot16(idx, id)` with the id immediate in r8d at displacement
  +0x80 — beyond the four shapes (slots 1–4) censused in R28/R29.
- The dsp engines are PER-VOICE (voice+0x430 each, ctor 0x180137E20 calls);
  the "engine" of R26–R28 is one per voice.
