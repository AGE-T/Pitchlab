"""R27 S1m: find the per-voice output module ctor (tree+0x16C28) in the miso ctor
FUN_180109D40, and identify the class vtable -> audio process method."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

# search the whole .text for 'lea rcx,[rbx+0x16c28]' style constructions: pattern
# lea rcx, [rbx + 0x16C28] = 48 8D 8B 28 16 01 00  (or rdi/r12 base variants)
fo = pe.va2fo(0x180001000)
data = pe.data[fo:fo + (0x18037D000 - 0x180001000)]
import re
print('=== lea reg, [base+0x16C28] scan ===')
for m in re.finditer(b'\x48\x8d[\x8b\x8f\x93\x97\x9b\x9f\x83\x87\x8b\x8f].{4}\x01\x00', data[:0x100000], re.S):
    va = pe.fo2va(fo + m.start())
    if m.group(0)[3] == 0x28 and m.group(0)[4] == 0x16:
        print('lea at 0x%X: %s' % (va, m.group(0).hex()))

# Simpler: scan for the byte sequence 28 16 01 00 as disp32 in lea instructions
print('=== byte scan 28 16 01 00 ===')
cnt = 0
for m in re.finditer(b'\x28\x16\x01\x00', data):
    va = pe.fo2va(fo + m.start())
    print('hit 0x%X' % va)
    cnt += 1
    if cnt > 20: break
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
