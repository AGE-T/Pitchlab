"""R30 S8g: U-2 numeric closure — vtable slot 75 ([vt+0x258]) = the latency
computation called by the prepareToPlay-like fn 0x1801A5F40 right before
setLatencySamples.

(a) the full prepare-context fn 0x1801A5F40..0x1801A6464 (head + tail around
    the latency block);
(b) primary vtable 0x1803F05D0 slot 75 (+0x258) target + full disassembly;
(c) if slot 75's body reads fields/constants, decode them (the NUMBER).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     fn_bounds, vtable_slots, looks_like_codeptr)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) prepare-fn 0x1801A5F40..0x1801A6464: head =====')
dump(0x1801A5F40, 0x1801A6060, 'head (loop entry)')

print('===== (b) primary vtable slot 75 (+0x258) =====')
slots = vtable_slots(pe, 0x1803F05D0, 80)
s75 = slots[75]
print('  slot75 = 0x%X' % (s75 or 0))
if s75 and looks_like_codeptr(pe, s75):
    bb = fn_bounds(s75)
    lo, hi = bb if bb else (s75, s75 + 0x400)
    print('  bounds: 0x%X..0x%X (%d B)' % (lo, hi, hi - lo))
    dump(lo, hi, 'slot75 body')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
