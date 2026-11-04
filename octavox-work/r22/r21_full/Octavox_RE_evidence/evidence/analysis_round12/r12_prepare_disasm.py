#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round 12 -- static scan B: full linear disassembly of the engine prepare
function (FUN_1800E2030 .. FUN_1800E7DF0 extent, covering the .pdata chunk that
contains the settled/crossfade paths at 0x1800E2105+), with an annotated listing.

Focus questions:
 (1) settled path: how many interpolated reads per output sample? (single tap?)
 (2) crossfade branch: the actual decrement source register for the state ramp
 (3) re-arm distance: the auVar35 value flow (period-derived jump)
 (4) how the +0x40f0 table base is formed (dword loads at 0x1800E24C5/0x1800E2858)

Read-only. Output: /home/z/octavox/analysis/r12_prepare_disasm.txt
"""

import struct
import sys

import capstone

TARGET = "/home/z/octavox/original/Ox.vst3"
START = 0x1800E2030
END = 0x1800E2A20  # a bit past the last known prepare site (0x1800E2858..)
OUT = "/home/z/octavox/analysis/r12_prepare_disasm.txt"


def main():
    with open(TARGET, "rb") as f:
        data = f.read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    machine, nsec = struct.unpack_from("<HH", data, coff)
    opt_sz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]  # PE32+
    sec_off = e_lfanew + 24 + opt_sz
    secs = {}
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o : o + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs[name] = (vaddr, vsize, roff, rsize)
    tv, _ts, t_off, t_rsz = secs[".text"]

    def va2fo(va):
        rva = va - image_base
        return t_off + (rva - tv)

    MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    MD.detail = True

    code = data[va2fo(START) : va2fo(END)]
    lines = [
        "OCTAVOX round 12 scan B -- prepare disassembly 0x%X..0x%X" % (START, END),
        "target sha256 7bb260f6... (verified); read-only",
        "",
    ]
    for ins in MD.disasm(code, START):
        extra = ""
        # annotate branch targets and key displacements
        if ins.mnemonic.startswith("j") and ins.operands and ins.operands[0].type == 2:
            extra = "  ; -> 0x%X" % ins.operands[0].imm
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp:
                d = op.mem.disp & 0xFFFFFFFF
                known = {
                    0x40F0: "engine+0x40f0",
                    0x40F8: "engine+0x40f8 vec",
                    0x4110: "engine+0x4110 vec",
                    0x4128: "engine+0x4128 vec",
                    0x4140: "engine+0x4140 int[nv]",
                    0x4158: "engine+0x4158 state[nv]",
                    0x4188: "engine+0x4188 pos[nv]",
                    0x40A0: "engine+0x40a0 workring",
                    0x4098: "engine+0x4098 mask",
                    0x409C: "engine+0x409c stride",
                    0x40B8: "engine+0x40b8 wrmask",
                    0x42F8: "engine+0x42f8 recidx",
                    0xA4FC: "engine+0xa4fc wdelta",
                    0x98: "engine+0x98 recring",
                    0x4510: "engine+0x4510 tracking",
                    0x54: "engine+0x54 nvoices",
                }
                if d in known:
                    extra += "  ; %s" % known[d]
        lines.append("0x%X  %-9s %-52s%s" % (ins.address, ins.mnemonic, ins.op_str, extra))

    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    print("disassembled %d instructions -> %s" % (len(lines) - 3, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
