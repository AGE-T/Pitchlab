"""R28 S3: decode the c_minmax / c_minmax2 module semantics (U-10 residual).

add_fns: c_minmax = 0x1800D15D0, c_minmax2 = 0x18013AB50.
Dump both + locate the per-block update functions they wire.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import PE, check_hash, get_disassembler, dump, scan_calls

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800D15D0, 0x1800D16E0, 'c_minmax add_fn 0x1800D15D0')
dump(pe, md, 0x18013AB50, 0x18013AC60, 'c_minmax2 add_fn 0x18013AB50')

# callers of the add_fns (executor wiring sites)
for fn in (0x1800D15D0, 0x18013AB50):
    hits = scan_calls(pe, md, fn)
    print('callers of 0x%X: %d' % (fn, len(hits)))
    for a, lo, hi in hits[:6]:
        print('    0x%X in fn 0x%X..0x%X' % (a, lo, hi))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
