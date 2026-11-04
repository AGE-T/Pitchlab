"""Decode FUN_1800E10B0 jump table at 0x1800E14D0 (18 entries, offsets from 0x180000000)."""
import json
import struct

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


TBL = 0x1800E14D0
BASE_CODE = 0x180000000
fo = va2fo(TBL)
print("table VA %X  file off %X  section rdata" % (TBL, fo))
for i in range(18):
    raw = IMG[fo + 4 * i: fo + 4 * i + 4]
    v = struct.unpack("<i", raw)[0]
    tgt = BASE_CODE + v
    print("idx %2d (0x%02X)  entry=0x%08X  -> 0x%X" % (i, i, v & 0xFFFFFFFF, tgt))
