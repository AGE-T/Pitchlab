"""R30 S8i: which vtable hosts the prepare fn 0x1801A5F40, and who calls
that slot."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, vtable_slots,
                     vtable_col, parse_type_descriptor, demangle_msvc)
from r29_lib import scan_text, opstr_rip_target
import re

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

PREPARE = 0x1801A5F40
print('===== (a) vtables hosting 0x1801A5F40 =====')
owners = []
for sec in ('.rdata', '.data'):
    for s in pe.sections:
        if s['name'] != sec:
            continue
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        off = 0
        while True:
            i = blob.find(struct.pack('<Q', PREPARE), off)
            if i < 0:
                break
            va = pe.image_base + s['va'] + i
            owners.append(va)
            off = i + 8
for o in owners:
    # vtable start: walk back to the entry right after the previous non-codeptr
    start = o
    while True:
        prev = pe.read_u64(start - 8)
        if prev is None or not (0x180001000 <= prev < 0x18037D000):
            break
        start -= 8
    slot = (o - start) // 8
    col = vtable_col(pe, start)
    td = parse_type_descriptor(pe, col['td']) if col else None
    print('  qword 0x%X: vtable 0x%X slot %d  owner-class=%s'
          % (o, start, slot, demangle_msvc(td['name']) if td else '?'))
print()

print('===== (b) call sites of [reg + slot*8] matching the slot =====')
for (o, start, slot) in [(o, o, 0) for o in owners]:
    pass  # filled after (a) known; do a generic scan for the computed slot
# find the slot number of the first owner
if owners:
    start = owners[0]
    while True:
        prev = pe.read_u64(start - 8)
        if prev is None or not (0x180001000 <= prev < 0x18037D000):
            break
        start -= 8
    slot = (owners[0] - start) // 8
    disp = slot * 8
    pat = re.compile(r'^call +qword ptr \[(\w+) \+ 0x%x\]$' % disp)
    print('  scanning call [reg+0x%X] (slot %d) in .text...' % (disp, slot))
    n = 0
    for insn in bmd.disasm(pe.data[pe.va2fo(0x180001000):pe.va2fo(0x180001000) + (0x18037D000 - 0x180001000)], 0x180001000):
        m = pat.match('%s %s' % (insn.mnemonic, insn.op_str))
        if m:
            n += 1
            if n <= 20:
                print('    0x%X  (fn %s)' % (insn.address, fn_bounds(insn.address) or '?'))
    print('  total: %d' % n)
print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
