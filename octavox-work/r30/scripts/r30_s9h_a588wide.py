"""R30 S9h: ANY operand mention of 0xA588/0xA58C + wide stores covering them."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
hits = []
for (flo, fhi) in fns:
    for ins in bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo):
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        if '0xa588' in op or '0xa58c' in op:
            hits.append((ins.address, op))
        # wide stores covering 0xA588/A58C: [r+0xa580] 16B, [r+0xa58c] etc.
        m = re.match(r'^mov(?:u|a)ps +(?:xmmword ptr )?\[(\w+) \+ 0x(a5[0-9a-f]{2})\], xmm\d$', op)
        if m and 0xa578 <= int(m.group(2), 16) <= 0xa588:
            hits.append((ins.address, op + '   << covers A588/A58C'))
print('=== all 0xA588/0xA58C mentions + covering wide stores ===')
for (va, op) in hits:
    fb = fn_bounds(va)
    print('  0x%X  %-60s fn %s' % (va, op, ('0x%X..0x%X' % fb) if fb else '?'))
print('total: %d' % len(hits))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
