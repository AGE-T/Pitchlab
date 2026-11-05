"""Round-10 part 2: raw-byte displacement scans (robust against linear-desync).

Scans the whole .text blob for the 4-byte LE patterns of the tracker
correction fields (0x6070 / 0x6074 T-relative, 0xa588 / 0xa58c
engine-relative) and for the engine-embed offset 0x430, then classifies each
raw hit by decoding the containing instruction (start-position search).
Read-only against the original image.
"""
import struct
from pathlib import Path

import capstone

TARGET = Path("/home/z/octavox/original/Ox.vst3")


def load_pe():
    data = TARGET.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
    nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt_off = e_lfanew + 24
    image_base = struct.unpack_from("<Q", data, opt_off + 24)[0]
    sec_off = opt_off + opt_size
    secs = []
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode()
        vsize, va, rsize, ptr = struct.unpack_from("<IIII", data, o + 8)
        secs.append(dict(name=name, va=va, vsize=vsize, rsize=rsize, ptr=ptr))
    return data, image_base, secs


DATA, BASE, SECS = load_pe()
TEXT = next(s for s in SECS if s["name"] == ".text")
TBLOB = DATA[TEXT["ptr"]:TEXT["ptr"] + TEXT["rsize"]]
TVA = BASE + TEXT["va"]
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def classify_hits(value, label):
    pat = struct.pack("<i", value)
    offs = []
    off = 0
    while True:
        off = TBLOB.find(pat, off)
        if off < 0:
            break
        offs.append(off)
        off += 1
    print(f"=== disp32 {label} ({value:#x}): {len(offs)} raw hits ===")
    seen = set()
    for i in offs:
        for s in range(max(0, i - 15), i):
            code = TBLOB[s:s + 24]
            try:
                ins = next(MD.disasm(code, TVA + s))
            except StopIteration:
                continue
            if ins.size < 4 or s + ins.size <= i:
                continue
            for op in ins.operands:
                if (op.type == capstone.x86.X86_OP_MEM
                        and op.mem.base != capstone.x86.X86_REG_RIP
                        and op.mem.disp == value
                        and s <= i < s + ins.size):
                    key = (ins.address, ins.mnemonic, ins.op_str)
                    if key not in seen:
                        seen.add(key)
                    break
    for key in sorted(seen):
        print(f"  {key[0]:#x}  {key[1]} {key[2]}")
    print(f"  -> {len(seen)} decoded instruction hits\n")
    return seen


allhits = {}
for v, lab in ((0x6070, "T+0x6070 running corr"), (0x6074, "T+0x6074 clamped corr target"),
               (0xA588, "engine+0xa588 (=T+0x6070)"), (0xA58C, "engine+0xa58c (=T+0x6074)")):
    allhits[(v, lab)] = classify_hits(v, lab)

print("=== engine-embed offset 0x430 (wrapper -> engine forwarding) ===")
classify_hits(0x430, "wrapper+0x430 engine embed")
