"""R30 S9l: what is called with rcx = engine+0xA508?"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

for va in (0x1800F6583, 0x1801041BF, 0x180110223, 0x18011DD9F):
    dump(va - 0x30, va + 0x50, 'context of lea 0x%X' % va)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
