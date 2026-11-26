"""R30 S9e: all `lea reg,[base+0x430]` sites feeding a slot-2 call
([rax+0x10]) — the m02 poller candidates."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()

LEA_RE = re.compile(r'^lea +(\w+), \[(\w+) \+ 0x430\]$')
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
cands = []
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    for k, ins in enumerate(insns):
        m = LEA_RE.match('%s %s' % (ins.mnemonic, ins.op_str))
        if not m:
            continue
        R, B = m.group(1), m.group(2)
        # look ahead for a slot-2 virtual call using R (possibly via rcx)
        for j in range(k + 1, min(k + 16, len(insns))):
            mn, op = insns[j].mnemonic, insns[j].op_str
            if mn == 'call':
                if op == 'qword ptr [rax + 0x10]' or op == 'qword ptr [rcx + 0x10]':
                    cands.append((ins.address, R, B, insns[j].address, j - k,
                                  'direct'))
                break
            # register moved? track mov rcx, R / mov rax-style
            if mn == 'mov' and op.startswith('rcx, ' + R):
                continue
            if mn == 'mov' and op.startswith('rax, qword ptr [' + R):
                continue
            if mn == 'mov' and op == 'rcx, ' + R:
                continue
        # also: two-step: mov rcx, R then mov rax,[rcx]; call [rax+0x10]
        for j in range(k + 1, min(k + 20, len(insns))):
            mn, op = insns[j].mnemonic, insns[j].op_str
            if mn == 'call' and op in ('qword ptr [rax + 0x10]',
                                       'qword ptr [rcx + 0x10]'):
                # verify rcx chain: rcx==R or rcx loaded from R within window
                ok = False
                for j2 in range(k, j):
                    o2 = '%s %s' % (insns[j2].mnemonic, insns[j2].op_str)
                    if o2 == 'mov rcx, ' + R or o2 == 'mov rcx, qword ptr [' + R + ']':
                        ok = True
                    if insns[j2].mnemonic == 'call' and j2 != j:
                        break
                if ok or R == 'rcx':
                    cand = (ins.address, R, B, insns[j].address, j - k, 'chain')
                    if cand not in cands:
                        cands.append(cand)
                break
            if mn == 'call':
                break
print('lea [base+0x430] -> slot-2 call candidates: %d' % len(cands))
for (lva, R, B, cva, dist, kind) in cands:
    fb = fn_bounds(lva)
    print('  lea 0x%X (%s = [%s+0x430]) -> call 0x%X (+%d insns, %s) in fn %s'
          % (lva, R, B, cva, dist, kind,
             ('0x%X..0x%X' % fb) if fb else '?'))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
