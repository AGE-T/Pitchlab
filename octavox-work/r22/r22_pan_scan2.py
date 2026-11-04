#!/usr/bin/env python3
# PITCHLAB R22 -- pan scan, stage 2: trace the stereomixer3 sub-functions
# 0x1800D8230 and 0x1800D7400 (called by the composite add_fn 0x180137720).
# Read-only static disassembly. The binary is NEVER executed.
import struct, capstone

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
OUT = "/home/z/my-project/octavox-work/Octavox_RE_evidence/evidence/analysis_round22/r22_pan_scan.txt"

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
        o = so + 40*i
        name = data[o:o+8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, ib, secs

def va2off(secs, va, ib):
    rva = va - ib
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva)
    return None

def go(data, secs, ib, start, end, tag, marks=None):
    L = ["", "-- %s: 0x%X..0x%X" % (tag, start, end)]
    off = va2Off_(secs, start, ib)
    if off is None:
        L.append("  <unmapped>")
        return L
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for ins in md.disasm(data[off:off + (end-start)], start):
        mk = ""
        if marks and ins.address in marks:
            mk = "   <== " + marks[ins.address]
        L.append("  %08X  %-9s %-48s%s" % (ins.address, ins.mnemonic, ins.op_str, mk))
    return L

def va2Off_(secs, va, ib):
    rva = va - ib
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva)
    return None

def main():
    data, ib, secs = load_pe(TARGET)
    L = open(OUT).read().split("\n") if False else []
    L = ["PITCHLAB R22 -- pan scan stage 2: the stereomixer3 sub-functions (static)", "="*78]
    # sub-function 1: 0x1800D8230 (called with rdx = inputs+0x260)
    L += go(data, secs, ib, 0x1800D8230, 0x1800D8400, "sub-fn A 0x1800D8230 (input gather?)")
    # sub-function 2: 0x1800D7400 (called with rcx = obj+0xF0)
    L += go(data, secs, ib, 0x1800D7400, 0x1800D7600, "sub-fn B 0x1800D7400 (mix?)")
    open(OUT, "a").write("\n".join(L) + "\n")
    print("appended stage 2")

if __name__ == "__main__":
    main()
