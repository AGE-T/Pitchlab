"""Round-10 fresh decoder: independent re-disassembly of the round-10 key sites.

Read-only against /home/z/octavox/original/Ox.vst3. Uses capstone directly
(no reuse of round-9 renderers, so the operand text here is independently
generated). Sites:
  A. coefficient stores        0x1800E1F80..0x1800E1FB2
  B. prepare quartic           0x1800E2330..0x1800E2444
  C. noise generator           0x180139B40..0x180139C00
  D. noise defaults            0x180139C00..0x180139C50
  E. rdm slew                  0x180139790..0x1801397D9
  F. rdata doubles             0x1804120B0 / 0x1804121B0 / 0x180412490
"""
import struct
import sys
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
    print(f"=== {title} ({va:#x}..{end:#x}) ===")
    fo = va2fo(va)
    code = DATA[fo:fo + (end - va)]
    for ins in MD.disasm(code, va):
        b = " ".join(f"{x:02x}" for x in ins.bytes)
        print(f"{ins.address:#x}  {b:<30} {ins.mnemonic} {ins.op_str}")
    print()


dis(0x1800E1F80, 0x1800E1FB2, "A. coefficient stores")
dis(0x1800E2330, 0x1800E2348, "B1. prepare: F/port load head")
dis(0x1800E2348, 0x1800E2410, "B2. prepare: f product + quartic")
dis(0x1800E2408, 0x1800E2444, "B3. prepare: float32 rounding + slot/64 multiply")
dis(0x180139B40, 0x180139C00, "C. noise generator process")
dis(0x180139C00, 0x180139C50, "D. noise defaults")
dis(0x180139790, 0x1801397D9, "E. rdm slew head (one channel)")

print("=== F. rdata doubles ===")
for va in (0x1804120B0, 0x1804121B0, 0x180412490, 0x180412480, 0x180412498):
    fo = va2fo(va)
    raw = DATA[fo:fo + 8]
    print(f"{va:#x}: {raw.hex()}  double={struct.unpack('<d', raw)[0]!r}")
print()

print("=== scale constant decode ===")
bits = 0x34000001
f32 = struct.unpack("<f", struct.pack("<I", bits))[0]
import math
print(f"0x34000001 as float32 = {f32!r} = 2^-23*(1+2^-23) ? "
      f"{f32 == math.ldexp(1.0, -23) * (1 + math.ldexp(1.0, -23))}")
print(f"  2^-23              = {math.ldexp(1.0, -23)!r}")
print(f"  1+2^-23            = {1 + math.ldexp(1.0, -23)!r}")
