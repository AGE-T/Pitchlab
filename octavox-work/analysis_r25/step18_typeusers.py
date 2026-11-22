"""Step 18: disassemble all functions referencing the 'stereomixer' type descriptor."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()


def pdata_bounds(va):
    pd = [s for s in b.secs if s[0] == ".pdata"][0]
    n = pd[4] // 12
    for i in range(n):
        off = pd[3] + 12 * i
        s_rva, e_rva = struct.unpack_from("<II", b.data, off)
        if s_rva and IB + s_rva <= va < IB + e_rva:
            return IB + s_rva, IB + e_rva
    return None


refs = [0x1800d7469, 0x1800d78e1, 0x1800d83b8, 0x1800d8909, 0x1800d8a8e,
        0x1800d8be5, 0x1800d8d6a, 0x1800d9718, 0x1800d9943, 0x18032cae0]
seen = set()
for r in refs:
    bd = pdata_bounds(r)
    if not bd or bd[0] in seen:
        continue
    seen.add(bd[0])
    print(f"\n{'='*74}\n== fn {bd[0]:#x}..{bd[1]:#x} (size {bd[1]-bd[0]}, contains ref {r:#x}) ==\n{'='*74}")
    insns = b.disasm(bd[0], bd[1] - bd[0])
    print(b.fmt(insns))
