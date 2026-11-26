"""R30 S8k: name the 0.5-flag — VoxAudioProcessor primary vtable slots 84
(+0x2a0) and 100 (+0x320)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds, vtable_slots)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

slots = vtable_slots(pe, 0x1803F05D0, 110)
for idx in (84, 100):
    v = slots[idx]
    print('===== slot %d (+0x%X) = 0x%X =====' % (idx, idx * 8, v or 0))
    bb = fn_bounds(v)
    if bb:
        print('  bounds 0x%X..0x%X (%d B)' % (bb[0], bb[1], bb[1] - bb[0]))
        dump(bb[0], min(bb[1], bb[0] + 0x100), 'slot %d' % idx)
    else:
        dump(v, v + 0x80, 'slot %d (no bounds)' % idx)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
