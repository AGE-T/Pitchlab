"""R28 S1h: dump the 0x1801D0xxx wrapper cluster + the 0x18002Bxxx cluster —
candidate module parameter set/get wrappers using the container protocol.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import PE, check_hash, get_disassembler, dump

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1801D0520, 0x1801D0580, 'fn 0x1801D0520 (slot-3 r8d=8)')
dump(pe, md, 0x1801D0580, 0x1801D0670, 'fn 0x1801D0580 (slot-3 r8d=8) + gap')
dump(pe, md, 0x1801D0670, 0x1801D0710, 'fn 0x1801D0670 (slot-3 r8d=4)')
dump(pe, md, 0x1801D0710, 0x1801D07B0, 'fn 0x1801D0710 (slot-3 r8d=2) / 0x1801D0750')
dump(pe, md, 0x1801D07B0, 0x1801D0810, 'fn 0x1801D07B0 (slot-3 r8d=1)')
dump(pe, md, 0x18002BBC4, 0x18002BC20, 'fn 0x18002BBC4 (slot-3 r8d=3)')
dump(pe, md, 0x18002BC20, 0x18002BC80, 'fn 0x18002BC20 (slot-3 r8d=1)')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
