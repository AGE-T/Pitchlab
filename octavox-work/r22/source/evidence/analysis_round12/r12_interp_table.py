#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCTAVOX round 12 -- scan D: dump and analyse the static interpolator table at
0x1803B4570 (engine+0x40f0, 6 floats x 0x18 stride x 256 phases per the prepare
reads).  Determine the kernel design: row sums, effective fractional delay,
comparison with 6-point Lagrange and with a 6-tap windowed-sinc design.

Read-only. Output: /home/z/octavox/analysis/r12_interp_table.txt
"""

import math
import struct
import sys

TARGET = "/home/z/octavox/original/Ox.vst3"
TABLE_VA = 0x1803B6170
OUT = "/home/z/octavox/analysis/r12_interp_table.txt"
NROWS = 256


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


def lag6(mu):
    xs = (-2.0, -1.0, 0.0, 1.0, 2.0, 3.0)
    w = [1.0] * 6
    for i in range(6):
        for j in range(6):
            if i != j:
                w[i] *= (mu - xs[j]) / (xs[i] - xs[j])
    return w


def main():
    data, image_base, secs = load()
    tfo = va2fo(secs, image_base, TABLE_VA)
    rows = []
    for r in range(NROWS):
        vals = struct.unpack_from("<6f", data, tfo + r * 0x18)
        rows.append(vals)

    lines = []
    lines.append("OCTAVOX round 12 -- static interpolator table analysis")
    lines.append("table VA 0x%X  (engine+0x40f0 <- lea 0x1800E1FB4 + mov 0x1800E1FBB in FUN_1800E18B0, OBS)" % TABLE_VA)
    lines.append("")

    # row sums and effective delays
    lines.append("row : weights (6)                                   sum      eff.delay(tap2=0)")
    sums = []
    delays = []
    for r in range(NROWS):
        w = rows[r]
        s = sum(w)
        # effective delay relative to tap index 2 (the third tap), assuming
        # weight k applies to ring[w0 + k]
        d = sum((k - 2) * w[k] for k in range(6))
        sums.append(s)
        delays.append(d)
    for r in list(range(0, 8)) + [16, 32, 64, 96, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 192, 248, 254, 255]:
        w = rows[r]
        lines.append(
            "%3d : %-48s %+8.5f  %+8.5f"
            % (r, " ".join("%+9.6f" % v for v in w), sums[r], delays[r])
        )
    lines.append("")

    nz = [r for r in range(NROWS) if any(v != 0.0 for v in rows[r])]
    lines.append("non-zero rows: %d (of %d); zero rows: %s" % (len(nz), NROWS, [r for r in range(NROWS) if r not in nz][:20]))
    lines.append("sum range: [%.6f, %.6f]" % (min(sums), max(sums)))
    lines.append("eff.delay range: [%.6f, %.6f]" % (min(delays), max(delays)))
    # find the row closest to identity at tap 2
    best = min(range(NROWS), key=lambda r: sum(abs(rows[r][k] - (1.0 if k == 2 else 0.0)) for k in range(6)))
    lines.append("row closest to identity(tap2): %d -> %s" % (best, ["%.6f" % v for v in rows[best]]))
    lines.append("")

    # hypothesis A: lagrange with phase mapping mu = (r + off)/256
    lines.append("hypothesis A: 6-point Lagrange, mu=(r+off)/256 -- best off per row (L2):")
    for r in range(0, NROWS, 32):
        w = rows[r]
        bo, be = 0.0, 1e9
        for i in range(-64, 320):
            mu = (r + i / 64.0) / 256.0
            lw = lag6(mu)
            e = sum((a - b) ** 2 for a, b in zip(w, lw))
            if e < be:
                bo, be = i / 64.0, e
        lines.append("  row %3d: best offset %+5.2f phases (mu=%.4f), rms err %.2e" % (r, bo, (r + bo) / 256.0, math.sqrt(be)))
    lines.append("")

    # hypothesis B: mu = r/256 with a different tap alignment (weights shifted)
    lines.append("hypothesis B: lagrange(mu=r/256) with tap order rotated:")
    for r in (0, 1, 2, 64, 128, 255):
        lw = lag6(r / 256.0)
        lines.append("  row %3d vs lag(mu): target=%s" % (r, ["%+.4f" % v for v in rows[r]]))
        lines.append("            lag    =%s" % (["%+.4f" % v for v in lw],))
    lines.append("")

    # raw bytes of first 4 rows for the record
    lines.append("raw first 0x60 bytes: " + data[tfo : tfo + 0x60].hex())

    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt)
    print("\nwritten: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
