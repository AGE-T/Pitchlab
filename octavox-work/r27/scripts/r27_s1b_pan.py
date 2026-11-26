"""R27 S1b: pan law — identify callees + caller; full context for closed form.

0x180180200 (R26: 'atan2f'), 0x180350400 (R26: 'sin/cos family'),
0x18013E590 (result consumer), caller of FUN_18013DD34.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x180180200, 0x180180260, 'THUNK 0x180180200 (R26: atan2f)')
dump(pe, md, 0x180350400, 0x1803504C0, 'TRIG CALLEE 0x180350400 head')
dump(pe, md, 0x180351690, 0x1803516E0, '0x180351690 (R26: sqrtf)')
dump(pe, md, 0x18013E590, 0x18013E640, 'CONSUMER FUN_18013E590')

print('=== callers of FUN_18013DD34 (pan fn) ===')
for site, lo, hi in scan_calls(pe, md, 0x18013DD34):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
    dump(pe, md, lo, hi, 'CALLER fn 0x%X..0x%X' % (lo, hi))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
