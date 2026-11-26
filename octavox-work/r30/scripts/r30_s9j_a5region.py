"""R30 S9j: the whole 0xA400..0xA600 engine-field region — every literal
access, to find the alias path that writes +0xA588/+0xA58C (if any)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
RX = re.compile(r'0xa([4-5][0-9a-f]{2})\b')
hits = {}
for (flo, fhi) in fns:
    for ins in bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo):
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        for m in RX.finditer(op):
            v = int('a' + m.group(1), 16)
            if 0xa400 <= v <= 0xa600:
                hits.setdefault(v, []).append((ins.address, op, ins.mnemonic))
                break
for off in sorted(hits):
    lst = hits[off]
    mnem = {}
    for (va, op, mn) in lst:
        mnem.setdefault(mn, 0)
        mnem[mn] += 1
    print('  +0x%X: %d hits  %s' % (off, len(lst), mnem))
    if 0xa570 <= off <= 0xa5a0:
        for (va, op, mn) in lst:
            fb = fn_bounds(va)
            print('      0x%X  %-58s %s' % (va, op,
                  ('fn 0x%X..0x%X' % fb) if fb else '?'))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
