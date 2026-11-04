#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round 12 -- scan C: (a) dump every rip-referenced constant used by the
prepare function; (b) disassemble the ctor FUN_1800E0360 and locate how the
engine+0x40f0 table pointer is initialised; (c) if the table is static .rdata,
dump the 256x6 float table and compare against 6-point Lagrange.

Read-only. Output: /home/z/octavox/analysis/r12_constants_and_table.txt
"""

import struct
import sys

import capstone

TARGET = "/home/z/octavox/original/Ox.vst3"
OUT = "/home/z/octavox/analysis/r12_constants_and_table.txt"

PREPARE = (0x1800E2030, 0x1800E2A20)
CTOR = (0x1800E0360, 0x1800E0F40)


def load():
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
    return data, image_base, secs


def va2fo(secs, image_base, va):
    rva = va - image_base
    for name, (vaddr, vsize, roff, rsize) in secs.items():
        if vaddr <= rva < vaddr + vsize:
            return roff + (rva - vaddr)
    return None


def main():
    data, image_base, secs = load()
    MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    MD.detail = True
    lines = []

    # ---- (a) constants referenced by the prepare --------------------------
    lines.append("== (a) rip-referenced constants in the prepare FUN_1800E2030 ==")
    fo = va2fo(secs, image_base, PREPARE[0])
    code = data[fo : fo + (PREPARE[1] - PREPARE[0])]
    consts = {}
    for ins in MD.disasm(code, PREPARE[0]):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                tgt = ins.address + ins.size + op.mem.disp
                tfo = va2fo(secs, image_base, tgt)
                if tfo is None:
                    continue
                raw4 = data[tfo : tfo + 4]
                raw8 = data[tfo : tfo + 8]
                f4 = struct.unpack("<f", raw4)[0] if len(raw4) == 4 else None
                f8 = struct.unpack("<d", raw8)[0] if len(raw8) == 8 else None
                consts.setdefault(tgt, []).append(
                    (ins.address, ins.mnemonic, f4, f8, raw4.hex())
                )
    for tgt in sorted(consts):
        uses = consts[tgt]
        f4 = uses[0][2]
        f8 = uses[0][3]
        hex4 = uses[0][4]
        lines.append(
            "  0x%X  used %dx  f32=%r  f64=%r  bytes=%s  (first ref 0x%X %s)"
            % (tgt, len(uses), f4, f8, hex4, uses[0][0], uses[0][1])
        )

    # ---- (b) ctor: find the +0x40f0 initialisation -------------------------
    lines.append("")
    lines.append("== (b) ctor FUN_1800E0360: stores to engine+0x40f0 region ==")
    fo = va2fo(secs, image_base, CTOR[0])
    code = data[fo : fo + (CTOR[1] - CTOR[0])]
    found = []
    for ins in MD.disasm(code, CTOR[0]):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM:
                d = op.mem.disp & 0xFFFFFFFF
                if d in (0x40F0, 0x40E8, 0x40EC, 0x40F4) or (0x40D8 <= d <= 0x40F8):
                    found.append((ins.address, ins.mnemonic, ins.op_str, d))
    for a, m, o, d in found:
        lines.append("  0x%X  %-8s %-40s  disp=0x%X" % (a, m, o, d))
    if not found:
        lines.append("  (no direct displacement stores -- pointer must come from a call)")

    # rip-lea targets in the ctor (static tables?)
    lines.append("")
    lines.append("== (b2) ctor rip-lea targets (potential static tables) ==")
    leas = []
    for ins in MD.disasm(code, CTOR[0]):
        if ins.mnemonic == "lea":
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    leas.append((ins.address, tgt))
    for a, t in leas:
        lines.append("  lea at 0x%X -> 0x%X" % (a, t))

    # ---- (c) if any target looks like a 0x1800-sized table, dump it --------
    lines.append("")
    lines.append("== (c) candidate 256x6 float tables in .rdata ==")

    def lag6(mu):
        xs = (-2.0, -1.0, 0.0, 1.0, 2.0, 3.0)
        w = [1.0] * 6
        for i in range(6):
            for j in range(6):
                if i != j:
                    w[i] *= (mu - xs[j]) / (xs[i] - xs[j])
        return w

    for a, t in leas:
        tfo = va2fo(secs, image_base, t)
        if tfo is None:
            continue
        # test: 256 rows x 24 bytes = 0x1800
        tbl = data[tfo : tfo + 0x1800]
        if len(tbl) < 0x1800:
            continue
        vals = struct.unpack("<1536f", tbl)
        # row 0 and row 128
        r0 = vals[0:6]
        r128 = vals[128 * 6 : 129 * 6]
        lag0 = lag6(0.0)
        lagc = lag6(0.5)
        lage = lag6(1.0 / 256.0)
        lines.append(
            "  table @0x%X: row0=%s row128=%s"
            % (
                t,
                ["%.6f" % v for v in r0],
                ["%.6f" % v for v in r128],
            )
        )
        lines.append(
            "    lagrange(mu=0)=%s  lagrange(mu=1/256)=%s  lagrange(mu=0.5)=%s"
            % (["%.6f" % v for v in lag0], ["%.6f" % v for v in lage], ["%.6f" % v for v in lagc])
        )

    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt)
    print("\nwritten: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
