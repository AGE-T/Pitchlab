"""R27 S1k: decode master jump table (cases 4/8/11) + FUN_1800EBAF0 tail + owner hunt."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)

# --- decode jump table: rdx = 0x180000000 ---
base = 0x180000000
print('=== master jump table decode (case -> target) ===')
seen = {}
for case in range(1, 0x23):
    idx = pe.read(base + 0x13B51C + (case - 1), 1)[0]
    off = struct.unpack('<I', pe.read(base + 0x13B4F8 + idx * 4, 4))[0]
    tgt = base + off
    if case in (1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,30,34):
        print('case %2d -> 0x%X' % (case, tgt))
    seen.setdefault(tgt, []).append(case)
print('--- case groups ---')
for tgt in sorted(seen):
    print('0x%X : cases %s' % (tgt, seen[tgt]))

md = get_disassembler()
dump(pe, md, 0x1800EC008, 0x1800EC0A9, 'FUN_1800EBAF0 TAIL')

print('=== callers of FUN_1800EBAF0 ===')
for site, lo, hi in scan_calls(pe, md, 0x1800EBAF0):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))

# who constructs a mix sub at +0x7C? scan for lea rcx,[rbx+0x7c] or +0x7c constructions near 0x1800D7400 calls
print('=== callers of MIX SUB CTOR 0x1800D7400 ===')
for site, lo, hi in scan_calls(pe, md, 0x1800D7400):
    print('call site 0x%X inside fn 0x%X..0x%X (size %d)' % (site, lo, hi, hi - lo))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
