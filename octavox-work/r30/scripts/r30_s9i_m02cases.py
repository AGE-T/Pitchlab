"""R30 S9i: m02 case 12/13 bodies — verify the +0xA588/+0xA58C base register
and the full body logic."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

dump(0x1800E1520, 0x1800E1560, 'm02 head (dispatch)')
dump(0x1800E1740, 0x1800E1890, 'case 12 + case 13 + tail')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
