"""R28 S1e: (a) dump the dsp_shared thunk family 0x1800C5730..0x1800C57D0;
(b) scan ALL of .text for indirect calls [reg + 0x10] where rdx/edx is loaded
with a small immediate (7..13) within the preceding 12 instructions — the
direct query path into m02-style getters.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     dump, load_pdata, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800C5730, 0x1800C57D0, 'dsp_shared thunk family 0x1800C5730..0x1800C57D0')

fns = load_pdata()
regs = ('rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp',
        'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15')
hits = []
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i, cur in enumerate(insns):
        if cur.mnemonic != 'call':
            continue
        op = cur.op_str
        if not (op.startswith('qword ptr [') and op.endswith('+ 0x10]')):
            continue
        # look back for rdx/edx immediate loads
        for j in range(i - 1, i - 13, -1):
            if j < 0:
                break
            p = insns[j]
            if p.mnemonic in ('mov', 'lea') and (p.op_str.startswith('rdx, ') or p.op_str.startswith('edx, ')):
                tail = p.op_str.split(', ', 1)[1]
                if tail.startswith('0x') or tail.lstrip('-').isdigit():
                    try:
                        v = int(tail, 0)
                    except ValueError:
                        continue
                    if 0 <= v <= 64:
                        hits.append({'call_va': hex(cur.address), 'fn': hex(lo),
                                     'rdx_set_va': hex(p.address), 'rdx_set': p.mnemonic + ' ' + p.op_str,
                                     'call': cur.mnemonic + ' ' + cur.op_str})
                break
            if p.mnemonic == 'call' and not p.op_str.startswith('qword'):
                break  # a direct call likely redefines rdx
print('=== [reg+0x10] calls with small-int rdx within 12 insns: %d ===' % len(hits))
for h in hits:
    print('  fn 0x%s: at 0x%s  %s   ->  %s' % (h['fn'][2:], h['call_va'][2:], h['rdx_set'], h['call']))

dump_json('r28_s1e_direct_query_calls.json', hits)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
