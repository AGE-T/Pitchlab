# R29 FRESH FINDINGS — Host COM Surface + Non-Immediate Id Census (static continuation of R28)

Target: `upload/Ox.vst3` (PE32+ x64, 5,307,392 bytes)
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
— verified before AND after every probe (s6a..s6k, s7a, s7b). The target was
never executed. Claim grammar: OBS / DER / UNK / CONFLICT (vs. an earlier
round's verdict — earlier documents stay byte-intact; corrections live here
and in the R29 report).

Evidence base: `octavox-work/r29/evidence/` — r29_s6a_exports.txt,
r29_s6b_comlatency.txt, r29_s6c_qi_cluster.txt, r29_s6d_vtable_walk.txt,
r29_s6e_qi_full.txt, r29_s6f_dispatch.txt, r29_s6g_iap_vtable.txt,
r29_s6h_ctor.txt, r29_s6i_extent.txt, r29_s6j_engine_iap.txt,
r29_s6k_vtableB.txt, r29_s7a_nonimmediate.txt, r29_s7b_id13_context.txt.
Scripts: r29_lib.py + r29_s6*.py + r29_s7*.py (12 probes).

Process notes: the analysis sandbox had been RESET between sessions (working
tree + /tmp reverted to an R24-era snapshot; the R25-R28 deliverables
survived only in git + on the repository remote). Recovery: fetch from the
remote (branch `r26-28-static-audit` @ 41c7628c, pushed 2026-11-26T13:11Z),
extract the R28 tree from local git objects, re-verify the target hash, then
continue. The git access token is stored OUTSIDE the repository
(`~/.secrets/`, `~/.git-credentials`, mode 600) and is never committed.
Bulk scans use a detail=False+skipdata disassembler (the r26 toolchain's
detail=True variant costs ~600 MB/MB-of-code and OOMs the sandbox on
full-.text passes; r29_lib.parse of rip targets from op_str is equivalent).

---

## F1 — U-2: the host-facing COM surface (the R28-defined hunt, executed)

**F1.1 [CONFLICT, s4f resolved] The TRUE export table (spec-correct parse).**
Three named exports: `ExitDll` ord#0 @ 0x180194B70 (the `mov al,1; ret` bool
stub that s4f had mis-mapped to GetPluginFactory), **`GetPluginFactory` ord#1
@ 0x180194440**, `InitDll` ord#2 @ 0x180194B80. The s4f name→VA mapping was
built with buggy ordinal handling; R28 F4.1's "four consecutive stubs"
observation is explained (they are ExitDll/InitDll and friends).

**F1.2 [OBS] GetPluginFactory @ 0x180194440.** Singleton-guarded (global
factory ptr + `flag = 2` stamp); first call allocates a **0x1F0-byte JUCE
factory object** (operator new 0x18015E63C) and builds std::strings starting
"Eventide" (vendor), then the JUCE module registration. `fn 0x180194BB0`
(the F4.2 "24-byte singleton") = the ctor of a **24-byte {vftable 0x1803EEF98,
GUID} C++ node** — the IID-constant storage class, NOT a COM factory: its
slot 0 (0x180003A90) is a **deleting destructor** (`test dl,1; mov edx,0x18;
call operator-delete`), not a queryInterface. All ~48 GUIDs the qIs compare
against live in exactly this node format in `.data` 0x1804A6120..0x1804A6560
(named in bulk via the vendored SDK DECLARE_CLASS_IID table — see F1.5).

**F1.3 [OBS] The class IDs are generated at RUNTIME from the plugin name.**
`fn 0x18030A010` (the F4.8 "CID-by-index") builds the CID STRING: "%06X" of
`0x565300 | ('T' or 'E')` (i.e. 565354/565345 — a VST3-class tag), "%08X" of
0x4F637457 ('OctW' hash), then "%02X" per lowercased char of **"Octavox"** —
the JUCE-generated processor ('T') / edit-controller ('E') class IDs. There
is NO static class-registration array — this is why the R28 F4.3
createInstance walk dead-ended.

