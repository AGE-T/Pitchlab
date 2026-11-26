# R28 FRESH FINDINGS — Event/Query Protocol + Parameter Graph (static continuation of R26/R27)

Target: `upload/Ox.vst3` (PE32+ x64, 5,307,392 bytes)
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
— verified before AND after every probe (s1..s4 evidence collection + s5a/b/c/d
follow-ups). The target was never executed. Claim grammar: OBS / DER / UNK /
CONFLICT (vs. an earlier round's verdict — the earlier documents are left
byte-intact; corrections live here and in the R28 report).

Evidence base:
- s1 series (13 files): publisher/executor/context/wrappers/debugreport/slot34/slot1/vtable scans — event system.
- s2 series (7 files): userscale refs/fns/mapcheck — U-7.
- s3 series (10 files): cminmax/cminmax_update/update/tail/delayxf/delayxf2/delayprep/conv — U-10-residual + delay crossfade.
- s4 series (14 files): latency/stub/vtable/ctor/subtables/exports/factory/factoryvt2/create/create2/serialiser/latrefs/getter_scan — U-2.
- s5 series (new this session): `r28_s5_probe.py`, `r28_s5b_probe.py`, `r28_s5c_probe.py`, `r28_s5d_25f4.txt` — m02 jump-table decode, +0x3E0 reader scan, +0x25C8/+0x25F4 writer hunt, slot-4 site classification.

Environment note (process, not DSP): the analysis sandbox suffered three
working-tree resets during this round (after the R26/R27/R28-WIP line had been
committed and pushed as branch `r26-28-static-audit` — push-first strategy;
nothing was lost). The full r28 evidence tree survived via the /tmp mirror; the
target hash was re-verified after each recovery. The subagent analyses (s1..s4)
were run against the identical tree (git-identical, hash-verified).

---

## F1 — Event system reclassified (s1)

**F1.1 [OBS] Publisher topology: exactly one caller each — the registrar, not an event source.**
Publisher A `FUN_1800C27E0` has exactly one direct call site (0x1800C162A, inside
wrapper A `0x1800C14C0..0x1800C16A4`); publisher B `FUN_1800C2960` exactly one
(0x1800C1771, wrapper B `0x1800C16B0..0x1800C1805`); dispatcher `FUN_1800C2CE0`
is called only by publisher A. Wrapper B's only caller is wrapper A. There is NO
per-block "fire event" call site anywhere in .text.

**F1.2 [OBS] The wrappers are subscription-tree mutators; payloads are smart-pointer pairs (+ index on channel B).**
Wrapper A inserts a node into a container's ordered child vector (grow, memmove),
`lock inc [node+8]` (refcount), sets `[node+0x48]=container` (parent link), then
publishes `{&sp_container, &sp_node}` (2-field payload). Wrapper B removes a node
at index and publishes a 3-field payload on channel B. Publisher A walks the
+0x48 ancestor chain and fans out `call [subscriber_vtable+0x10](arg1, arg2)`.

**F1.3 [CONFLICT vs R27 F7.2] The router/publisher machinery is the report/section-tree OBSERVER system — not the feed for m02 ids 7..13.**
The only payloads ever published are stack-address smart-pointer pairs; in m02
(`lea eax,[rdx-7]; cmp eax,6; ja default`) such an argument would take the
default path immediately. All 16 wrapper-A call sites live in the report/host
layer (0x180155700, 0x180158280, 0x18024FF60, 0x180307C10, 0x180310820,
0x1803109E0, 0x1803146E0, 0x180347500 region), not the dsp region. R27's
mechanics (multicast calling [vtable+0x10]) remain correct; the classification
changes: this is a structure-change notification system for a tree of
report/config nodes (node class vtable 0x1800C1DB0).

**F1.4 [UNK, narrowed] U-3-residual: the publisher of m02 ids 12/13 still not found — but the router route is excluded.**
The full-.text scan of `call [reg+0x10]` with small immediate rdx found only ids
{1,1,1,1,1,2,2,4} — nothing in 7..13. The id-12 candidate (0x1801E240C,
`mov r8d, 0xc`) is a 12-byte stream READ (BE u16 tag parse), not an event id.
Per F1.3 the publishers cannot emit id events. Concrete next-search shape (for
R29): the id must travel in a register loaded from memory/a loop variable.

