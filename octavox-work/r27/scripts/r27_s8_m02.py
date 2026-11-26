"""R27 S8: m02 (engine vtable slot 2, +0x10) polling consumer hunt.

Scan .text for indirect calls [reg+0x10] within a window after a load of
[reg2+0x430] (the engine pointer inside the dtonicii9 wrapper), and also for
the generic port-dispatch pattern that reaches vtable slot 2.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import PE, check_hash, get_disassembler, disasm_range, annotate, load_pdata

pe = PE(); check_hash(pe)
md = get_disassembler()

fns = load_pdata()
hits = []
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i in range(len(insns)):
        cur = insns[i]
        # candidate: call/jmp [reg + 0x10] or [reg + 0x10] with qword
        if cur.mnemonic in ('call', 'jmp') and 'qword ptr [r' in cur.op_str and '+ 0x10]' in cur.op_str:
            # look back up to 12 instructions for a +0x430 load
            window = insns[max(0, i - 12):i]
            for w in window:
                if '0x430]' in w.op_str and w.mnemonic == 'mov':
                    hits.append((cur.address, lo, hi, w.address))
                    break

print('=== indirect [x+0x10] calls after engine(+0x430) loads ===')
for addr, lo, hi, w in hits[:30]:
    print('call 0x%X (engine load at 0x%X) in fn 0x%X..0x%X' % (addr, w, lo, hi))

# ALSO: the generic port dispatch — find the port router that calls a module's
# vtable+0x10 with (module, value). Pattern: functions reading a module-id table.
# The mix publish used [r10+0xA0]; m01/m02 might be reached via [vtable+8]/[+0x10]
# in a shared dispatch. Scan for 'call qword ptr [rax + 0x10]' preceded by
# 'mov rax, qword ptr [rcx]' (vtable load) within 4 insns, in the 0x1800C0000-0x1800F0000 core:
print()
print('=== vtable+0x10 dispatches in the module-core region ===')
cnt = 0
for lo, hi in fns:
    if not (0x1800C0000 <= lo < 0x1800F0000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i in range(1, len(insns)):
        cur = insns[i]
        if cur.mnemonic == 'call' and cur.op_str in ('qword ptr [rax + 0x10]', 'qword ptr [rcx + 0x10]',
                                                      'qword ptr [rdx + 0x10]', 'qword ptr [rsi + 0x10]',
                                                      'qword ptr [rdi + 0x10]'):
            prev = insns[i-1]
            if prev.mnemonic == 'mov' and prev.op_str.startswith('r') and ', qword ptr [r' in prev.op_str:
                print('0x%X in fn 0x%X: %s %s | prev: %s %s' % (cur.address, lo, cur.mnemonic, cur.op_str, prev.mnemonic, prev.op_str))
                cnt += 1
                if cnt > 20:
                    break
    if cnt > 20:
        break
pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
