"""R30 S9c: where does the dsp engine (vtable 0x180409F60, m01/m02) live?
lea-references to the vtable in .text -> ctor/installer -> embedding."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, TEXT_LO, TEXT_HI,
                     opstr_rip_target)
import re

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

VT = 0x180409F60
print('===== lea/mov rip->0x180409F60 sites =====')
hits = []
for insn in bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_LO) + (TEXT_HI - TEXT_LO)], TEXT_LO):
    if 'rip' in insn.op_str:
        t = opstr_rip_target(insn)
        if t == VT:
            hits.append(insn.address)
            fb = fn_bounds(insn.address)
            print('  0x%X  (fn %s)' % (insn.address,
                  ('0x%X..0x%X' % fb) if fb else '?'))
print()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

for va in hits[:4]:
    fb = fn_bounds(va)
    if fb:
        lo = max(fb[0], va - 0x60)
        dump(lo, va + 0x30, 'context of 0x%X' % va)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
