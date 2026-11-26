"""R30 S9b: the prime m02-poller candidate fn 0x180097A20..0x180097CC1."""
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

dump(0x180097A20, 0x180097CC1, 'candidate poller fn')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
