"""R29 S6d: adjustor map -> IAudioProcessor vtable -> getLatencySamples (U-2, leg 4).

s6c decoded the processor qI (0x18000EC90): IAudioProcessor match ->
lea rax,[rcx+8]  =>  the IAudioProcessor subobject sits at this+8.
The MI layout means the vtable at [this+8] has as its slot 0 an ADJUSTOR
THUNK `sub rcx, 8; jmp 0x18000EC90` (the F4.3 thunk cluster).

This probe:
 (a) names every GUID in the .data registry (full SDK DECLARE_CLASS_IID table);
 (b) dumps the qI tail 0x18000ED80..0x18000EE40 (remaining blocks + out-store);
 (c) scans ALL of .text for `sub rcx, imm8; jmp rel32` adjustor thunks,
     listing every thunk whose target is the processor qI 0x18000EC90
     (and, for context, targets 0x180004490 / 0x1800074B0 / 0x18019C2D0);
 (d) searches .rdata/.data for vtables containing those thunks/qIs as slot 0
     -> dumps candidate vtables;
 (e) from the IAudioProcessor vtable (the one whose slot0 is the `sub rcx, 8`
     adjustor), reads slot 17 (+0x88) = getLatencySamples and dumps its body.

Static only. Hash checked pre+post.
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     vtable_slots, looks_like_codeptr, find_bytes,
                     guid_bytes_encA, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()

# ---------------- (a) full SDK IID table -> name the .data registry ----------------
SDK_DIR = '/home/z/my-project/octavox-work/vst3sdk/pluginterfaces'
iid_by_bytes = {}
pat = re.compile(r'DECLARE_CLASS_IID\s*\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)')
for root, _, files in os.walk(SDK_DIR):
    for fn in files:
        if not fn.endswith('.h'):
            continue
        try:
            txt = open(os.path.join(root, fn), 'r', errors='replace').read()
        except OSError:
            continue
        for m in pat.finditer(txt):
            nm = m.group(1)
            q = tuple(int(m.group(i), 16) for i in range(2, 6))
            iid_by_bytes[guid_bytes_encA(q)] = nm
print('===== (a) SDK IID table: %d IIDs; .data registry naming =====' % len(iid_by_bytes))
reg_lo, reg_hi = 0x1804A6120, 0x1804A6560
for va in range(reg_lo, reg_hi, 24):
    vt = pe.read_u64(va)
    if vt != 0x1803EEF98:
        continue
    g = pe.read(va + 8, 16)
    nm = iid_by_bytes.get(g, '?UNKNOWN?')
    gs = '-'.join('%08X' % x for x in struct.unpack('<IIII', g))
    print('  0x%X  %s  %s' % (va, gs, nm))

# ---------------- (b) qI tail ----------------
print('\n===== (b) processor qI tail 0x18000ED80..0x18000EE40 =====')
for i in disasm_range(pe, md, 0x18000ED80, 0x18000EE40):
    print(' ', annotate(pe, i))

# ---------------- (c) adjustor thunk map ----------------
print('\n===== (c) adjustor thunks (sub rcx, imm8; jmp rel32) in .text =====')
fo_lo = pe.va2fo(TEXT_LO)
fo_hi = pe.va2fo(TEXT_HI)
blob = pe.data[fo_lo:fo_hi]
adj_re = re.compile(rb'\x48\x83\xe9(.)\xe9(....)', re.S)
by_target = {}
for m in adj_re.finditer(blob):
    n = struct.unpack('<b', m.group(1))[0]
    rel = struct.unpack('<i', m.group(2))[0]
    thunk_va = TEXT_LO + m.start()
    tgt = thunk_va + 9 + rel
    by_target.setdefault(tgt, []).append((thunk_va, n))
INTERESTING = (0x18000EC90, 0x180004490, 0x1800074B0, 0x18019C2D0,
               0x18000FB70, 0x180004F60, 0x18000C230, 0x1800048E0,
               0x180004480, 0x18000C1F0, 0x18000C200)
for t in INTERESTING:
    thunks = by_target.get(t, [])
    print('  target 0x%X  <- %s' % (t, ['thunk 0x%X (sub rcx, 0x%X)' % (va, -n) for (va, n) in thunks] or 'none'))

PROC_QI = 0x18000EC90
proc_thunks = by_target.get(PROC_QI, [])
print('\n  processor-qI adjustors: %s' % ['0x%X (sub 0x%X)' % (va, -n) for (va, n) in proc_thunks])

# ---------------- (d) vtables containing the qI / its adjustors as slot 0 ----------
print('\n===== (d) vtable discovery (slot-0 search) =====')
slot0_cands = [PROC_QI] + [va for (va, n) in proc_thunks]
vt_hits = []
for cand in slot0_cands:
    for hit in find_bytes(pe, struct.pack('<Q', cand), ('.rdata', '.data')):
        vt_hits.append((hit, cand))
        print('  qword 0x%X (slot0 cand 0x%X) at VA 0x%X  [%s]' % (cand, cand, hit, pe.sec_of(hit)))

# also: every thunk in the cluster may itself be a slot0 — check targets of all
# thunks that jump into the qI cluster region 0x18000EC00..0x18000EE00
print('  (qI-cluster-targeting thunks, for the layout map:)')
for t, lst in sorted(by_target.items()):
    if 0x18000EC00 <= t < 0x18000EE00:
        print('    -> 0x%X : %s' % (t, ['0x%X(sub 0x%X)' % (va, -n) for (va, n) in lst]))

# ---------------- (e) IAudioProcessor vtable -> slot 17 (+0x88) ----------------
print('\n===== (e) candidate IAudioProcessor vtables =====')
for (vt_va, cand) in vt_hits:
    slots = vtable_slots(pe, vt_va, 20)
    if not all(looks_like_codeptr(pe, v) for v in slots[:18]):
        print('  vtable 0x%X (slot0=0x%X): not 18 code slots — skip' % (vt_va, cand))
        continue
    print('  vtable 0x%X  slot0=0x%X' % (vt_va, cand))
    for i, v in enumerate(slots):
        mark = '   <<< getLatencySamples (slot 17, +0x88)' if i == 17 else ''
        if i in (0, 1, 2, 3, 4, 11, 12, 13, 14, 15, 16, 17, 18, 19):
            print('    slot %-2d +0x%02X  0x%X%s' % (i, 8 * i, v, mark))
    lat = slots[17]
    print('  --- getLatencySamples body 0x%X ---' % lat)
    for i in disasm_range(pe, md, lat, lat + 0x60):
        print('    ', annotate(pe, i))
    print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
