"""All combos of 'mov r64,[rcx+d]; mov eax,[r64+d]; ret' in .text, with vtable refs."""
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
TEXT = [s for s in SEC if s[3] == ".text"][0]
RD = [s for s in SEC if s[3] == ".rdata"][0]


def fo2va(fo):
    for sva, rsize, ptr, name in SEC:
        if ptr <= fo < ptr + rsize:
            return BASE + sva + (fo - ptr)
    return None


t0 = TEXT[2]
t1 = TEXT[2] + TEXT[1]
reg64 = {0: "rax", 1: "rcx", 2: "rdx", 3: "rbx", 6: "rsi", 7: "rdi"}


def disp(b, i, mod):
    if mod == 1:
        return struct.unpack("<b", b[i:i + 1])[0], i + 1
    if mod == 2:
        return struct.unpack("<i", b[i:i + 4])[0], i + 4
    return 0, i


hits = []
for off in range(t0, t1 - 16):
    b = IMG[off:off + 16]
    if b[0] != 0x48 or b[1] != 0x8B:
        continue
    m1 = b[2]
    if (m1 & 0x07) != 0x01:          # [rcx + disp]
        continue
    mod1 = (m1 >> 6) & 3
    if mod1 not in (1, 2):
        continue
    r1 = (m1 >> 3) & 7
    if r1 not in reg64:
        continue
    d1, i = disp(b, 3, mod1)
    if b[i] != 0x8B:
        continue
    m2 = b[i + 1]
    if (m2 & 7) != r1 or ((m2 >> 3) & 7) != 0:
        continue
    mod2 = (m2 >> 6) & 3
    if mod2 not in (1, 2):
        continue
    d2, j = disp(b, i + 2, mod2)
    if b[j] == 0xC3:
        hits.append((fo2va(off), reg64[r1], d1, d2, j + 1))

print("hits:", len(hits))
refs = {}
rva0, size0, ptr0, _ = RD
for off in range(ptr0, ptr0 + size0 - 8, 8):
    v = struct.unpack("<Q", IMG[off:off + 8])[0]
    refs.setdefault(v, []).append(fo2va(off))

for va, reg, d1, d2, ln in hits:
    tag = "VT" if va in refs else "  "
    print("  %s %-10s %s [rcx+0x%x] -> [%s+0x%x] len=%d refs=%s" % (
        tag, hex(va), reg, d1 & 0xFFFFFFFF, reg, d2 & 0xFFFFFFFF, ln,
        [hex(x) for x in refs.get(va, [])[:3]]))
