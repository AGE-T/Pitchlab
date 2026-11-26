"""R28 S1i: dump the DebugReport builder region (fn 0x180347500) and the other
subscriber-registrar sites, hunting the generic module query path (m02 ids).
Also check engine+0x10 semantics against the R26 memory map context.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x180347500, 0x180347900, 'DebugReport builder fn 0x180347500 (subscribe x4)')
dump(pe, md, 0x180155700, 0x1801559BA, 'registrar fn 0x180155700 (subscribe x4)')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
