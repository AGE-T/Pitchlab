"""R30 S9t: THE missing immediate surface — call [reg+{0x78,0x80,0x88,0x90}]
with r8d (or edx) immediate ids 7..13. The env_t poller slots."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_bulk_disassembler, fn_bounds,
                     TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
bmd = get_bulk_disassembler()
fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]

CALLS = {0x78: 15, 0x80: 16, 0x88: 17, 0x90: 18}
CALL_RE = re.compile(r'^call +qword ptr \[(\w+) \+ (0x78|0x80|0x88|0x90)\]$')

def id_def(insns, idx):
    """Backward 40-insn def-slice for r8d (stop at call/ret/jmp)."""
    for j in range(idx - 1, max(idx - 40, -1), -1):
        mn, op = insns[j].mnemonic, insns[j].op_str
        if mn in ('call', 'ret', 'jmp'):
            return None
        dst = op.split(', ')[0] if ', ' in op else op
        if dst in ('r8d', 'r8', 'r8w', 'r8b'):
            src = op.split(', ')[-1] if ', ' in op else ''
            if mn == 'mov' and src.startswith('0x'):
                try:
                    return ('imm', int(src, 16), insns[j].address)
                except ValueError:
                    pass
            if mn == 'xor':
                return ('imm', 0, insns[j].address)
            if mn in ('mov', 'lea', 'movzx', 'movsxd', 'add'):
                return (mn, src, insns[j].address)
            return (mn, op, insns[j].address)
    return None

imm_hits, nonimm = [], 0
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    for k, ins in enumerate(insns):
        m = CALL_RE.match('%s %s' % (ins.mnemonic, ins.op_str))
        if not m:
            continue
        d = id_def(insns, k)
        if d and d[0] == 'imm':
            imm_hits.append((ins.address, int(m.group(2), 16), d[1], d[2]))
        elif d is None or d[0] != 'imm':
            nonimm += 1

print('=== immediate r8d ids at env_t-slot calls ===')
for (va, disp, val, dva) in imm_hits:
    fb = fn_bounds(va)
    print('  0x%X: call [+0x%X] with r8d=%d (def 0x%X)  fn %s'
          % (va, disp, val, dva, ('0x%X..0x%X' % fb) if fb else '?'))
print('total immediate: %d | non-immediate: %d' % (len(imm_hits), nonimm))
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
