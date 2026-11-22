"""Step 3: .pdata function bounds + xref finder + disassemble ctor 0x1800D7400 / 0x1800D8230."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
text = [s for s in b.secs if s[0] == ".text"][0]
TEXT_VA = IB + text[1]
TEXT_END = TEXT_VA + text[2]
TBL = {}


def build_pdata():
    pd = [s for s in b.secs if s[0] == ".pdata"][0]
    n = pd[4] // 12
    for i in range(n):
        off = pd[3] + 12 * i
        s_rva, e_rva = struct.unpack_from("<II", b.data, off)
        if s_rva:
            TBL[IB + s_rva] = IB + e_rva


build_pdata()


def bounds(va):
    for s, e in TBL.items():
        if s <= va < e:
            return s, e
    return None


def find_xrefs(target):
    """Scan .text for direct E8/E9 rel32 to target."""
    data = b.data
    res = []
    # iterate all call/jmp positions: naive byte scan then validate
    raw = text[3]
    for off in range(raw, raw + text[4] - 5):
        op = data[off]
        if op in (0xE8, 0xE9):
            rel = struct.unpack_from("<i", data, off + 1)[0]
            va_caller = b.off2va(off)
            tgt = va_caller + 5 + rel
            if tgt == target:
                res.append((va_caller, "call" if op == 0xE8 else "jmp"))
    return res


for name, fn in [("FUN_180103a10", 0x180103a10), ("0x1800d7f10", 0x1800d7f10),
                 ("ctor 0x180137720", 0x180137720), ("subctor 0x1800d8230", 0x1800d8230),
                 ("subctor 0x1800d7400", 0x1800d7400)]:
    bd = bounds(fn)
    xr = find_xrefs(fn)
    print(f"{name}: bounds {bounds_str if (bounds_str := (f'{bd[0]:#x}..{bd[1]:#x}' if bd else 'NONE')) else ''} "
          f"size {bd[1]-bd[0] if bd else 0}, xrefs={[(hex(a), t) for a, t in xr]}")
