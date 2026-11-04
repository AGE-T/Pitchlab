"""Enumerate MSVC COLs in .rdata, resolve type-descriptor names, print interesting ones."""
import json
import struct
from pathlib import Path

A = Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE = json.loads((A / "pe_info.json").read_text(encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()


def as_int(v):
    return v if isinstance(v, int) else int(v, 16)


SEC = [(as_int(s["va"]), as_int(s["rsize"]), as_int(s["ptr"]), s["name"]) for s in PE["sections"]]
RD = [s for s in SEC if s[3] == ".rdata"][0]


def va2fo(va):
    rva = va - BASE
    for sva, rsize, ptr, name in SEC:
        if sva <= rva < sva + rsize:
            return ptr + (rva - sva)
    return None


def fo2va(fo):
    for sva, rsize, ptr, name in SEC:
        if ptr <= fo < ptr + rsize:
            return BASE + sva + (fo - ptr)
    return None


def cstr(va, n=200):
    fo = va2fo(va)
    if fo is None:
        return None
    e = IMG.find(b"\0", fo, fo + n)
    return IMG[fo:e].decode("latin-1", "replace")


rva0, size0, ptr0, _ = RD
cols = {}
for off in range(ptr0, ptr0 + size0 - 0x18, 4):
    sig, o, cd, ptd, pcd = struct.unpack("<IIIII", IMG[off:off + 0x14])
    if sig != 1 or o > 0x2000 or cd > 0x2000:
        continue
    if not (0 < ptd < 0x600000 and 0 < pcd < 0x600000):
        continue
    td = va2fo(BASE + ptd)
    if td is None:
        continue
    name = None
    for off2 in (0x10, 8, 0):
        cand = cstr(BASE + ptd + off2, 200)
        if cand and cand.startswith(".?A"):
            name = cand
            break
    if name is None:
        continue
    cols[fo2va(off)] = name

print("total COLs:", len(cols))
for va, name in sorted(cols.items()):
    low = name.lower()
    if any(k in low for k in ("vox", "tide", "juce", "vst", "audio")):
        print(hex(va), name)
