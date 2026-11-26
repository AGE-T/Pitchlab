"""R27 S1h: hunt the mix audio loop — dump candidates 0x18013B559, 0x18013B7B0, 0x18013BE50."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()
dump(pe, md, 0x18013BE50, 0x18013C239, 'MIX AUDIO CANDIDATE 0x18013BE50 (1001 B)')
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
