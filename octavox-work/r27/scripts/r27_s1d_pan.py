"""R27 S1d: pan law — the EVEN nPairs path (stereomixer3: ninputs=8 -> 4 pairs).

Dump 0x18013DF1E..0x18013E070 (function continuation) + the coefficient consumer
side: find who reads [obj+0x20]/[obj+0x24]/[obj+0x34] (mix-family audio loop).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x18013DF1E, 0x18013E070, 'PAN FN EVEN PATH 0x18013DF1E..0x18013E070')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
