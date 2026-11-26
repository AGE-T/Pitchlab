"""R27 S1f: the mix-family master update fn FUN_18013AF20..0x18013B455 (full)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()
dump(pe, md, 0x18013AF20, 0x18013B455, 'MIX MASTER UPDATE FUN_18013AF20 (full, 1333 B)')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
