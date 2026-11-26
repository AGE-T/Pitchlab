"""R30 S9o: the m02 call-shape hunt — slot-2 calls with r8 = lea [stack]
(out-pointer) and non-immediate rdx; slot-4 calls with r9 = lea [stack] and
non-immediate r8d. THE poller signature."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]

CALL2 = re.compile(r'^call +qword ptr \[(\w+) \+ 0x10\]$')
CALL4 = re.compile(r'^call +qword ptr \[(\w+) \+ 0x20\]$')
LEA_R8 = re.compile(r'^lea +r8, \[(rsp|rbp)[^]]*\]$')
LEA_R9 = re.compile(r'^lea +r9, \[(rsp|rbp)[^]]*\]$')

c2, c4 = [], []
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    for k, ins in enumerate(insns):
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        m = CALL2.match(op)
        if m:
            win = insns[max(0, k - 6):k]
            wops = ['%s %s' % (x.mnemonic, x.op_str) for x in win]
            if any(LEA_R8.match(w) for w in wops):
                # rdx def in window (non-imm check: no mov edx, imm)
                imm = any(re.match(r'^mov +edx, 0x', w) for w in wops)
                c2.append((ins.address, m.group(1), imm, wops[-4:]))
        m = CALL4.match(op)
        if m:
            win = insns[max(0, k - 6):k]
            wops = ['%s %s' % (x.mnemonic, x.op_str) for x in win]
            if any(LEA_R9.match(w) for w in wops):
                imm = any(re.match(r'^mov +r8d, 0x', w) for w in wops)
                c4.append((ins.address, m.group(1), imm, wops[-4:]))

print('=== slot-2 calls with lea r8,[stack] in window: %d ===' % len(c2))
for (va, reg, imm, wops) in c2:
    fb = fn_bounds(va)
    print('  0x%X recv=%s idimm=%s fn %s' % (va, reg, imm,
          ('0x%X..0x%X' % fb) if fb else '?'))
    for w in wops:
        print('      %s' % w)
print()
print('=== slot-4 calls with lea r9,[stack] in window: %d ===' % len(c4))
for (va, reg, imm, wops) in c4:
    fb = fn_bounds(va)
    print('  0x%X recv=%s idimm=%s fn %s' % (va, reg, imm,
          ('0x%X..0x%X' % fb) if fb else '?'))
    for w in wops:
        print('      %s' % w)
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
