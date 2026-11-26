"""R30 S8j: caller contexts of slot-58 dispatch (0x1801E2CD2, 0x180316B60,
0x180318940) — the selector and xmm2 float argument."""
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

for (va, flo, fhi) in ((0x1801E2CD2, 0x1801E2A90, 0x1801E2E0F),
                        (0x180316B60, 0x1803145C7, 0x180314609),
                        (0x180318940, 0x180318500, 0x18031857A)):
    lo = max(flo, va - 0x90)
    print('===== caller fn 0x%X..0x%X call @ 0x%X =====' % (flo, fhi, va))
    dump(lo, va + 0x10, 'context')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
