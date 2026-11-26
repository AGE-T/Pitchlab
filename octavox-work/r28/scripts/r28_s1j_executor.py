"""R28 S1j: map the module lifecycle protocol from the graph executor sites
(0x180104CB4 / 0x18011E88E per R27 F2.1) — which vtable slots does the
executor call? Plus: direct-caller scan for m01 (FUN_1800E10B0) and slot-1
([reg+8]) calls with small rdx immediates (m01 set-parameter path).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata, scan_calls, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

# executor context windows
for site, name in ((0x180104CB4, 'executor site 1 (0x180104CB4)'),
                   (0x18011E88E, 'executor site 2 (0x18011E88E)')):
    lo = None
    for a, b in load_pdata():
        if a <= site < b:
            lo, hi = a, b
            break
    print('### %s in fn 0x%X..0x%X' % (name, lo, hi))
    w = list(disasm_range(pe, md, max(lo, site - 120), min(hi, site + 60)))
    for i in w:
        print(annotate(pe, i))
    print()

# direct callers of m01
print('### direct callers of m01 FUN_1800E10B0:')
for a, lo, hi in scan_calls(pe, md, 0x1800E10B0):
    print('    call at 0x%X in fn 0x%X..0x%X' % (a, lo, hi))

# slot-1 calls with small-int rdx
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
        if not (op.startswith('qword ptr [') and op.endswith('+ 8]')):
            continue
        for j in range(i - 1, i - 9, -1):
            if j < 0:
                break
            p = insns[j]
            o = p.op_str
            if p.mnemonic in ('mov', 'lea') and (o.startswith('rdx, ') or o.startswith('edx, ')):
                tail = o.split(', ', 1)[1]
                if tail.startswith('0x') or tail.lstrip('-').isdigit():
                    try:
                        v = int(tail, 0)
                    except ValueError:
                        continue
                    if 0 <= v <= 32:
                        hits.append({'call_va': hex(cur.address), 'fn': hex(lo),
                                     'set': p.mnemonic + ' ' + o, 'call': cur.mnemonic + ' ' + op})
                break
            if p.mnemonic == 'call' and not p.op_str.startswith('qword'):
                break
print('### slot-1 [reg+8] calls with small-int rdx: %d' % len(hits))
for h in hits:
    print('    fn 0x%s at 0x%s: %s -> %s' % (h['fn'][2:], h['call_va'][2:], h['set'], h['call']))
dump_json('r28_s1j_slot1_calls.json', hits)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
