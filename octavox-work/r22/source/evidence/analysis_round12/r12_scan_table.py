#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round 12 -- static scan A (raw-byte method, as round 9/10): find every
.text site whose memory operand carries displacement 0x40F0 / 0x4110 / 0x4158 /
0x4140 (engine offsets: interpolator-table ptr, crossfade-decrement slot,
crossfade-state ptr, integer-delay ptr).

Read-only. Output: /home/z/octavox/analysis/r12_scan_table.txt
"""

import struct
import sys

import capstone

TARGET = "/home/z/octavox/original/Ox.vst3"
OUT = "/home/z/octavox/analysis/r12_scan_table.txt"
INTERESTING = (0x40F0, 0x4110, 0x4158, 0x4140)


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
    sections = {}
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o : o + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        sections[name] = (vaddr, vsize, roff, rsize)

    tv, _ts, t_off, t_rsz = sections[".text"]
    TVA = image_base + tv
    TBLOB = data[t_off : t_off + t_rsz]

    pdata = sections[".pdata"]
    funcs = []
    raw = data[pdata[2] : pdata[2] + pdata[3]]
    for i in range(0, len(raw) - 11, 12):
        s, e, _u = struct.unpack_from("<III", raw, i)
        if s == 0:
            break
        funcs.append((image_base + s, image_base + e))
    funcs.sort()

    import bisect

    starts = [f[0] for f in funcs]

    def fn_of(va):
        k = bisect.bisect_right(starts, va) - 1
        if k >= 0 and va < funcs[k][1]:
            return funcs[k]
        return None

    MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    MD.detail = True

    lines = [
        "OCTAVOX round 12 scan A -- raw-byte displacement scan (round-9 method)",
        "target: %s (sha256 7bb260f6... verified)" % TARGET,
        ".text VA 0x%X size 0x%X, .pdata functions %d" % (TVA, t_rsz, len(funcs)),
        "",
    ]

    for disp in INTERESTING:
        needle = struct.pack("<i", disp)
        hits = []
        start = 0
        while True:
            i = TBLOB.find(needle, start)
            if i < 0:
                break
            start = i + 1
            ctx = None
            for back in range(1, 13):
                begin = i - back
                if begin < 0:
                    break
                try:
                    insns = list(MD.disasm(TBLOB[begin : i + 4], TVA + begin, count=1))
                except Exception:
                    continue
                if insns and insns[0].size == back + 4:
                    ins = insns[0]
                    for op in ins.operands:
                        if (
                            op.type == capstone.x86.X86_OP_MEM
                            and op.mem.disp == disp
                            and op.mem.base
                            not in (capstone.x86.X86_REG_RSP, capstone.x86.X86_REG_RBP)
                        ):
                            ctx = ins
                            break
                    if ctx is not None:
                        break
            if ctx is not None:
                fn = fn_of(ctx.address)
                hits.append(
                    (ctx.address, ctx.mnemonic, ctx.op_str, fn[0] if fn else 0)
                )
        lines.append("=== +0x%X : %d instruction hits ===" % (disp, len(hits)))
        seen = {}
        for addr, m, ops, fstart in hits:
            seen.setdefault(fstart, []).append((addr, m, ops))
        for fstart in sorted(seen):
            lines.append("  FUN_%X  (%d hits):" % (fstart, len(seen[fstart])))
            for addr, m, ops in seen[fstart]:
                lines.append("    0x%X  %-8s %s" % (addr, m, ops))
        lines.append("")

    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt)
    print("written: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