**F1.5 [OBS] m02's true signature and default-path module-tree forwarding.**
m02 = `(this=engine, rdx=id, r8=outFloatPtr)`; ids 7..13 via jump table at
0x1800E1890; default path forwards to engine vtable slot 4 (child demux) —
`children[id]->m02(0, out)`.

**F1.6 [OBS — s5b/s5c full decode] The m02 jump table (4-byte RVA entries) — ALL SEVEN read-outs decoded.**
| id | case VA | semantics |
|----|---------|-----------|
| 7 | 0x1800E1554 | **cents from C4**: gate f>7.105e-15, then log2((engine+0xA524·engine+0xA520)/f)·(1/261.6256) via log-helper 0x1801887D0, scaled by 1731.234049 (=1200/ln2); below gate → **−65535.0 sentinel** |
| 8 | 0x1800E15CB | **frequency ratio** (same fields, no log conversion) |
| 9 | 0x1800E161E | **Q23-quantised ratio**: int(x·8388607.0) over three fields (+0xA4B8, fallback +0xA4A8, then max with +0xA498), result = n/(n+8) |
| 10 | 0x1800E16A5 | **sqrt(frac·(1−2⁻²³))** of the pitch object's +4 field (0.9999998812035873 = (2²³−1)/2²³) |
| 11 | 0x1800E16DB | **confidence-like**: sqrt((u−t)/(u+7.1e-15)) · ([p]·2048.0/engine+0xA520) · 1.06467459859e-05, where u=[p+4]·(2²³−1), t=[p+8]·(2²³−1) |
| 12 | 0x1800E1751 | **−cents(engine+0xA58C)**, gated: if sqrt([p+4]·(1−2⁻²³)) < [engine+0xA4E8]·0.11513 (=ln10/20) via helper 0x180351F50 → 0; else if |v|>7.1e-15 → log-helper 0x180352DC0 ×(−1731.234) |
| 13 | 0x1800E17E1 | **+cents(engine+0xA588)** — identical body, reading the twin field, ×(+1731.234) |
Constants verified by direct rip-target decode (s5c): 1/261.6256=C4 Hz,
1731.234049=1200/ln2, 8388607.0=2²³−1, 0.9999998812035873=1−2⁻²³,
0.11513000000000001=ln(10)/20, 2048.0, 1.06467459859e-05, 7.105427357601002e-15,
−65535.0. The −65535 sentinel and the dB constant tie ids 7/12/13 to the
pitch-correction target family (R26 ledger's "target/gain read-outs" confirmed
and specified).

**F1.7 [OBS] Engine vtable fully decoded; m01/m02 unique to the engine; ~20 sibling vtables are container modules.**
Engine vtable 0x180409F60: slot0=0x1800E7DB0, slot1=0x1800E10B0 (m01), slot2=
0x1800E1520 (m02), slot3=0x1800C5750, slot4=0x1800C5770, slot5=0x1800C5790,
slot6=0x1800C57B0, slot7=0x180436AA8 (.rdata descriptor, UNK), slot8=
0x1800D68E0, slot9=0x1800C5730. Thunk semantics (s1e): slot3 =
`children[[this+0x10]+rdx*8]->slot1(r8d, r9)`; slot4 = `children[idx]->slot2
(r8d, r9)`; slot5 = `return [children[idx]+8] + r8d`; slot6 = `return [this+8]`.
Every vtable hosting these thunks is a container module with a child array at
+0x10.

**F1.8 [OBS] Five dispatch primitives; zero clones.**
Besides publishers A/B + dispatcher, two new primitives: `FUN_1800C2C70`
(slot-1 dispatcher that SKIPS the sender — "notify all except me") and
`FUN_1800C2B00` (single-level slot-5 publisher, single pointer arg). The exact
dispatch byte patterns occur exactly once each in the image — no per-profile
clones.

