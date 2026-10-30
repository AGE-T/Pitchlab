"""Disassemble a VA range with capstone and write it to a text file (read-only)."""
import json
import struct
import sys

import capstone

A = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis"
PE = json.load(open(A + r"\pe_info.json", encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = open(r"F:\VST\OV_DIS\Ox.vst3", "rb").read()
SEC = [(int(s["va"], 16) if isinstance(s["va"], str) else s["va"],
        int(s["rsize"], 16) if isinstance(s["rsize"], str) else s["rsize"],
        int(s["ptr"], 16) if isinstance(s["ptr"], str) else s["ptr"], s["name"]) for s in PE["sections"]]


def va2fo(va):
    r = va - BASE
    for sva, rs, ptr, n in SEC:
        if sva <= r < sva + rs:
            return ptr + (r - sva)


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False

start = int(sys.argv[1], 16)
end = int(sys.argv[2], 16)
out = sys.argv[3] if len(sys.argv) > 3 else "dis_range.txt"
fo = va2fo(start)
code = IMG[fo:va2fo(end)]
lines = []
for ins in md.disasm(code, start):
    lines.append("%016X  %-24s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
print("wrote", out, len(lines), "instructions")
