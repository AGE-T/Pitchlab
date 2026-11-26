"""R30 S9n: 0x1800D2B10 — the A508 module's param-applier; hunting the
stores to [this+0x80]/[this+0x84] (engine+0xA588/+0xA58C)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        mark = '   <<<' if (' + 0x80]' in i.op_str or ' + 0x84]' in i.op_str) else ''
        print(' ', annotate(pe, i), mark)
    print()

bb = fn_bounds(0x1800D2B10)
lo, hi = bb if bb else (0x1800D2B10, 0x1800D2B10 + 0x400)
print('===== 0x1800D2B10 bounds 0x%X..0x%X (%d B) =====' % (lo, hi, hi - lo))
dump(lo, hi, 'full')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
