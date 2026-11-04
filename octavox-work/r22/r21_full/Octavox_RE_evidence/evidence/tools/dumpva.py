"""Hex dump a VA range with dword annotations (base-relative targets)."""
import json
import struct
import sys

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
            return ptr + (r - sva), n


start = int(sys.argv[1], 16)
end = int(sys.argv[2], 16)
fo, sec = va2fo(start)
print("VA %X -> file %X (%s)" % (start, fo, sec))
va = start
while va < end:
    raw = IMG[va2fo(va)[0]:va2fo(va)[0] + 16]
    q = struct.unpack("<Q", raw[:8])[0]
    d0 = struct.unpack("<I", raw[:4])[0]
    d1 = struct.unpack("<I", raw[4:8])[0]
    print("%X  %s  q=0x%016X  d0=0x%08X->%X  d1=0x%08X->%X" %
          (va, raw.hex(" "), q, d0, BASE + d0, d1, BASE + d1))
    va += 16
