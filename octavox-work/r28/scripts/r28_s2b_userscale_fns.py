"""R28 S2b: identify the two functions referencing the userscale table block
(0x1800C9990, 0x1800C9F10) and the param-record container + applier target
of the userscale initializer (rsi+0x3E0 in FUN_1800E3350).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800C9990, 0x1800C9AB0, 'fn 0x1800C9990 (refs 0x1803BB5F0)')
dump(pe, md, 0x1800C9F10, 0x1800CA030, 'fn 0x1800C9F10 (refs 0x1803BB5F0)')
dump(pe, md, 0x1800E3350, 0x1800E3420, 'FUN_1800E3350 prologue (identify rsi)')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
