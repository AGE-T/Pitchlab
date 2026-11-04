#!/usr/bin/env python3
# PITCHLAB R22 -- static scan: the Minimum Pitch choice->tracker-domain
# conversion + sample-rate interaction (PRIORITY 5, item 7).
# Read-only. The binary is NEVER executed.
# Baseline (R21 P5): Minimum Pitch = 48 choices C0..B3, default 24 = C2;
# consumer m01 writes T+0x5F70 (base) / T+0x5F74 (max). This scan searches
# .text for writes to +0x5F70/+0x5F74 and disassembles the surroundings.
import struct, capstone

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
OUT = "/home/z/my-project/octavox-work/Octavox_RE_evidence/evidence/analysis_round22/r22_minpitch_scan.txt"

def load_pe(path):
    data = open(path, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e+4)[0]
    opt_sz = struct.unpack_from("<H", data, e+20)[0]
    ib = struct.unpack_from("<Q", data, e+4+20+24)[0]
    so = e + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = so + 40*i
        name = data[o:o+8].rstrip(b"\0").decode("ascii","replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o+8)
        secs.append((name, va, vsize, raw, rsize))
    return data, ib, secs

def main():
    data, ib, secs = load_pe(TARGET)
    text = None
    for name, va, vsize, raw, rsize in secs:
        if name == ".text":
            text = (va, vsize, raw, rsize)
    tva, tsz, traw, trsz = text
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    L = ["PITCHLAB R22 -- Minimum Pitch conversion scan (static; never executed)", "="*78]
    # scan .text for "mov [reg+0x5F70], ..." / "mov [reg+0x5F74], ..." encodings.
    # mov dword [reg+disp32]: 89 /r with disp32 0x00005F70 little-endian: 70 5F 00 00
    hits = []
    buf = data[traw:traw+trsz]
    for pat, tag in [(b"\x70\x5f\x00\x00", "+0x5F70"), (b"\x74\x5f\x00\x00", "+0x5F74")]:
        i = 0
        while True:
            j = buf.find(pat, i)
            if j < 0: break
            va = 0x180000000 + tva + j - 3  # disp starts ~3 bytes into the insn
            hits.append((va, tag))
            i = j + 1
    L.append("disp32 scan hits: %d" % len(hits))
    for va, tag in sorted(set(hits)):
        L.append("  candidate 0x%X %s" % (va, tag))
    L.append("")
    # disassemble around the densest cluster
    for va, tag in sorted(set(hits))[:24]:
        start = va - 0x40
        end = va + 0x80
        rva = start - ib
        off = traw + (rva - tva)
        if off < traw or off >= traw+trsz: continue
        L.append("-- context 0x%X..0x%X (hit %s)" % (start, end, tag))
        for ins in md.disasm(data[off:off+(end-start)], start):
            mk = ""
            if "0x5f70" in ins.op_str or "0x5f74" in ins.op_str:
                mk = "   <== TRACKER DOMAIN WRITE"
            L.append("  %08X  %-9s %-46s%s" % (ins.address, ins.mnemonic, ins.op_str, mk))
        L.append("")
    open(OUT, "w").write("\n".join(L) + "\n")
    print("wrote", OUT)

if __name__ == "__main__":
    main()
