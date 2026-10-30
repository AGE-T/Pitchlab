"""List branch/call sources whose immediate target falls in a VA range (linear sweep)."""
import sys

import capstone

sys.path.insert(0, r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
from va_scan import va2fo, data  # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
md.skipdata = True

TEXT_LO, TEXT_HI = 0x180001000, 0x180335000
lo = int(sys.argv[1], 16)
hi = int(sys.argv[2], 16)

code = data[va2fo(TEXT_LO):va2fo(TEXT_LO) + (TEXT_HI - TEXT_LO)]
hits = []
n = 0
for insn in md.disasm(code, TEXT_LO):
    n += 1
    try:
        ops = insn.operands
    except capstone.CsError:
        continue
    if not ops:
        continue
    op = ops[0]
    if op.type == capstone.x86.X86_OP_IMM and lo <= op.imm < hi:
        if insn.mnemonic != "int3":
            hits.append((insn.address, insn.mnemonic, op.imm))
print("scanned %d instructions; %d branches into [%X,%X)" % (n, len(hits), lo, hi))
for a, m, t in hits:
    print("   %016X  %-8s -> %X" % (a, m, t))
