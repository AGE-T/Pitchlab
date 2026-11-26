"""R30 S9s: the holder unification — callers of the engine embedder
0x180137E20 and the env_t ctor 0x180142B60."""
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

for fn in (0x180137E20, 0x180142B60):
    sites = scan_calls(pe, md, fn)
    print('===== callers of 0x%X: %d =====' % (fn, len(sites)))
    for (va, flo, fhi) in sites:
        print('  call at 0x%X in fn 0x%X..0x%X' % (va, flo, fhi))
    print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
