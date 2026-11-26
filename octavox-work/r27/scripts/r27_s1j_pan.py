"""R27 S1j: the master's caller FUN_1800EBAF0 — wrapper-side stereomixer3 param source."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()
dump(pe, md, 0x1800EBAF0, 0x1800EC0A9, 'CALLER FUN_1800EBAF0 (1465 B) — around call site 0x1800EBD90')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