**F1.4 [OBS] The processor class is named by RTTI: `juce::JuceVST3Component`.**
A 0x480-byte broker object, ctor **0x18000BEF0** (called from the 0x480
allocation site 0x18000EEEA). The ctor installs SIX vtable views:
`[+0x00]=0x1803EF8A0` (primary; slot0 = the outer qI directly),
`[+0x08]=0x1803EF580`, `[+0x10]=0x1803EFDC0`, `[+0x18]=0x1803EF0D0`,
`[+0x20]=0x1803F01E8`, `[+0x28]=0x1803F0210`; allocates and stores the
**0x28A0-byte Eventide engine (ctor 0x1801A7620) at +0x38**; keeps the host
context at +0x40 (addRef'd); the vendor interface slot at +0x48; a 0x108-byte
state-chunk object with tag `'prst'` wrapping the engine pointer; self-linked
anchors at +0x120/+0x248.

**F1.5 [OBS] The dual-path queryInterface.** Outer qI **0x18000C230**
(reached from every subobject view via the adjustor thunks `sub rcx,
{8,0x10,0x18,0x20}` = 0x180016ED0/0x180016F98/0x180016DB4/0x180016E40):
(1) **aggregation FIRST**: `rcx=[this+0x38]` (the engine), walk call
**0x180160D5C** (an RTTI-based subobject walk: reads `[vtable-8]` =
CompleteObject Locator, offsets +4/+8) with the descriptor pair at
0x18037FE80 ({slot0=0x18015EAD0 dtor-family, slot1=NULL} + the array-walker
0x18015EAFC), then query the result via **slot-2** (thunk 0x180016E14 =
`mov rax,[rcx]; jmp [rax+0x10]`) with the host's IID; (2) **fallback**: the
inner qI **0x18000EC90** — a 7-IID compare chain with a STRUCT-RETURN ABI
(qI(this=rcx, ret24=rdx {tresult@0, iface@+8, addRef-forwarder 0x180015F10
@+0x10}, iid=r8)):
  FUnknown / IPluginBase / IComponent → this+0
  **IAudioProcessor → this+8**
  IUnitInfo → this+0x10
  (32-bit-compared IID @ 0x1804A6600-region) → this+0x18
  IProcessContextRequirements → this+0x20
  vendor "TideOctW…" (GUID halves 0xEF01ABCD0101ABAB / 0x5774634F65646954)
    → `[this+0x48]` (a member object)
  else → E_NOINTERFACE {1, NULL, NULL}.

**F1.6 [OBS] The broker's own +8 view cannot serve a sane latency.**
Vtable A = 0x1803EF580 (installed at +8): 11 clean slots (qI-thunk, addRef-,
release-thunks, initialize 0x18000E310, terminate 0x18000E4A0,
getControllerClassId 0x18000E500, setIoMode 0x18000E540, getBusCount
0x18000E550, getBusInfo 0x18000E660, getRoutingInfo 0x18000E8C0, activateBus
0x18000E680) — then the RTTI Complete Object Locator of the NEXT vtable at
+0x58 (0x180419D58, .rdata) — i.e. A is an IComponent-shaped 11-slot view,
NOT a 20-slot IAudioProcessor vtable. A host calling slot 17 (+0x88) on the
+8 view would land in the adjacent vtable B (0x1803EF5E0: slot0=0x180004220,
slots 9..14 = the shared stub 0x18016028C, slot 17 = 0x180199260 = an
addRef-pattern `lock xadd [rcx+8]`) — B is a base-class vtable (its user
0x180015F20 is a recursive destruction chain that reinstalls B during
teardown). The linear +0x88 read on A's base (0x180003BA0 =
`mov rax,[rcx+8]; ret`) returns `[this+0x10]` — **the next view's vtable
pointer** (≈0x1803EFDC0 ≈ 6.4e9 "samples") — the fallback surface is
structurally non-viable as a latency source.

**F1.7 [DER, high] The host-facing IAudioProcessor is served by the
aggregation path.** The outer qI tries the engine-side walk FIRST (F1.5);
the broker's own views lack a real IAudioProcessor vtable (F1.6); the plugin
works in real hosts (no 6.4e9-sample latency reports anywhere) — therefore
the engine-side aggregated object answers IAudioProcessor, and its
getLatencySamples (slot 17) is the host-facing latency source. The terminal
object of the walk (the slot-2 GUID-query target) is the LAST-HOP UNK:
  - the engine (0x28A0-byte class, ctor 0x1801A7620) installs its own
    vtables {0x1803F05D0, 0x1803F04E8, 0x1803F03B8, 0x1803F05A0,
    0x1803F0478, 0x1803F23C8, 0x1803F0E68, 0x1803F0B00, 0x1803F2A78,
    0x1803F2948}; none has a clean 20-slot IAudioProcessor layout
    (0x1803F05D0's +0x88 = 0x1800055F0 = the shared `xor al,al; ret` stub;
    0x1803F04E8/0x1803F03B8's +0x88 = 0x18030C400 = a float-clamp);
  - the engine's release-family (0x180004490: `lock xadd [rcx+8],-1`, destroy
    via `[vtable+0x20]`) and adjustor cluster (subs {0x10..0x78} into
    {0x180004490, 0x1800048E0, 0x180004480, 0x1800074B0}) map its many
    subobjects; the R28 engine (vtable 0x180409F60, m01/m02) is a DIFFERENT
    object from this 0x28A0 aggregation host.

**F1.8 — U-2 R29 VERDICT: architecture CLOSED to the last hop.** True export
table (F1.1); factory chain (F1.2/F1.3); processor class identity + layout
(F1.4); dual-path qI with the full IID→offset map (F1.5); the fallback
surface proven non-viable (F1.6); the host-facing service located on the
aggregation path (F1.7). Residual (one hop): the walk-result object's slot-2
GUID-query implementation and the IAudioProcessor vtable it returns →
its slot-17 body = the numeric latency the host receives. Search shape for
R30: follow 0x180160D5C's offset arithmetic for the descriptor at
0x18037FE80 / dump the slot-2 entries of the engine's ten vtables /
xref the DebugReport "LatencySamples" consumer side (the host-side attribute
reader is the same value per F4.6-R28 — the serialised field is never
written, so the host-side path is the only live one).

