"""R30 S8h: U-2 final refinement — the xmm2/0.5 flag source and the
'live'-mode caller of the prepare fn 0x1801A5F40.

(a) direct callers of 0x1801A5F40;
(b) caller contexts (the edx selector + xmm2 float arg);
(c) 0x1801A616A block re-check: the exact setLatencySamples value flow;
(d) ctor zero-init of +0x3C (default latency before any 'live' prepare).
"""
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

print('===== (a) callers of prepare fn 0x1801A5F40 =====')
sites = scan_calls(pe, md, 0x1801A5F40)
for (va, flo, fhi) in sites:
    print('  call at 0x%X in fn 0x%X..0x%X' % (va, flo, fhi))
print()

print('===== (b) caller contexts =====')
for (va, flo, fhi) in sites[:6]:
    lo = max(flo, va - 0x70)
    dump(lo, va + 0x10, 'caller near 0x%X' % va)

print('===== (c) the latency block 0x1801A616A..0x1801A61A7 re-read =====')
dump(0x1801A616A, 0x1801A61A7, 'live-mode latency block')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
