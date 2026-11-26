"""R27 S1c: pan law — disassemble the full mix-family region containing the pan chunk.

0x18013DD34 is a pdata chunk, not a function entry (0 callers). Disassemble
0x18013DB50..0x18013DF30 linearly + find the real function start (scan back for
prologue / int3 padding), and dump the acosf call context + coefficient stores.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

# int3 padding between chunks tells us real function starts
print('=== padding scan 0x18013DAD0..0x18013DF30 ===')
fo = pe.va2fo(0x18013DAD0)
data = pe.data[fo:fo + (0x18013DF30 - 0x18013DAD0)]
for i, b in enumerate(data):
    if b == 0xCC:
        va = 0x18013DAD0 + i
        nxt = data[i:i+8]
        print('CC @ 0x%X  next bytes: %s' % (va, nxt.hex()))

print()
dump(pe, md, 0x18013DB50, 0x18013DD34, 'PRE-CHUNK 0x18013DB50..0x18013DD34')
dump(pe, md, 0x18013DEAB, 0x18013DF30, 'POST-CHUNK 0x18013DEAB..0x18013DF30 (continuation)')
