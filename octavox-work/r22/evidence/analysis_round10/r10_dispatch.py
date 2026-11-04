"""Round-10 part 3: engine-vtable dispatch sites (the m02 consumer chain).

Disassembles windows around every site that loads the qword at wrapper+0x430
(the embedded engine's vtable pointer) or leas the engine address, looking for
virtual calls into the engine vtable (slots +0x08 m01 / +0x10 m02 / ...).
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

SITES = [
    (0x180152123, "mov rcx,[rbx+0x430]"),
    (0x18016f9bb, "mov rax,[rbp+0x430]"),
    (0x1802fbbd0, "mov rsi,[rbp+0x430]"),
    (0x18032cf1a, "mov rcx,[r15+0x430]"),
    (0x18033062e, "mov rcx,[rsi+0x430]"),
    (0x18033553c, "mov rax,[rbp+0x430]"),
    (0x18030f920, "lea rcx,[rbp+0x430]"),
    (0x18030f956, "lea rdx,[rbp+0x430]"),
    (0x18030fd85, "lea rcx,[rbp+0x430]"),
    (0x18035d1a0, "lea rcx,[rdx+0x430]"),
    (0x1800cec90, "mov r9d,[rcx+0x430]"),
]

for va, why in SITES:
    print(f"=== {va:#x}  ({why}) ===")
    start = va - 0x20
    fo = va2fo(start)
    code = DATA[fo:fo + 0x70]
    for ins in MD.disasm(code, start):
        mark = " <<" if ins.address == va else ""
        print(f"  {ins.address:#x}  {ins.mnemonic} {ins.op_str}{mark}")
    print()
