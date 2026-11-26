"""R27 S1n: eqn add_fn FUN_180137AC0 — the EQ composite structure."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()
dump(pe, md, 0x180137AC0, 0x180137C30, 'EQN ADD_FN FUN_180137AC0 (full)')
print('=== does it call the stereomixer3 composite ctor 0x180137720? ===')
for site, lo, hi in scan_calls(pe, md, 0x180137720):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
