"""R30 S9d: callers of the engine ctor 0x1800E0360 — who embeds the engine."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds)
from r29_lib import scan_calls

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== callers of engine ctor 0x1800E0360 =====')
sites = scan_calls(pe, md, 0x1800E0360)
for (va, flo, fhi) in sites:
    print('  call at 0x%X in fn 0x%X..0x%X' % (va, flo, fhi))
print()
for (va, flo, fhi) in sites[:6]:
    lo = max(flo, va - 0x80)
    dump(lo, va + 0x30, 'context 0x%X' % va)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
