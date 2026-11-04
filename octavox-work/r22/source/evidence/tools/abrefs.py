"""Find absolute 64-bit and 32-bit (base-relative) references to a VA anywhere in the image."""
import json
import struct
import sys

A = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis"
PE = json.load(open(A + r"\pe_info.json", encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = open(r"F:\VST\OV_DIS\Ox.vst3", "rb").read()


def fo2va(fo):
    for s in PE["sections"]:
        va = int(s["va"], 16) if isinstance(s["va"], str) else s["va"]
        ptr = int(s["ptr"], 16) if isinstance(s["ptr"], str) else s["ptr"]
        rs = int(s["rsize"], 16) if isinstance(s["rsize"], str) else s["rsize"]
        if ptr <= fo < ptr + rs:
            return BASE + va + (fo - ptr), s["name"]


for arg in sys.argv[1:]:
    want = int(arg, 16)
    print("== refs to %X ==" % want)
    q = struct.pack("<Q", want)
    d = struct.pack("<I", want - BASE)
    nq = nd = 0
    start = 0
    while True:
        i = IMG.find(q, start)
        if i < 0:
            break
        start = i + 1
        loc = fo2va(i)
        nq += 1
        print("  qword @ %s  (%X)" % (("%X" % loc[0]) if loc else "?", i))
    start = 0
    while True:
        i = IMG.find(d, start)
        if i < 0:
            break
        start = i + 1
        loc = fo2va(i)
        nd += 1
        print("  dword(base-rel) @ %s  (%X)" % (("%X" % loc[0]) if loc else "?", i))
    print("  total qword=%d dword=%d" % (nq, nd))
