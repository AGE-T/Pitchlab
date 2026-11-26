"""R29 S6f: who dispatches the processor qI — callers, bare thunks, cluster ptrs (U-2, leg 6).

s6e: qI 0x18000EC90 = qI(this=rcx, ret24=rdx{status, iface, addRefFwd}, iid=r8);
IAudioProcessor -> this+8; IUnitInfo -> +0x10; ?(+0x18); IProcessCtxReq -> +0x20;
vendor "TideOctW" -> [this+0x48]; else E_NOINTERFACE. The qI qword appears
NOWHERE in the image (no static vtable slot) => dispatched via bare jmp
thunks (E9 rel32, no sub — e.g. 0x180016E80 jmp 0x180004490) or direct calls.

This probe:
 (a) all direct callers of 0x18000EC90 (scan_calls) + of the addRef forwarder
     0x180015F10;
 (b) ALL bare `jmp rel32` thunks targeting 0x18000EC90 (regex over .text);
 (c) every .rdata/.data qword pointing into the qI cluster 0x18000EC00..0x18000EF00
     (any pointer-table entry) -> the containing array base + neighbours;
 (d) dump the callers' contexts.

Static only. Hash checked pre+post.
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     scan_calls, fn_bounds, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()

print('===== (a) direct callers =====')
for tgt in (0x18000EC90, 0x180015F10):
    print('--- target 0x%X ---' % tgt)
    hits = scan_calls(pe, md, tgt)
    if not hits:
        print('  (no direct call/jmp sites)')
    for (site, lo, hi) in hits[:20]:
        print('  call/jmp at 0x%X  (fn 0x%X..0x%X)' % (site, lo, hi))

print('\n===== (b) bare jmp rel32 thunks -> 0x18000EC90 =====')
fo_lo = pe.va2fo(TEXT_LO)
blob = pe.data[fo_lo:fo_lo + (TEXT_HI - TEXT_LO)]
found = []
for m in re.finditer(rb'\xe9(....)', blob, re.S):
    rel = struct.unpack('<i', m.group(1))[0]
    va = TEXT_LO + m.start()
    if va + 5 + rel == 0x18000EC90:
        found.append(va)
print('  bare thunks: %s' % (['0x%X' % v for v in found] or 'NONE'))

print('\n===== (c) .rdata/.data qwords into the qI cluster 0x18000EC00..0x18000EF00 =====')
for secname in ('.rdata', '.data'):
    sec = [s for s in pe.sections if s['name'] == secname][0]
    base = pe.image_base + sec['va']
    blob2 = pe.data[sec['ro']:sec['ro'] + sec['rsize']]
    for off in range(0, len(blob2) - 8, 8):
        v = struct.unpack_from('<Q', blob2, off)[0]
        if 0x18000EC00 <= v < 0x18000EF00:
            va = base + off
            print('  [%s] 0x%X: 0x%X' % (secname, va, v))
            # neighbourhood: dump 4 qwords before/after
            for d in range(-4, 5):
                vv = struct.unpack_from('<Q', blob2, off + 8 * d)[0]
                print('      0x%X (+%d): 0x%X' % (va + 8 * d, 8 * d, vv))

print('\n===== (d) caller contexts =====')
seen = set()
for tgt in (0x18000EC90,):
    for (site, lo, hi) in scan_calls(pe, md, tgt)[:6]:
        if lo in seen:
            continue
        seen.add(lo)
        print('--- fn 0x%X..0x%X (caller of the qI) ---' % (lo, hi))
        for i in disasm_range(pe, md, lo, min(hi, lo + 0x120)):
            print(' ', annotate(pe, i))
        print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
