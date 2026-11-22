"""Step 17: enumerate module TYPE descriptors {name, version, count, paramlist} across .rdata."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
rd = [s for s in b.secs if s[0] == ".rdata"][0]
start, size = rd[3], rd[4]

def cstr_at(va):
    if va is None or not (0x180000000 <= va < 0x180600000):
        return None
    t = b.cstr(va, 40)
    if t and all(32 <= ord(c) < 127 for c in t):
        return t
    return None

hits = []
for off in range(start, start + size - 0x20, 8):
    va = b.off2va(off)
    name_p = b.u64(va)
    ver_p = b.u64(va + 8)
    cnt = b.u64(va + 0x10)
    list_p = b.u64(va + 0x18)
    name = cstr_at(name_p) if name_p else None
    ver = cstr_at(ver_p) if ver_p else None
    if name and ver and 1 <= (cnt or 0) <= 64 and list_p and 0x180300000 <= list_p < 0x180400000:
        # version should look like digits/dots
        if all(c.isdigit() or c == '.' for c in ver) and 1 <= len(ver) <= 4:
            hits.append((va, name, ver, cnt, list_p))

print(f"found {len(hits)} type descriptors")
for va, name, ver, cnt, list_p in hits:
    print(f"  {va:#x}: {name!r} v{ver} nparams={cnt} list={list_p:#x}")
