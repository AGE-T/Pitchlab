import sys
import re
import capstone
from pathlib import Path

sys.path.insert(0, r'C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis')
from va_scan import va2fo, data, IMAGE_BASE

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
md.skipdata = True

TARGET = int(sys.argv[1], 16)
TEXT_LO, TEXT_HI = 0x180001000, 0x180335000
fo = va2fo(TEXT_LO)
code = data[fo:fo + (TEXT_HI - TEXT_LO)]

calls = []
leas = []
for insn in md.disasm(code, TEXT_LO):
    try:
        ops = insn.operands
    except capstone.CsError:
        continue
    if insn.mnemonic in ('call', 'jmp'):
        for op in ops:
            if op.type == capstone.x86.X86_OP_IMM and op.imm == TARGET:
                calls.append((insn.mnemonic, insn.address))
    for op in ops:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            tgt = insn.address + insn.size + op.mem.disp
            if tgt == TARGET:
                leas.append((insn.mnemonic, insn.address))

print('target 0x%X' % TARGET)
for kind, lst in (('direct call/jmp', calls), ('rip-relative ref', leas)):
    print('%s: %d' % (kind, len(lst)))
    for m, a in lst[:20]:
        print('   %-6s 0x%X' % (m, a))
