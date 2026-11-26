"""R30 S9q: which vtables host the pollers 0x1801445A0/5D0/600, and which
slots — then the virtual call sites of those slots."""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, vtable_col,
                     parse_type_descriptor, demangle_msvc, vtable_slots,
                     looks_like_codeptr, TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

POLLERS = (0x1801445A0, 0x1801445D0, 0x180144600)
print('===== vtable membership =====')
slotinfo = {}
for fn in POLLERS:
    for sec in ('.rdata', '.data'):
        for s in pe.sections:
            if s['name'] != sec:
                continue
            blob = pe.data[s['ro']:s['ro'] + s['rsize']]
            off = 0
            while True:
                i = blob.find(struct.pack('<Q', fn), off)
                if i < 0:
                    break
                va = pe.image_base + s['va'] + i
                start = va
                while True:
                    prev = pe.read_u64(start - 8)
                    if prev is None or not (0x180001000 <= prev < 0x18037D000):
                        break
                    start -= 8
                slot = (va - start) // 8
                col = vtable_col(pe, start)
                td = parse_type_descriptor(pe, col['td']) if col else None
                print('  0x%X in vtable 0x%X slot %d owner=%s'
                      % (fn, start, slot, demangle_msvc(td['name']) if td else '?'))
                slotinfo[slot] = start
                off = i + 8

print()
print('===== call sites of those slots =====')
for slot, vt in sorted(slotinfo.items()):
    disp = slot * 8
    pat = re.compile(r'^call +qword ptr \[(\w+) \+ 0x%x\]$' % disp)
    n = 0
    for (flo, fhi) in [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]:
        for ins in bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo):
            m = pat.match('%s %s' % (ins.mnemonic, ins.op_str))
            if m:
                n += 1
                fb = fn_bounds(ins.address)
                print('  slot %d (+0x%X): call at 0x%X in fn %s'
                      % (slot, disp, ins.address,
                         ('0x%X..0x%X' % fb) if fb else '?'))
    print('  slot %d total: %d' % (slot, n))
    print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
