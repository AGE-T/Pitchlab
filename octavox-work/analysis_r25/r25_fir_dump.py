#!/usr/bin/env python3
# PITCHLAB R25 -- dump tracker decimation-FIR coefficients + per-rate-class
# detector constants from the ORIGINAL Eventide Octavox VST3 (Ox.vst3).
#
# STATIC / READ-ONLY: the binary is only read + disassembled, NEVER executed,
# NEVER modified.
#
# Targets:
#   * m01 setparam FUN_1800E10B0 (0x1800E10B0..0x1800E14D0) - full disassembly
#   * engine ctor FUN_1800E0360 region 0x1800E0360..0x1800E1FF0 scanned for
#     `mov [reg+disp], imm32` with disp in [0xA3CC..0xA42C] and all
#     rip-relative loads (candidate .rdata constant tables)
#   * dispatch table raw bytes at 0x1800E14D0
#   * dump every rip-referenced .rdata location as f32/f64/hex tables
import hashlib
import struct
import capstone

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
OUT = "/home/z/my-project/octavox-work/analysis_r25/r25_fir_dump.txt"
SHA = "7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2"

# ---------------- PE loader (pattern from r22_pan_scan2.py) ----------------

def load_pe(path):
    data = open(path, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff)[0]
    opt_sz = struct.unpack_from("<H", data, e + 20)[0]
    ib = struct.unpack_from("<Q", data, coff + 20 + 24)[0]
    so = e + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = so + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, ib, secs


def va2off(secs, va, ib):
    rva = va - ib
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva)
    return None


def sec_of(secs, va, ib):
    rva = va - ib
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return name
    return "??"


# ---------------- float helpers ----------------

def bits2f32(bits):
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def fmt_f32(bits):
    """Return printable float32 interpretation, or None if implausible
    (denormals ~ integer constants, giant exponents)."""
    x = bits2f32(bits)
    if x == 0.0:
        return "0.0"
    ax = abs(x)
    if 1e-29 < ax < 1e29:
        return "%.9g" % x
    if ax <= 1e-29:  # denormal: almost surely an integer constant
        return None
    return "special(%r)" % x


def bits2f64(bits):
    return struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]


def fmt_f64(bits):
    x = bits2f64(bits)
    if x == 0.0:
        return "0.0"
    ax = abs(x)
    if 1e-300 < ax < 1e300:
        return "%.12g" % x
    if ax <= 1e-300:
        return None
    return "special(%r)" % x


# ---------------- capstone ----------------
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
X86 = capstone.x86

T_OFF = 0x4518  # tracker T = engine(dsp) + 0x4518
TRACKER_FIELDS = {
    0x5DB0: "raw decimated ring (64 f, mask 0x3F)",
    0x5EB0: "FIR coeff array c[0..20] (21 f)",
    0x5F04: "fwd tap count",
    0x5F08: "bwd tap count",
    0x5F0C: "LP ring index",
    0x5F10: "cadence counter",
    0x5F14: "cadence reload {2,4,8}",
    0x5F18: "energy leak 0.99182129",
    0x5F1C: "1/128 energy leak",
    0x5F20: "0.99219 peak decay",
    0x5F24: "peak",
    0x5F28: "gate factor",
    0x6004: "detection ring idx",
    0x600C: "rate (SR)",
}


