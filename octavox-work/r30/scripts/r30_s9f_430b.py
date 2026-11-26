"""R30 S9f: all lea [base+0x430] sites + slot-2 calls per function (intersect)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()

LEA_RE = re.compile(r'^lea +(\w+), \[(\w+) \+ 0x430\]$')
CALL_RE = re.compile(r'^call +qword ptr \[rax \+ 0x10\]$|'
                     r'^call +qword ptr \[rcx \+ 0x10\]$|'
                     r'^call +qword ptr \[(\w+) \+ 0x10\]$')
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
lea_fns = {}
call_fns = {}
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    for ins in insns:
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        m = LEA_RE.match(op)
        if m:
            lea_fns.setdefault((flo, fhi), []).append((ins.address, m.group(1), m.group(2)))
        if CALL_RE.match(op):
            call_fns.setdefault((flo, fhi), []).append(ins.address)

print('functions with lea [x+0x430]: %d' % len(lea_fns))
for (fb, lst) in sorted(lea_fns.items()):
    ncall = len(call_fns.get(fb, []))
    print('  fn 0x%X..0x%X: %d lea(s) %s | slot2-calls: %d %s'
          % (fb[0], fb[1], len(lst),
             ['0x%X %s=[%s+0x430]' % t for t in lst[:4]],
             ncall, ['0x%X' % v for v in call_fns.get(fb, [])[:6]]))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
