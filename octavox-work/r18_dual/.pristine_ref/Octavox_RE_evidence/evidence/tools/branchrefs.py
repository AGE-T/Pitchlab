"""Find E8/E9 rel32 branches to a given VA in .text."""
import json
import struct
import sys

A = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis"
PE = json.load(open(A + r"\pe_info.json", encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = open(r"F:\VST\OV_DIS\Ox.vst3", "rb").read()
text = next(s for s in PE["sections"] if s["name"] == ".text")
tva = int(text["va"], 16) if isinstance(text["va"], str) else text["va"]
tptr = int(text["ptr"], 16) if isinstance(text["ptr"], str) else text["ptr"]
tsize = int(text["rsize"], 16) if isinstance(text["rsize"], str) else text["rsize"]
blob = IMG[tptr:tptr + tsize]

for arg in sys.argv[1:]:
    want = int(arg, 16)
    print("== E8/E9 -> %X ==" % want)
    n = 0
    for i in range(len(blob) - 5):
        if blob[i] not in (0xE8, 0xE9):
            continue
        rel = struct.unpack("<i", blob[i + 1:i + 5])[0]
        va = BASE + tva + i
        if va + 5 + rel == want:
            n += 1
            print("  %s at 0x%X" % ("call" if blob[i] == 0xE8 else "jmp", va))
    print("  total %d" % n)
