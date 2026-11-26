"""R27 S1e: mix-family full context — ctor layout, helper IDs, big audio fns.

1. mix sub ctor FUN_1800D7400 (full) — object layout (0x18-stride records)
2. callers of pan fn 0x18013DCF0 + spread fn 0x18013DC00
3. identify 0x1803528e0 (unary float fn) + 0x18017eb60 (pow?)
4. big mix audio fns 0x18013C239..0x18013C85E, 0x18013C85E..0x18013D1FB
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800D7400, 0x1800D7560, 'MIX SUB CTOR FUN_1800D7400 (full)')

print('=== identify 0x1803528e0 ===')
dump(pe, md, 0x1803528E0, 0x180352960, '0x1803528e0 head')
print('=== identify 0x18017eb60 ===')
dump(pe, md, 0x18017EB60, 0x18017EB90, '0x18017eb60 head')

print('=== callers of PAN FN 0x18013DCF0 ===')
for site, lo, hi in scan_calls(pe, md, 0x18013DCF0):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
print('=== callers of SPREAD FN 0x18013DC00 ===')
for site, lo, hi in scan_calls(pe, md, 0x18013DC00):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
print('=== callers of 0x18013DB50 (record copy fn) ===')
for site, lo, hi in scan_calls(pe, md, 0x18013DB50):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
