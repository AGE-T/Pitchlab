#!/usr/bin/env python3
# PITCHLAB R22 -- static scan: the stereomixer3 pan mix law (PRIORITY 5, item 1).
# Read-only disassembly of the original binary (NEVER executed).
# Goal: push R21's UNK mix-law toward arithmetic by tracing the composite
# add_fn 0x180137720 and the coefficient-table family it selects.
import struct, capstone, json, math

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
OUT = "/home/z/my-project/octavox-work/Octavox_RE_evidence/evidence/analysis_round22/r22_pan_scan.txt"

def load_pe(path):
    data = open(path, "rb").read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", data, coff)[0]
    opt_sz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    sec_off = e_lfanew + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o:o+8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, image_base, secs

def va_to_off(secs, va, image_base):
    rva = va - image_base
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva), name
    return None, None

def disasm_range(data, secs, ib, start, end, tag, marks=None):
    L = []
    off, sec = va_to_off(secs, start, ib)
    if off is None:
        return ["  <no mapping for 0x%X>" % start]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    code = data[off:off + (end - start)]
    L.append("-- %s: 0x%X..0x%X (%s)" % (tag, start, end, sec))
    for ins in md.disasm(code, start):
        mk = ""
        if marks and ins.address in marks:
            mk = "   <== " + marks[ins.address]
        L.append("  %08X  %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, mk))
    return L

def main():
    data, ib, secs = load_pe(TARGET)
    L = ["PITCHLAB R22 -- stereomixer3 PAN MIX LAW SCAN (static; never executed)", "="*78, ""]

    # 1. the composite add_fn
    L += disasm_range(data, secs, ib, 0x180137720, 0x180137900,
                      "stereomixer3 add_fn (composite wrapper)",
                      marks={0x180137720: "entry"})
    L.append("")

    # 2. The sub-module ctors the R21 scan saw: 0x1800E2C00 / 0x1800E0360 families
    L += disasm_range(data, secs, ib, 0x1800E2C00, 0x1800E2D40, "sub ctor A (0x1800E2C00)")
    L.append("")
    L += disasm_range(data, secs, ib, 0x1800E0360, 0x1800E0480, "sub ctor B (0x1800E0360)")
    L.append("")

    # 3. Scan .rdata for pan-law-like tables: interleaved L/R gain pairs whose
    #    powers sum ~1 (constant-power) or linear pairs summing to 1. Search a
    #    broad .rdata window for float32 pairs (a,b) with a in [0,1], b in [0,1].
    rdata_off, rdata_name = None, None
    for name, va, vsize, raw, rsize in secs:
        if name == ".rdata":
            rdata_off, rdata_va, rdata_sz = raw, va, rsize
            break
    L.append("-- .rdata pan-table scan (float32 pairs, power/linear candidates)")
    hits = []
    if rdata_off is not None:
        base = 0x180000000 + rdata_va
        buf = data[rdata_off:rdata_off + rdata_sz]
        import math
        i = 0
        while i + 8 * 32 <= len(buf):
            # look for runs of >=16 floats all in [0,1] with pattern structure
            ok = True
            vals = []
            for k in range(32):
                f = struct.unpack_from("<f", buf, i + 4*k)[0]
                if not (0.0 <= f <= 1.0000001) or f != f:
                    ok = False
                    break
                vals.append(f)
            if ok:
                # candidate: check symmetric-complement structure
                pairs_ok = 0
                for k in range(0, 32, 2):
                    a, b = vals[k], vals[k+1]
                    if abs(a*a + b*b - 1.0) < 0.01 and a >= b:
                        pairs_ok += 1
                    elif abs(a + b - 1.0) < 0.01:
                        pairs_ok += 10
                if pairs_ok >= 6:
                    hits.append((base + i, vals))
                    i += 128
                    continue
            i += 4
    for va, vals in hits[:40]:
        kind = "power" if all(abs(vals[k]**2 + vals[k+1]**2 - 1) < 0.01 for k in range(0, 32, 2)) else "linear?"
        L.append("  0x%X  %s  %s" % (va, kind, " ".join("%.4f" % v for v in vals[:16])))
    L.append("  total candidate tables: %d" % len(hits))
    L.append("")

    # 4. sqrt-related constants: scan .rdata doubles for 0.7071.., 1/sqrt2, etc.
    L.append("-- .rdata constant scan (pan-law doubles)")
    found = []
    if rdata_off is not None:
        buf = data[rdata_off:rdata_off + rdata_sz]
        base = 0x180000000 + rdata_va
        for i in range(0, len(buf) - 8, 8):
            d = struct.unpack_from("<d", buf, i)[0]
            if d != d or d == 0.0:
                continue
            if (abs(d - 0.7071067811865476) < 1e-15 or
                abs(d - 0.5) < 1e-16 or abs(d - 0.25) < 1e-16 or
                abs(d - math.pi/4) < 1e-15 or abs(d - 0.7853981633974483) < 1e-15 or
                abs(d - 0.6000000000000001) < 1e-15 or
                abs(d - 0.6400000000000001) < 1e-15):
                found.append((base + i, d))
    for va, d in found[:30]:
        L.append("  0x%X  %.19g" % (va, d))
    L.append("  (0.5/0.7071/pi-over-4 hits: %d)" % len(found))

    open(OUT, "w").write("\n".join(L) + "\n")
    print("wrote", OUT, "lines:", len(L), "table hits:", len(hits))

if __name__ == "__main__":
    main()
