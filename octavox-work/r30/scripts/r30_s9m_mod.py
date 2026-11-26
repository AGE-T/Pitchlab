"""R30 S9m: the A508 module — 0x180138d30 (process) and 0x180138b00 (reset):
the writes to alias+0x80/+0x84 (engine+0xA588/+0xA58C)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label, mark=None):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        tag = ''
        if mark and any((m in ('%s %s' % (i.mnemonic, i.op_str))) for m in mark):
            tag = '   <<<'
        print(' ', annotate(pe, i), tag)
    print()

for fn in (0x180138d30, 0x180138b00):
    bb = fn_bounds(fn)
    lo, hi = bb if bb else (fn, fn + 0x400)
    print('===== fn 0x%X (bounds 0x%X..0x%X, %d B) =====' % (fn, lo, hi, hi - lo))
    dump(lo, hi, 'body', mark=('+ 0x80]', '+ 0x84]'))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
