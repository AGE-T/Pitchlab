"""R27 S1l: examine the 8 call sites of FUN_1800EBAF0 in the miso tree update FUN_1801061A0."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump)

pe = PE(); check_hash(pe)
md = get_disassembler()
# context before each call site: what is rcx?
for site in (0x180106B4F, 0x180106BA3, 0x180106BF7):
    lo = site - 0x60
    dump(pe, md, lo, site + 8, 'CONTEXT before call site 0x%X' % site)
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
