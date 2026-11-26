"""R30 S9k: the engine+0xA508 interior alias — its users and the writes
through offsets 0x80/0x84 (= engine+0xA588/+0xA58C)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
LEA_RE = re.compile(r'^lea +(\w+), \[(\w+) \+ 0xa508\]$')
MEM_RE = re.compile(r'^movss? +(?:dword ptr )?\[(\w+) \+ (0x[0-9a-f]+)\],')
READ_RE = re.compile(r'^movss +xmm\d+, dword ptr \[(\w+) \+ (0x[0-9a-f]+)\]$')

print('=== lea [reg+0xA508] sites ===')
alias_sites = []
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    for k, ins in enumerate(insns):
        m = LEA_RE.match('%s %s' % (ins.mnemonic, ins.op_str))
        if not m:
            continue
        A = m.group(1)
        alias_sites.append((ins.address, A, flo, fhi))
        print('  0x%X  %s = [%s+0xA508]  (fn 0x%X..0x%X)' % (ins.address, A, m.group(2), flo, fhi))
print('total: %d' % len(alias_sites))
print()

print('=== writes/reads through alias+0x80/0x84 in those functions ===')
fnset = {}
for (va, A, flo, fhi) in alias_sites:
    fnset.setdefault((flo, fhi), []).append((va, A))
for (fb, alist) in sorted(fnset.items()):
    insns = list(bmd.disasm(pe.data[pe.va2fo(fb[0]):pe.va2fo(fb[0]) + (fb[1] - fb[0])], fb[0]))
    for ins in insns:
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        m = MEM_RE.match(op)
        r = READ_RE.match(op)
        for (lva, A) in alist:
            if (m and m.group(1) == A and m.group(2) in ('0x80', '0x84')) or \
               (r and r.group(1) == A and r.group(2) in ('0x80', '0x84')):
                kind = 'WRITE' if m else 'READ '
                print('  %s 0x%X  %s  (fn 0x%X..0x%X, alias %s from 0x%X)'
                      % (kind, ins.address, op, fb[0], fb[1], A, lva))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
