"""R30 S9u: THE POLLER fn 0x1800E5160 — full decode of the id 7..13
read-out sequence + its callers."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)
from r29_lib import scan_calls

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

dump(0x1800E5160, 0x1800E5240, 'poller head')
dump(0x1800E5500, 0x1800E55D0, 'ids region 1 (7..11-ish)')
dump(0x1800E56C0, 0x1800E57D0, 'ids 12/13 region')

print('===== callers of the poller 0x1800E5160 =====')
for (va, flo, fhi) in scan_calls(pe, md, 0x1800E5160):
    print('  call at 0x%X in fn 0x%X..0x%X' % (va, flo, fhi))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
