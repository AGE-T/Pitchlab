"""R30 S8f: U-2 producer callers — who calls setLatencySamples 0x180260220?

(a) .text rel32 call/jmp sites targeting 0x180260220;
(b) .rdata/.data qword references (vtable membership);
(c) each caller disassembled around the call site (the edx argument);
(d) the notification target 0x18025DA50 identified (bounds + head).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)
from r29_lib import scan_calls

pe = PE(); check_hash(pe)
md = get_disassembler()
SET_LAT = 0x180260220

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) direct call/jmp sites of 0x180260220 =====')
sites = scan_calls(pe, md, SET_LAT)
for (va, flo, fhi) in sites:
    print('  call/jmp at 0x%X  (fn 0x%X..0x%X)' % (va, flo, fhi))
print()

print('===== (b) vtable membership (qword refs in .rdata/.data) =====')
for sec in ('.rdata', '.data', '_RDATA'):
    for s in pe.sections:
        if s['name'] != sec:
            continue
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        off = 0
        while True:
            i = blob.find(struct.pack('<Q', SET_LAT), off)
            if i < 0:
                break
            va = pe.image_base + s['va'] + i
            print('  qword at 0x%X [%s] = 0x%X' % (va, sec, SET_LAT))
            off = i + 8
print()

print('===== (c) caller contexts =====')
for (va, flo, fhi) in sites[:8]:
    lo = max(flo, va - 0x100)
    dump(lo, va + 0x18, 'caller of setLatencySamples near 0x%X' % va)

print('===== (d) notification fn 0x18025DA50 =====')
bb = fn_bounds(0x18025DA50)
print('  bounds:', ('0x%X..0x%X' % bb) if bb else 'none')
if bb:
    dump(bb[0], min(bb[1], bb[0] + 0xC0), '0x18025DA50 head')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
