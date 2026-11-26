"""R27 S1i: master fn callers + continuation + composite ctor full."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

print('=== callers of MASTER FUN_18013AF20 ===')
for site, lo, hi in scan_calls(pe, md, 0x18013AF20):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))

dump(pe, md, 0x18013B453, 0x18013B53E, 'MASTER CONTINUATION 0x18013B453..0x18013B53E')
dump(pe, md, 0x18013B53E, 0x18013B560, 'next chunk head')
dump(pe, md, 0x180137720, 0x1801377A0, 'COMPOSITE CTOR FUN_180137720')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
