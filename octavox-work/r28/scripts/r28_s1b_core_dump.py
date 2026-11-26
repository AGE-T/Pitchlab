"""R28 S1b: full dumps of the event-system core + the single callers of the
publishers, plus a caller-scan of those wrapper functions.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     dump, load_pdata, fn_bounds, scan_calls, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

# 1) full dumps of the event core
dump(pe, md, 0x1800C27E0, 0x1800C2956, 'PUBLISHER A FUN_1800C27E0 (slot +0x10 channel)')
dump(pe, md, 0x1800C2960, 0x1800C2CE0, 'PUBLISHER B FUN_1800C2960 (slot +0x18 channel)')
dump(pe, md, 0x1800C2CE0, 0x1800C2E00, 'DISPATCHER FUN_1800C2CE0')

# 2) the two wrapper functions that call the publishers
dump(pe, md, 0x1800C14C0, 0x1800C16A4, 'WRAPPER A fn 0x1800C14C0 (calls publisher A at 0x1800C162A)')
dump(pe, md, 0x1800C16B0, 0x1800C1805, 'WRAPPER B fn 0x1800C16B0 (calls publisher B at 0x1800C1771)')

# 3) who calls the wrappers?
for w in (0x1800C14C0, 0x1800C16B0):
    hits = scan_calls(pe, md, w)
    print('callers of wrapper 0x%X: %d' % (w, len(hits)))
    for a, lo, hi in hits:
        print('    call at 0x%X in fn 0x%X..0x%X' % (a, lo, hi))

# 4) m02 prologue: confirm the (module, id) signature and the return mechanism
dump(pe, md, 0x1800E1520, 0x1800E15A0, 'M02 FUN_1800E1520 prologue + id-7 switch head')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
