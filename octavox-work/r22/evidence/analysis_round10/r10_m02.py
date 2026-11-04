"""Round-10 m02 params 12/13 data-path verification (tracker outputs 12 and 13).

Read-only. Fresh capstone decode + .rdata reads + .text scans:
  A. m02 jump table 0x1800E1890 -> paramId->handler map (7..13)
  B. engine vtable 0x180409F60 entries; all .rdata slots holding FUN_1800E1520
  C. the constants used by handlers 0x1800E1751 / 0x1800E17E1
  D. fresh decode of both handlers
  E. .text displacement scan for 0x6070/0x6074 (T-relative) and 0xa588/0xa58c
     (engine-relative) -> every toucher of the two correction fields
  F. direct callers of FUN_1800E1520 (E8/E9 rel32)
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


def va2fo(va):
    r = va - BASE
    for s in SECS:
        if s["va"] <= r < s["va"] + s["rsize"]:
            return s["ptr"] + (r - s["va"])
    return None


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def dis(va, end, title):
    print(f"=== {title} ===")
    fo = va2fo(va)
    code = DATA[fo:fo + (end - va)]
    for ins in MD.disasm(code, va):
        b = " ".join(f"{x:02x}" for x in ins.bytes)
        print(f"{ins.address:#x}  {b:<28} {ins.mnemonic} {ins.op_str}")
    print()


print("[A] m02 jump table at 0x1800E1890 (paramId-7 -> handler RVA)")
fo = va2fo(0x1800E1890)
for i in range(7):
    rva = struct.unpack_from("<I", DATA, fo + 4 * i)[0]
    print(f"  paramId {7 + i} (0x{7 + i:x}): -> {BASE + rva:#x}")
print()

print("[B] engine vtable 0x180409F60")
fo = va2fo(0x180409F60)
for i in range(6):
    p = struct.unpack_from("<Q", DATA, fo + 8 * i)[0]
    print(f"  [{i}] +{8 * i:#04x}: {p:#x}")
print("  .rdata scan for qword == 0x1800E1520 (all vtable slots holding m02):")
for s in SECS:
    if s["name"] in (".rdata", ".data"):
        blob = DATA[s["ptr"]:s["ptr"] + s["rsize"]]
        pat = struct.pack("<Q", 0x1800E1520)
        off = 0
        while True:
            off = blob.find(pat, off)
            if off < 0:
                break
            print(f"    {BASE + s['va'] + off:#x} (in {s['name']})")
            off += 1
print()

print("[C] constants of handlers 0x1800E1751 (p12) / 0x1800E17E1 (p13)")
for va, name, kind in (
    (0x180412320, "K1 (mulsd after [rax+4])", "d"),
    (0x180412200, "K2 (mulsd before exp)", "d"),
    (0x180412888, "K3 param12 (mulsd after log)", "d"),
    (0x180412540, "K3 param13 (mulsd after log)", "d"),
    (0x180412250, "0.23025850929 (gatelevel applier, ref)", "d"),
    (0x180411E44, "comiss small const (p12)", "f"),
    (0x180412C60, "andps mask (p12)", "m"),
):
    fo = va2fo(va)
    raw = DATA[fo:fo + (16 if kind == "m" else 8)]
    if kind == "d":
        print(f"  {va:#x} {name}: {struct.unpack('<d', raw[:8])[0]!r}")
    elif kind == "f":
        print(f"  {va:#x} {name}: {struct.unpack('<f', raw[:4])[0]!r}")
    else:
        print(f"  {va:#x} {name}: {raw.hex()}")
print()

print("[C2] T-relative offset arithmetic (T = engine + 0x4518)")
for eng, meaning in ((0xA58C, "p12 field"), (0xA588, "p13 field"),
                     (0xA4E0, "envelope ptr"), (0xA4E8, "gatelevel field")):
    print(f"  engine+{eng:#x} = T+{eng - 0x4518:#x}  ({meaning})")
print()

dis(0x1800E1751, 0x1800E17D1, "D1. m02 handler 0x1800E1751 (param 12?)")
dis(0x1800E17E1, 0x1800E1861, "D2. m02 handler 0x1800E17E1 (param 13?)")

print("[E] .text displacement scan for the two correction fields")
TEXT = next(s for s in SECS if s["name"] == ".text")
TBLOB = DATA[TEXT["ptr"]:TEXT["ptr"] + TEXT["rsize"]]
TVA = BASE + TEXT["va"]
WANT = {0x6070, 0x6074, 0xA588, 0xA58C}
hits = []
for ins in MD.disasm(TBLOB, TVA):
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base != capstone.x86.X86_REG_RIP:
            if op.mem.disp in WANT:
                hits.append((ins.address, ins.mnemonic, ins.op_str, ins.mem.disp if False else op.mem.disp))
for h in hits:
    print(f"  {h[0]:#x}  {h[1]} {h[2]}   [disp {h[3]:#x}]")
print(f"  total: {len(hits)} hits")
print()

print("[F] direct callers of FUN_1800E1520 (E8/E9 rel32 in .text)")
callers = []
for i in range(len(TBLOB) - 5):
    op = TBLOB[i]
    if op in (0xE8, 0xE9):
        rel = struct.unpack_from("<i", TBLOB, i + 1)[0]
        tgt = TVA + i + 5 + rel
        if tgt == 0x1800E1520:
            callers.append((TVA + i, "call" if op == 0xE8 else "jmp"))
for c in callers:
    print(f"  {c[0]:#x}: {c[1]} FUN_1800E1520")
print(f"  total: {len(callers)}")