**F1.9 [OBS] The executor is a pair of twin direct-call pipelines; the complete module offset map.**
Twin fns 0x180103A10..0x18010523A and 0x18011D5F0..0x18011EE0E: parameter
injection into the module at container+0x1B988, then a fixed direct-call chain
(no vtables): 0x1800E8B50 (chain head = R26's dec2_voice_fn), 0x1800EA650,
0x180137E90, 0x1800D38B0, 0x1800D7F10 (MIX audio, level-gated by `comiss xmm6,
[+0x168]` — the R27 "executor sites" 0x180104CB4/0x18011E88E are this gate, not
calls), 0x1800D0D50, 0x180137F70 (SOFTCLIP dispatcher), 0x1800CEC80, … with
module base offsets {0x1B988, 0x1BA98, 0x1BCF0, 0x1BF80, 0x1C0A8, 0x1C4B0,
0x1C6A0, 0x1C780, 0x1CDF8, 0x1D2B8, …}.

**F1.10 [OBS] The slot-3/4 "twin channel" landscape on non-engine classes = an IBStream read/write family.**
Slot 3 (+0x18) = `read(buf,len)` on stream objects — a big-endian reader cluster
(u64 bswap, u32, u16, u8, bool, varint record) around 0x1801D0520..0x1801D07B0
plus the chunk parser 0x1801E2380 (6/12/16-byte reads) and the colour reader
0x18002BBC4. Slot 4 (+0x20) = `write(buf,len)` on text writers — the XML entity
escaper 0x1801BAA40 ("&amp;" 5, "&quot;" 6, "&gt;" 4, "&lt;" 4), the XML header
writer 0x1801BA200 ("<?xml version=…" 30). Slot 5 (+0x28) = putc. The s1f
"ids" were byte lengths. No dsp-module subscriber via slots 3/4 was found.

**F1.11 [OBS] DebugReport builder: the serialiser is a section factory in the report tree.**
fn 0x180347500 builds the "DebugReport" root, sets "PluginInfoComponentVersion"
= "1.0.1", subscribes four section objects (SystemProperties registrar
0x180155700; sections 0x180347170, FUN_180310F20 (the <PluginEnvironment
LatencySamples=…> serialiser), 0x1803109E0). Report/router nodes are 0x50-byte
objects, vtable 0x1800C1DB0.

**F1.12 [OBS/UNK] Slot 1 (+0x8) = m01 "set/notify" event channel; the only small-id feeds are 8 host-layer sites.**
m01 FUN_1800E10B0 has no direct callers (virtual-only). Slot-1 calls with small
immediate rdx: ids {1,4,3,1,1,1,1,2} in fns 0x18020AD80..0x1802214E0 (the
host-facing/serialisation region). 0x1802214E0 calls slot-1(edx=2) then
slot-2(edx=2) back-to-back (a paired set+get). Whether any site targets the
engine: UNK (no receiver identification in the evidence).

**F1.13 [DER, medium] The subscription model restated.**
The binary contains (a) a report-tree observer system (F1.3) and (b) a module
query protocol: the engine is the only class with custom slot-1/2 handlers;
containers forward ids to children via slots 3/4; the observed inbound calls
come from the host-facing layer. The engine's m02 ids 12/13 must be fed by a
virtual call whose id argument is not a small immediate.

---

## F2 — Userscale (U-7) CLOSED

**F2.1 [OBS] "userscale" is a named parameter registration, not a data map.**
Inside FUN_1800E3350 (pitch-config param-table initializer; first registration
"nvoices"): a 9-char std::string "userscale" is built and a 48-byte record
{f32 min/max/def via lookup quartet 0x18032B520/0x18032B420/0x18032B320/
0x18032B230; storage = config+0x3E0; id; applier} is appended to the config's
param vector (registration site 0x1800E4A43..0x1800E4B5C).

**F2.2 [OBS, CONFLICT vs R26/R27 U-7 framing] The suspected "userscale map at 0x1803BB60C" does not exist.**
The qword dump shows 0x1803BB5F0..0x1803BB60F is a 4-qword static descriptor
tuple for the SLEW parameter: {"slew", "1.0", 6, 0x1803BB380}. The claimed map
address 0x1803BB60C is the high dword of the pointer 0x1803BB380 — an artifact
of reading ints where the compiler laid out a pointer. The "map" hypothesis is
refuted by direct data inspection.

**F2.3 [OBS] Xref census: the whole region has exactly 2 readers — the slew/pslew/state_clear parameter setup functions (0x1800C9F10, 0x1800C9990).**
Nothing references the keymap at 0x1803BB610..0x1803BB65F directly; nothing
anywhere references any userscale table.

**F2.4 [OBS] The write path IS wired.**
At 0x1800E4B37 the registration calls applier-lookup 0x1800C4930 and installs
record.applier; then (pattern proven byte-identical by the "pslew" enqueue at
0x1800CA003..0x1800CA02B) pushes a 16-byte {storage_ptr, id} record into the
applier's dirty queue. The host/UI/preset path CAN set config+0x3E0.

**F2.5 [OBS + s5b] No reader anywhere: the +0x3E0 full-.text scan.**
The s5b scan found 38 '+0x3E0' displacement accesses in all of .text: the F2.1
registration LEA, an atomic init/cmpxchg in fn 0x1800E5999 (prefetchw; mov
rax,[rsi+0x3E0]; lock cmpxchg — initialisation, same fn that reads the scale
pattern table 0x1803BB780), a `mov [rdi+0x3E0], -1` stamp in fn 0x1800E2C00,
and 35 hits that are stack frames ([rbp/rsp+0x3E0]), other objects' fields,
vtable calls ([rax+0x3E0] — a different class with a 124-slot vtable), and
indexed arrays. **No float read of any +0x3E0 exists.** The harmony chain's
scale input is fed from the separate pattern table 0x1803BB780 via fn
0x1800E5999 (3 refs, all in that fn).

**F2.6 [OBS] Mapcheck: region layout and table inventory.**
0x1803BB610..0x1803BB654: keymap ints [0,1,1,2,3,3,4,5,6,6,7,8,8,9,10,10,11]
(chromatic→diatonic-degree, 17 entries); 0x1803BB690: 12-TET cents [0,100,…,
1100]; 0x1803BB6C0: just intonation [0,112,204,316,386,498,590,702,814,884,
996,1088] (16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 16/9, 15/8);
0x1803BB6F0: a second JI variant (25/24, 10/9, …). No inconsistency; the
userscale slot is simply absent from this layout.

**F2.7 — U-7 VERDICT: UI/parameter-legacy — registered-but-unconsumed.**
Write side wired (F2.4); read side: zero consumers (F2.5, closing the residual
that R28-s2 flagged — the +0x3E0 reader scan is now DONE); the "map" refuted
(F2.2). Not dead code; not a map; the engine ignores it. (Residual ~nil: a
bulk struct copy could in principle carry the value, but no consumer of any
copy exists either — no +0x3E0 float read anywhere.)

---

## F3 — c_minmax + delay crossfade (U-10-residual) CLOSED

**F3.1 [OBS] c_minmax is a named-port module ctor registering outputs "minimum" and "maximum" (VA 0x1800D15D0).**
The sibling 0x1800D1780 parses the patch property "ninputs" → module arity N is
patch-defined. Module layout: min port cell +0x30, min subscribers +0x38..+0x40,
max port cell +0x50, max subscribers +0x58..+0x60. An aggregator — N parameter
cells in, min/max out.

**F3.2 [OBS] All delay-family modules bind the same host sub-object at parameter-block+0x260.**
c_minmax2 (0x18013AB50) = `add rdx, 0x260; call 0x1800D15D0`; identical +0x260
binding for the delay_xf composite (0x180137630) and the 3-triple clamp variant
(0x1801377A0). One shared parameter-graph host for the three delay/feedback
systems. Callers of the add_fns sit only inside the three executor
mega-functions (0x1800F00A0, 0x180109D40, 0x180122E00).

**F3.3 [OBS] The int16 bound triple {−32768.0, +32767.0, 1.0} is the family-wide default clamp domain.**
Appears in c_minmax variant 0x18013ABD0 (+0x18/+0x1c/+0x20), the delay_xf
composite ctor (+0x48/+0x4c/+0x50), and the 3-triple module 0x1801377A0 (three
repeats at +0x10/+0x40/+0x70 with a single "out" port) — three clamped channels
+ one out ≙ the three delay/feedback systems (DER-medium on the mapping).

**F3.4 [OBS] update 0x1800D1D40 = per-entry clamped running min/max reduction (×4-unrolled).**
N = [+0x10]; entries at [+0x18] stride 0x30 {boundA, boundB, cellptr@+0x20,
default@+0x28}; value = cellptr ? *cellptr : default; each entry contributes
clamp(value, boundA, boundB) to both accumulators (min init +FLT_MAX, max init
−FLT_MAX). The ×4 unroll mirrors the framework 4-sample cadence.

**F3.5 [OBS] Output flow: cached-compare publish + atomic dirty-bit fanout.**
On change, the new min/max is stored to the port cell and every subscriber
{flags_word, bit_index} (stride 0x10) is poked: `lock or [rax], rdx` with
`rdx = 1 << bit`. (The s3d "tail" evidence is this output/publish tail — NOT a
delay/bypass audio tail; no bypass-tail or ring-flush behaviour exists in the
s3 series.)

**F3.6 [DER, high] Recompute is block-rate, event-driven.**
`lock cmpxchg [+8], old & ~1` — clean → return; the −1 init forces a first pass.
Executor wiring: cluster A = engine+0x6E0 (c_minmax) → 0x1800D1D40, +0x810 →
0x1800CBA00, +0x8A8 → 0x18013A9F0; cluster B = engine+0xB088 (c_minmax2) →
0x1800D1D40, +0xB100 → 0x1800CBA00, +0xB198 → 0x18013A250. Parameter edits
take effect at the next block boundary; the audio-side smoothing is the
crossfade ramp (F3.10/11).

**F3.7–F3.9 [OBS] delay_xf composite layout; reset/flush; prepare properties.**
ctor 0x180137630 (host = param+0x260): bound triple +0x48..+0x50; input cell
slot +0x68; sub-modules +0xA8 (ctor 0x1800CE0F0, prepare 0x1800CF080) and +0xF0
(ctor 0x1800D8230/update 0x1800D7400); entries +0x128 stride 0x18; buffer
+0x140. reset/flush 0x180137560: aligned wholesale buffer clear + zeroing of
16-byte (4-float) cells — crossfade state is per 4-sample group. prepare
0x1800CF080 parses "noutputs" (base-10 int) and "xfadetime" — the R27
xfadetime↔page[col+12] link now also confirmed on the delay module.

**F3.10 [OBS] Per-output tap setup: ms→samples then FIVE tap registrations.**
Per output: value from cell [+0x78]+idx·0x30; sample rate via host vt+8; then
five calls to host vt+0x98 with r9d = base−2, base−1, base, base+1, base+2 —
an old/new-tap + interpolation neighbourhood (consistent with the 256×6
fractional kernel's multi-tap needs).

**F3.11 [DER, high] The conversion is ms→SAMPLES (not dB→linear).**
`mulss xmm7, xmm6; mulss xmm7, xmm8(0.001)` = sr·ms·0.001 at 0x1800CFD43-47
and (clamped) 0x1800D0080-90 — the same formula as the delay-comp
FUN_180138A20, performed independently at ≥3 sites.

**F3.12 [DER, medium-high] The deglitch crossfade is a Q23 fixed-point linear ramp.**
fn 0x1800CFFB0 (dirty-gated like c_minmax): clamped ms→samples N, then
`2^23 / N` → cvttss2si → Q23 per-sample step installed via host vt+0x58. The
fade accumulator reaches 1.0 after exactly N samples; ramp state lives in the
4-float cells zeroed on reset. UNK-medium: whether the clamped input cell
(+0x68) is xfadetime or delayamt. No ring re-indexing — the crossfade is
read-tap side; the write ring remains FUN_180138B00.

**F3.13 — U-10-residual VERDICT: RESOLVED.**
c_minmax proper = a patch-arity min/max aggregator (NOT a clamp, NOT a
normalisation — no division by max exists in it; the only division in the
family is the ramp step 2²³/N). The clamp seen "in the parameter path" lives in
the delay_xf composite / 3-triple variant as inline clamps against the int16
triple. **Units: milliseconds, int16 scale (±32.767 s, step 1.0)** — pinned by
the downstream ms→samples conversion (F3.11). Remaining UNKs (wiring edges):
cell-level wiring from delayamtN/fbkN/fbkd/mfbk into the two engine instances;
xfadetime-vs-delayamt for the ramp input; the 5-tap consumption details; roles
of siblings 0x1800CBA00 / 0x18013A9F0 / 0x18013A250.

---

## F4 — Host latency / factory (U-2): false lead eliminated

**F4.1 [CONFLICT] The s4f export-table name→VA mapping is unreliable (buggy ordinal handling).**
"GetPluginFactory -> 0x180194B70" disassembles as `mov al, 1; ret` — a bool
stub; four consecutive stubs at 0x180194B70/B80/B90/BA0; a real factory
constructor sits at 0x180194BB0.

**F4.2 [OBS/DER] Factory object: fn 0x180194BB0 builds a 24-byte singleton, vtable 0x1803EEF98.**
The s4i slot naming is unsound (slot +0x08 = a .rdata pointer — misaligned
dump; s4h "dumped" code bytes as a vtable).

**F4.3 [CONFLICT] The "createInstance" walk failed: 0x180016EA0 is an MI adjustor thunk into a DESTRUCTOR (0x18019C2D0).**
The chain exports→factory→createInstance→processor ctor was never closed; the
0x180016E–0x180016F region is a dense adjustor-thunk cluster of some large MI
class.

**F4.4 [DER, high] The candidate "JuceVST3Component vtable 0x1803F05D0" is DISPROVEN — it is the internal 0x28A0-byte MI engine class.**
Slot +0x00 = 0x1800175D0 = a scalar/vector deleting destructor (re-installs 5
vtables, 4 member dtors, `operator delete(this, 0x28A0)`). A COM/IUnknown
vtable must have queryInterface at slot 0. Sub-vtable slot-0s are MSVC MI
destructor thunks. The "process" slot was a 5-byte flag setter; "getTailSamples"
was a virtual clone (allocates 0x420, calls 0x18030F650).

**F4.5 [CONFLICT] The s4 headline "getLatencySamples returns 0" is WITHDRAWN.**
It rested on slot +0x88 of the disproven vtable mapping to a shared `xor al,al;
ret` stub — which is also non-conformant for a uint32 return (upper EAX
undefined). No host-facing conclusion can be drawn from it. The s4b
"activateBus/setActive/… are stubbed" table falls with it.

**F4.6 [OBS] The serialiser is confirmed diagnostic-only; the LatencySamples source field is NEVER written.**
"LatencySamples" (string 0x1803B5BF0) has exactly ONE xref in the image:
0x180311A4D inside FUN_180310F20; value = `*(int32*)(env+0x24)`. The call site
(s5c): `mov rax,[r15+0xE8]; lea rbx,[rax+0x25C8]; lea rcx,[rbx+8]; call
0x180310F20` → **env = X+0x25D0**, so the value = [X+0x25F4]. The ctor fn
0x180312670 (which initialises X+0x25C0=0x101, X+0x25C8=self-ptr, X+0x25D0=a
rip function pointer, X+0x25F9=0) writes NOTHING at +0x25F4. The full-.text
scan for '+0x25F4' (s5d): **0 hits**. The DebugReport latency attribute
serialises a vestigial, never-written field — in practice 0 from fresh heap
pages.

**F4.7 [OBS] No latency-like getter exists (0-hit getter scans).**

**F4.8 — U-2 VERDICT: UNK, materially narrowed.**
What the host receives from IComponent/IAudioProcessor::getLatencySamples is
NOT proven by any round; the "0 stub" evidence is invalid (F4.5). The plugin's
own diagnostic attribute is provably vestigial (F4.6) — the plugin tracks NO
latency value anywhere in its own state. R29 next actions: fix the export
parser to pin the true GetPluginFactory; find the real COM vtables (slot-0
GUID-comparing queryInterface — candidate 0x180003A90 deserves a real
decompile); follow 0x18030A010 / 0x18019CA70 / 0x18019CA60 to the class
registration array and the processor Create; read the true IAudioProcessor
vtable's getLatencySamples slot.

**F4.9 [OBS] Slot-4 candidate sites classified (s5/s5b d2).**
0x1800044B2 = a COM Release()-style pattern (`lock xadd [rcx+8], -1`; on zero:
edx=1, call [vt+0x20] — a destroy/destruct virtual). 0x1800AEB85 = a
named-port value query (two string lookups → 16-byte value pair → call
[vt+0x20](this, 1, &pair)). The remaining sites are framework plumbing, not the
engine m02 id path.

---

## Updated standing-UNK table (R27 state → R28 state)

| id | item | R27 | R28 |
|---|---|---|---|
| U-1 | kernel generator closed form | UNK (characterised) | unchanged (not attacked; FD-bank characterisation stands) |
| U-2 | host-reported latency | UNK | **narrowed + false lead eliminated**: internal diagnostic = never-written field (practically 0); host-facing path unproven; "0 stub" claim withdrawn; COM-vtable hunt defined for R29 |
| U-3 | m02 12/13 publisher | mechanism closed (router) — **classification now corrected** | **router route EXCLUDED** (F1.3/F1.4); ids must arrive via non-immediate virtual calls; search shape defined |
| U-4 | m01 id0 semantics | closed (scan-base) | unchanged |
| U-5 | Live-Mode 40 unit | closed (40 ms) | unchanged |
| U-6 | pan law | closed (linear) | unchanged |
| U-7 | userscale path | declared, unwired | **CLOSED: UI-legacy parameter** — registered + applier-wired write side, ZERO readers (full +0x3E0 scan); "map" refuted |
| U-8 | hungarian OOB | closed (clamped) | unchanged |
| U-9 | MIDI/tempo/snapshot/locks | UI-layer negatives | unchanged |
| U-10 | c_minmax units | softclip/mix closed; c_minmax residual | **CLOSED: min/max aggregator, ms on int16 scale**; deglitch = Q23 linear ramp; 5-tap neighbourhood; ms→samples at ≥3 sites |
| U-11 | m02 12/13 double-conversion | DER prediction | **now specified**: ids 12/13 = ∓cents(0xA58C/0xA588) gated by a dB-domain confidence — the double conversion is cents→(dB-domain gate)→cents |
| U-12 | R25 archive | MISSING | unchanged (repo-side, not DSP) |

## New architecture facts (not in R26/R27)

- The event machinery = a report-tree observer system (subscribe/unsubscribe
  mutators + 2 publishers + 3 dispatchers incl. a sender-excluding slot-1
  dispatcher); the DSP side uses a module query protocol with container demux
  thunks — engine m01/m02 are the ONLY custom slot-1/2 handlers.
- m02's complete read-out set (F1.6) — the pitch-correction telemetry surface.
- The Q23 (2²³−1) fixed-point domain appears in THREE subsystems: the m02
  quantised read-outs (id 9/10/11), the delay deglitch ramp (F3.12), and
  (R27) the choice/mux values — a framework-wide convention.
- The twin executor pipelines with the complete module offset map (F1.9).
- The IBStream BE reader/writer family on slots 3/4 of stream classes (F1.10).
- The parameter-graph host at param+0x260 shared by the whole delay family,
  with two engine aggregation clusters (+0x6E0, +0xB088).
- The scale/temperament table inventory at 0x1803BB610..0x1803BB780 (keymap,
  12-TET, two JI variants, scale patterns) with fn 0x1800E5999 as the reader.
