"""R30 S9r: modpp::simple::env_t — ctor (vtable 0x18040AE10 installer),
embedding offsets, and the m02-id feed sites."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, vtable_slots,
                     looks_like_codeptr, TEXT_LO, TEXT_HI, opstr_rip_target)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

VT = 0x18040AE10
print('===== lea rip->0x18040AE10 sites (env_t ctor/installers) =====')
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

for va in hits[:3]:
    fb = fn_bounds(va)
    if fb:
        dump(max(fb[0], va - 0x40), va + 0x40, 'installer ctx 0x%X' % va)

print('===== env_t vtable slots 12..20 =====')
slots = vtable_slots(pe, VT, 22)
for i in range(12, 20):
    v = slots[i]
    bb = fn_bounds(v) if v and looks_like_codeptr(pe, v) else None
    print('  slot %2d (+0x%02X): 0x%-12X %s' % (i, 8 * i, v or 0,
          ('%d B' % (bb[1] - bb[0])) if bb else ''))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
