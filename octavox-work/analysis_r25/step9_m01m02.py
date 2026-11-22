"""Step 9: disassemble m01/m02 of vtable 0x180409d80 (coeff-array subobject): 0x18013ba50, 0x18013ba40."""
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


for fn, name in [(0x18013ba50, "m01 (setparam?)"), (0x18013ba40, "m02 (getparam?)")]:
    bd = pdata_bounds(fn)
    print(f"\n{'='*70}\n== {name} {fn:#x}: bounds {bd[0]:#x}..{bd[1]:#x} ==\n{'='*70}" if bd else f"!! no bounds {fn:#x}")
    if bd:
        insns = b.disasm(bd[0], bd[1] - bd[0])
        print(b.fmt(insns))
