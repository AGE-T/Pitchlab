"""R28 S1g: full context dumps of the most informative slot-3/slot-4 call sites.

- fn 0x1801E2380 (slot-3 calls with r8d=6 and r8d=0xC=12)
- fn 0x18022D036 (slot-4 call with r8d=8)
- fn 0x1801BAA40 (slot-4 calls r8d=5/6/4/4/2)
- fn 0x1801BA200 (slot-4 calls r8d=5/3)
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1801E2380, 0x1801E2450, 'fn 0x1801E2380 (slot-3 r8d=6 @23d3, r8d=0xC @240c)')
dump(pe, md, 0x18022D036, 0x18022D0A0, 'fn 0x18022D036 (slot-4 r8d=8 @2d04a)')
dump(pe, md, 0x1801BAA40, 0x1801BAC00, 'fn 0x1801BAA40 (slot-4 r8d=5/6/4/4/2)')
dump(pe, md, 0x1801BA200, 0x1801BA340, 'fn 0x1801BA200 (slot-4 r8d=5/3)')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
