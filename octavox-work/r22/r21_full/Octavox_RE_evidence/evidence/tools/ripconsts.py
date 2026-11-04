"""Resolve RIP-relative memory operands in a VA range and print the constants."""
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
md.detail = True

start = int(sys.argv[1], 16)
end = int(sys.argv[2], 16)
code = IMG[va2fo(start):va2fo(end)]
for ins in md.disasm(code, start):
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            tgt = ins.address + ins.size + op.mem.disp
            fo = va2fo(tgt)
            if fo is None:
                print("%X  %s %s -> %X (no section)" % (ins.address, ins.mnemonic, ins.op_str, tgt))
                continue
            raw = IMG[fo:fo + 8]
            d = struct.unpack("<d", raw)[0] if len(raw) == 8 else float("nan")
            f = struct.unpack("<f", raw[:4])[0]
            print("%X  %-8s %-28s -> %X  bytes=%s  f32=%.9g  f64=%.17g" %
                  (ins.address, ins.mnemonic, ins.op_str, tgt, raw.hex(" "), f, d))