---

## F2 — U-3: the non-immediate id census (the R28-defined search, executed)

**F2.1 [OBS] The demux argument shift was the missing immediate surface.**
R28 F1.4 scanned only `call [reg+0x10]` with small immediate rdx. But the
container thunks are `slot3(+0x18) = children[[this+0x10]+rdx*8]->slot1(r8d,
r9)` and `slot4(+0x20) = children[...]->slot2(r8d, r9)` — at slot-3/4 call
sites THE ID RIDES IN R8D. The R29 census (r29_s7a, single detail=False
streaming pass over .text): immediate ids in 7..13 now cover slots 1-4:
  slot 3 id 12: 0x1801E2288, 0x1801E240C — **stream READ lengths** (the
    R28 F1.4 classification stands; slot-3 = IBStream read(buf,len));
  slot 4 id 13: 0x1801AB235 — **writer length** (context 0x1801AB1E0..2B0 =
    an HTTP/multipart header builder: `mov r8d,0x28; "…Conn…",
    mov r8d,0xd; '"; filename="', mov r8d,3; mov r8d,0xe; "Content-Type: "`
    — the F1.10 text-writer family, NOT the m02 feed);
  slot 4 id 10: 0x18022C2A2, 0x18022C529 — **formatting parameters**
    (the 0x18022Cxxx region = the putc/formatter layer, `mov dl,0x20` /
    0x1801C7B50 / 0x1801C7A30 string-float formatters).
  → **no genuine immediate id-12/13 m02 feed exists on ANY slot shape.**

**F2.2 [OBS] The non-immediate census: 1337 sites.** All slot-call sites
whose id register is register/memory-sourced (or defined too far back to
slice in 22 instructions) are enumerated in r29_s7a evidence with a
backward def-slice classification (imm/src/def/far per site). This is the
haystack for the final narrowing; the R28 prediction ("the id must travel
in a register loaded from memory/a loop variable") is now backed by a
complete negative on the immediate surface.

**F2.3 — U-3 R29 VERDICT: surface EXHAUSTED (negative), census delivered.**
The ids 12/13 (the ∓cents correction telemetry, R28 F1.6) are provably not
fed by any immediate at any dispatch shape. Next narrowing (R30): filter the
1337-site census by receiver class (sites whose receiver derives from the
R28 engine's known offsets or the UI/telemetry layer that consumes the
floats), then backward-slice the id producer. The paired id-12/13 consumer
is expected to be a UI-refresh loop (the twin fields engine+0xA588/0xA58C
read by adjacent case bodies).

---

## Standing-UNK table (R28 → R29 delta)

| id | item | R28 | R29 |
|---|---|---|---|
| U-2 | host latency | narrowed; COM hunt defined | **architecture closed to the last hop**: exports fixed, factory + JuceVST3Component broker + dual-path qI + IID map decoded; fallback surface non-viable; host-facing = aggregation path; terminal object = the one-hop residual (F1.8) |
| U-3 | m02 12/13 publisher | search shape defined | **immediate surface exhausted (negative, all 4 slot shapes incl. the demux arg shift)**; 1337-site non-immediate census delivered (F2.3) |
| U-7 | userscale | CLOSED (R28) | unchanged |
| U-10 | c_minmax | CLOSED (R28) | unchanged |
| U-1, U-4, U-5, U-6, U-8, U-9, U-11, U-12 | — | — | unchanged |

## New architecture facts (not in R26-R28)

- The binary's COM layer = JUCE wrapper (JuceVST3Component broker, 0x480 B)
  around Eventide's module engine (0x28A0 B) with an RTTI-walk aggregation
  query path and a 7-IID fallback with a {status, iface, addRef-forwarder}
  struct-return ABI — including a vendor interface keyed by the GUID
  "TideOctW…" (0xEF01ABCD0101ABAB / 0x5774634F65646954).
- The IID constant pool: ~48 24-byte {vftable, GUID} nodes in .data
  0x1804A6120..0x1804A6560, all named via the SDK's DECLARE_CLASS_IID
  table (IPluginBase…IAttributes2, incl. IVst3ToAUWrapper / IVst3ToAAXWrapper
  / IVst3ToVst2Wrapper — the JUCE multi-format wrapper set).
- The class IDs are string-derived at runtime ("565354|565345" tag +
  'OctW' hash + hex("octavox")) — no static class array exists.
- The qI addRef forwarder idiom: `lea rax,[rip→0x180015F10]; …;
  movsd [rdx+0x10], xmm1` where 0x180015F10 = `mov rax,[rcx];
  jmp [rax+8]` (call the receiver's own addRef).
- Bulk-scan discipline: detail=True capstone costs ~600 MB per MB of code —
  the full-.text scans must use detail=False + op_str rip parsing
  (r29_lib.get_bulk_disassembler / scan_text).