def trk_name(t):
    if t in TRACKER_FIELDS:
        return TRACKER_FIELDS[t]
    if 0x5EB0 < t < 0x5F04:
        return "c[%d]" % ((t - 0x5EB0) // 4)
    return None


DATA, IB, SECS = load_pe(TARGET)
sha = hashlib.sha256(DATA).hexdigest()
assert sha == SHA, "SHA-256 mismatch: %s" % sha

riptargets = {}     # target VA -> [ref instruction addresses]
float_imm_stores = []   # (addr, mnemonic, opnd_str, imm, f32)
imm_stores_class = []   # mov [reg+disp],imm with disp in [0xA3C0..0xA440] engine-class area


def annotate(ins):
    """Build annotation notes for one instruction; record global facts."""
    notes = []
    for op in ins.operands:
        if op.type == X86.X86_OP_MEM:
            if op.mem.base == X86.X86_REG_RIP:
                tgt = ins.address + ins.size + op.mem.disp
                acc = ""
                try:
                    a = op.access
                    if a & capstone.CS_AC_WRITE:
                        acc = "W"
                    elif a & capstone.CS_AC_READ:
                        acc = "R"
                except Exception:
                    pass
                notes.append("RIP->0x%X[%s,%s]" % (tgt, sec_of(SECS, tgt, IB), acc))
                riptargets.setdefault(tgt, []).append(ins.address)
            elif op.mem.base != 0:
                d = op.mem.disp
                # interpretation A: base register is tracker T
                if d in TRACKER_FIELDS:
                    notes.append("T+0x%X: %s" % (d, TRACKER_FIELDS[d]))
                else:
                    n = trk_name(d)
                    if n and 0x5EB0 <= d < 0x5F04:
                        notes.append("T+0x%X: %s" % (d, n))
                # interpretation B: base register is engine (dsp)
                t = d - T_OFF
                if 0x5DB0 <= t <= 0x6010:
                    n = trk_name(t)
                    if n:
                        notes.append("eng+0x%X == T+0x%X: %s" % (d, t, n))
                if 0xA3C0 <= d <= 0xA440:
                    notes.append("eng-class-area +0x%X" % d)
    # immediate operands: float interpretations
    for op in ins.operands:
        if op.type == X86.X86_OP_IMM:
            v = op.imm & 0xFFFFFFFFFFFFFFFF
            f = fmt_f32(v & 0xFFFFFFFF)
            if f is not None:
                notes.append("f32lo=%s" % f)
            if v > 0xFFFFFFFF:
                fh = fmt_f32((v >> 32) & 0xFFFFFFFF)
                if fh is not None:
                    notes.append("f32hi=%s" % fh)
                fd = fmt_f64(v)
                if fd is not None:
                    notes.append("f64=%s" % fd)
            # record memory-immediate stores of plausible floats
            if ins.mnemonic.startswith("mov") and len(ins.operands) == 2:
                o0, o1 = ins.operands
                if (o0.type == X86.X86_OP_MEM and o1.type == X86.X86_OP_IMM
                        and o0.mem.base != X86.X86_REG_RIP):
                    if f is not None:
                        float_imm_stores.append((ins.address, ins.mnemonic,
                                                  ins.op_str, v & 0xFFFFFFFF, f))
                    d = o0.mem.disp
                    if 0xA3C0 <= d <= 0xA440:
                        imm_stores_class.append((ins.address, ins.mnemonic,
                                                 o0.mem.base, d, v & 0xFFFFFFFF,
                                                 f if f is not None else "(int)"))
    return "  ; " + " ".join(notes) if notes else ""


def disasm_region(start, end, tag, lines):
    off = va2off(SECS, start, IB)
    if off is None:
        lines.append("  <unmapped 0x%X>" % start)
        return
    lines.append("")
    lines.append("=" * 110)
    lines.append("-- %s : VA 0x%X .. 0x%X  (section %s)" %
                 (tag, start, end, sec_of(SECS, start, IB)))
    lines.append("=" * 110)
    code = DATA[off:off + (end - start)]
    for ins in md.disasm(code, start):
        note = annotate(ins)
        b = " ".join("%02X" % x for x in ins.bytes)
        lines.append("  %08X  %-24s %-8s %-52s%s" %
                     (ins.address, b, ins.mnemonic, ins.op_str, note))


def dump_rdata_table(va, lines, nf=96, nd=48):
    """Dump nf float32s and nd float64s (with hex) at VA, generously."""
    off = va2off(SECS, va, IB)
    if off is None:
        lines.append("  <unmapped>")
        return
    sec = sec_of(SECS, va, IB)
    lines.append("")
    lines.append("-" * 110)
    lines.append("## .rdata/.?data table at VA 0x%X (section %s) referenced from %s" %
                 (va, sec,
                  ", ".join("0x%X" % a for a in sorted(set(riptargets[va]))[:8]) +
                  ("" if len(set(riptargets[va])) <= 8 else " (+%d more)" %
                   (len(set(riptargets[va])) - 8))))
    lines.append("-" * 110)
    lines.append("### as float32 (x%d):" % nf)
    for i in range(nf):
        o = off + 4 * i
        if o + 4 > len(DATA):
            break
        w = struct.unpack_from("<I", DATA, o)[0]
        f = fmt_f32(w)
        s = f if f is not None else "(denorm/int)"
        lines.append("  0x%08X  +0x%03X  0x%08X  %s" % (va + 4 * i, 4 * i, w, s))
    lines.append("### as float64 (x%d):" % nd)
    for i in range(nd):
        o = off + 8 * i
        if o + 8 > len(DATA):
            break
        w = struct.unpack_from("<Q", DATA, o)[0]
        f = fmt_f64(w)
        s = f if f is not None else "(denorm/int)"
        lines.append("  0x%08X  +0x%03X  0x%016X  %s" % (va + 8 * i, 8 * i, w, s))
    # raw hex for eyeballing structure (zeros etc.)
    lines.append("### raw first 0x100 bytes:")
    for i in range(0, 0x100, 16):
        o = off + i
        if o + 16 > len(DATA):
            break
        row = DATA[o:o + 16]
        lines.append("  0x%08X  %s" % (va + i, " ".join("%02X" % x for x in row)))


def main():
    L = []
    L.append("PITCHLAB R25 -- tracker decimation FIR coefficient dump (STATIC, read-only)")
    L.append("target: %s" % TARGET)
    L.append("SHA-256: %s (verified)" % sha)
    L.append("image base: 0x%X" % IB)
    L.append("sections: " + ", ".join("%s@0x%X+0x%X" % (n, IB + v, s) for n, v, s, r, rs in SECS))
    L.append("")

    # 1) full disassembly: engine ctor region
    disasm_region(0x1800E0360, 0x1800E10B0, "engine ctor FUN_1800E0360 (0x1800E0360..0x1800E10B0)", L)
    # 2) full disassembly: m01 setparam
    disasm_region(0x1800E10B0, 0x1800E14D0, "m01 setparam FUN_1800E10B0 (0x1800E10B0..0x1800E14D0)", L)
    # 3) region after setparam (dispatch table 0x1800E14D0 lives here)
    disasm_region(0x1800E14D0, 0x1800E1FF0, "post-setparam region (dispatch table area)", L)

    # dispatch table raw dump at 0x1800E14D0
    off = va2off(SECS, 0x1800E14D0, IB)
    L.append("")
    L.append("=" * 110)
    L.append("-- dispatch table @0x1800E14D0 raw (as u64 then u32)")
    L.append("=" * 110)
    for i in range(24):
        w = struct.unpack_from("<Q", DATA, off + 8 * i)[0]
        L.append("  qword[%2d] 0x%08X = 0x%016X%s" %
                 (i, 0x1800E14D0 + 8 * i, w,
                   "  (code ptr)" if 0x180000000 <= w < 0x181000000 else ""))
    for i in range(48):
        w = struct.unpack_from("<I", DATA, off + 4 * i)[0]
        L.append("  dword[%2d] 0x%08X = 0x%08X%s" %
                 (i, 0x1800E14D0 + 4 * i, w,
                   "  (rva-ish)" if 0xE0000 < w < 0x200000 else ""))

    # 4) engine-class-area immediate stores summary
    L.append("")
    L.append("=" * 110)
    L.append("-- SUMMARY: immediate stores with disp in [0xA3C0..0xA440] (engine 'class table' area)")
    L.append("=" * 110)
    if not imm_stores_class:
        L.append("  (none found)")
    for (a, m, base, d, v, f) in imm_stores_class:
        reg = ins_reg(base)
        L.append("  0x%08X  %s [%s+0x%X], 0x%08X   f32=%s" % (a, m, reg, d, v, f))

    # 5) ALL plausible-float immediate stores in scanned region
    L.append("")
    L.append("=" * 110)
    L.append("-- SUMMARY: ALL mov [reg+disp], imm with plausible f32 imm (0x1800E0360..0x1800E1FF0)")
    L.append("=" * 110)
    if not float_imm_stores:
        L.append("  (none found)")
    for (a, m, ops, w, f) in float_imm_stores:
        L.append("  0x%08X  %-6s %s   -> 0x%08X f32=%s" % (a, m, ops, w, f))

    # 6) rip-relative referenced data tables
    L.append("")
    L.append("=" * 110)
    L.append("-- RIP-relative referenced locations (%d unique) -> dump tables" % len(riptargets))
    L.append("=" * 110)
    for va in sorted(riptargets):
        nrefs = len(set(riptargets[va]))
        L.append("  0x%X  (%s)  refs=%d  from %s" %
                 (va, sec_of(SECS, va, IB), nrefs,
                  ", ".join("0x%X" % a for a in sorted(set(riptargets[va]))[:12])))
    for va in sorted(riptargets):
        dump_rdata_table(va, L)

    open(OUT, "w").write("\n".join(L) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(L)))

    # ---- compact stdout summary ----
    print("\n== imm stores in engine class area [0xA3C0..0xA440]: %d" % len(imm_stores_class))
    for (a, m, base, d, v, f) in imm_stores_class:
        print("  0x%08X  %s [%s+0x%X], 0x%08X  f32=%s" % (a, m, ins_reg(base), d, v, f))
    print("\n== plausible float immediate stores: %d" % len(float_imm_stores))
    for (a, m, ops, w, f) in float_imm_stores:
        print("  0x%08X  %-6s %-46s 0x%08X f32=%s" % (a, m, ops, w, f))
    print("\n== rip-relative targets: %d" % len(riptargets))
    for va in sorted(riptargets):
        print("  0x%X (%s) refs=%d" % (va, sec_of(SECS, va, IB), len(set(riptargets[va]))))


def ins_reg(regid):
    try:
        return md.reg_name(regid)
    except Exception:
        return "r?"


if __name__ == "__main__":
    main()
