"""R28 S1f: find slot-3 ([reg+0x18]) and slot-4 ([reg+0x20]) indirect calls
where the third argument (r8d/edx) carries a small immediate (the m01/m02 id),
i.e. the actual query/publish path into module getters.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range,
                     load_pdata, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

fns = load_pdata()
hits = []
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i, cur in enumerate(insns):
        if cur.mnemonic != 'call':
            continue
        op = cur.op_str
        if not (op.startswith('qword ptr [') and (op.endswith('+ 0x18]') or op.endswith('+ 0x20]'))):
            continue
        slot = op[-5:-1]
        # look back for r8d/edx/r8/edx immediate load
        for j in range(i - 1, i - 13, -1):
            if j < 0:
                break
            p = insns[j]
            o = p.op_str
            if p.mnemonic in ('mov', 'lea') and (o.startswith('r8d, ') or o.startswith('r8, ') or o.startswith('edx, ') or o.startswith('rdx, ')):
                tail = o.split(', ', 1)[1]
                if tail.startswith('0x') or tail.lstrip('-').isdigit():
                    try:
                        v = int(tail, 0)
                    except ValueError:
                        continue
                    if 0 <= v <= 64:
                        hits.append({'slot': slot, 'call_va': hex(cur.address), 'fn': hex(lo),
                                     'arg_set': p.mnemonic + ' ' + o, 'call': cur.mnemonic + ' ' + op,
                                     'value': v})
                break
            if p.mnemonic == 'call' and not p.op_str.startswith('qword'):
                break

print('=== slot-3/slot-4 calls with small-int 3rd/2nd arg: %d ===' % len(hits))
for h in hits:
    print('  fn 0x%s slot %s: at 0x%s  %-22s -> %s' %
          (h['fn'][2:], h['slot'], h['call_va'][2:], h['arg_set'], h['call']))
dump_json('r28_s1f_slot34_calls.json', hits)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
