"""Step 7: disasm helper functions: 0x1800c57c0 (port-group register), 0x18013d260, 0x18013d4a0."""
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


for fn, name in [(0x1800c57c0, "0x1800c57c0 port-group ctor?"),
                 (0x18013d260, "0x18013d260 subobj ctor (+0xa0/+0xc8)"),
                 (0x18013d4a0, "0x18013d4a0 port-list ctor (+0x58)")]:
    bd = pdata_bounds(fn)
    if not bd:
        print(f"!! no pdata bounds for {name}")
        continue
    print(f"\n{'='*70}\n== {name}: {bd[0]:#x}..{bd[1]:#x} ==\n{'='*70}")
    insns = b.disasm(bd[0], bd[1] - bd[0])
    print(b.fmt(insns))
