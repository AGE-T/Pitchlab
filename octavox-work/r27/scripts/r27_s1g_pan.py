"""R27 S1g+S3: the mix audio loops 0x18013C239..0x18013C85E and 0x18013C85E..0x18013D1FB.

These should read per-input {type, gA, gB} records and accumulate the mix —
resolving both the pan-law consumer side (S1) and the accumulator order (S3).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()
dump(pe, md, 0x18013C239, 0x18013C85E, 'MIX AUDIO FN A 0x18013C239 (1573 B)')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
