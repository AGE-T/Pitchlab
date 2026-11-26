"""R30 S9g: all reads/writes of engine+0xA588 / +0xA58C (the ±cents
telemetry twin fields) — who produces and who consumes them."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()

R_RE = re.compile(r'^(movss|movsd|mov) +(xmm\d+|(?:e?\w+)), (?:qword ptr |dword ptr )?\[(\w+) \+ 0x(a588|a58c)\]$')
W_RE = re.compile(r'^(movss|movsd|mov) +(?:qword ptr |dword ptr )?\[(\w+) \+ 0x(a588|a58c)\], (xmm\d+|\w+|0x[0-9a-f]+)$')
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
reads, writes = [], []
for (flo, fhi) in fns:
    for ins in bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo):
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        m = R_RE.match(op)
        if m and m.group(1) in ('movss', 'movsd'):
            reads.append((ins.address, m.group(3), m.group(4)))
            continue
        m = W_RE.match(op)
        if m and m.group(1) in ('movss', 'movsd'):
            writes.append((ins.address, m.group(3)))
print('=== reads of +0xA588/+0xA58C (movss from) ===')
for (va, reg, off) in reads:
    fb = fn_bounds(va)
    print('  0x%X  [%s+0x%s]  fn %s' % (va, reg, off,
          ('0x%X..0x%X' % fb) if fb else '?'))
print('total reads: %d' % len(reads))
print()
print('=== writes to +0xA588/+0xA58C (movss to) ===')
for (va, off, _) in writes:
    fb = fn_bounds(va)
    print('  0x%X  [+0x%s]  fn %s' % (va, off,
          ('0x%X..0x%X' % fb) if fb else '?'))
print('total writes: %d' % len(writes))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
