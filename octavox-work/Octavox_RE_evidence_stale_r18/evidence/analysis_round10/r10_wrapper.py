"""Round-10 part 4: locate the wrapper vtable and the engine-vtable dispatch calls.

1. .rdata scan for the thunk 0x18035D1A0 (lea rcx,[rdx+0x430]; jmp 0x1800C48E0)
   -> the wrapper's vtable(s).
2. Read the candidate wrapper vtable slots.
3. .text byte scan for call/jmp [reg+0x10] (FF 5x/6x 10, incl. REX.B) and keep
   those with a 0x430-displacement instruction (lea/mov) within the previous
   32 bytes -> engine vtable slot +0x10 dispatch candidates (m02 consumers).
Read-only.
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

print("[1] .rdata/.data scan for qword == 0x18035D1A0 (the +0x430 thunk)")
for s in SECS:
    if s["name"] in (".rdata", ".data"):
        blob = DATA[s["ptr"]:s["ptr"] + s["rsize"]]
        for pat, label in ((struct.pack("<Q", 0x18035D1A0), "thunk 0x18035D1A0"),):
            off = 0
            while True:
                off = blob.find(pat, off)
                if off < 0:
                    break
                va = BASE + s["va"] + off
                print(f"  {label} referenced at {va:#x} (in {s['name']})")
                off += 1
print()

print("[2] read vtable candidates around the hit")
for va in (0x180409E60,):
    fo = va2fo(va)
    print(f"  vtable window at {va:#x}:")
    for i in range(24):
        p = struct.unpack_from("<Q", DATA, fo + 8 * i)[0]
        print(f"    [{i}] +{8 * i:#04x} (va {va + 8 * i:#x}): {p:#x}")
print()

print("[3] call/jmp [reg+0x10] with 0x430-context in the previous 32 bytes")
TEXT = next(s for s in SECS if s["name"] == ".text")
TB = DATA[TEXT["ptr"]:TEXT["ptr"] + TEXT["rsize"]]
TVA = BASE + TEXT["va"]
DISP = struct.pack("<i", 0x430)
hits = []
for i in range(len(TB) - 4):
    b0, b1, b2 = TB[i], TB[i + 1], TB[i + 2]
    rex = False
    j = i
    if b0 == 0x41 and TB[i + 1] in (0xFF,):
        rex = True
        j = i + 1
        b0, b1, b2 = TB[j], TB[j + 1], TB[j + 2]
    if b0 == 0xFF and b1 in range(0x50, 0x68) and b2 == 0x10:
        # call (0x50-0x57) / jmp (0x60-0x67) [reg+0x10]
        window = TB[max(0, j - 32):j]
        if DISP in window:
            hits.append((TVA + i, "call" if 0x50 <= b1 < 0x58 else "jmp"))
for h in hits:
    print(f"  {h[0]:#x}: {h[1]} qword ptr [reg + 0x10]  (0x430-context nearby)")
print(f"  total: {len(hits)}")
print()
for va, _ in hits[:6]:
    start = va - 0x30
    fo = va2fo(start)
    code = DATA[fo:fo + 0x50]
    print(f"  --- context {va:#x} ---")
    for ins in MD.disasm(code, start):
        mark = " <<" if ins.address == va else ""
        print(f"    {ins.address:#x}  {ins.mnemonic} {ins.op_str}{mark}")
    print()
