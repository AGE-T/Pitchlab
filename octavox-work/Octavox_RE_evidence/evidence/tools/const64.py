import json, struct
from pathlib import Path
A=Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE=json.loads((A/"pe_info.json").read_text(encoding="utf-8")); BASE=int(PE["image_base"],16)
IMG=Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()
def ai(v): return v if isinstance(v,int) else int(v,16)
SEC=[(ai(s["va"]),ai(s["rsize"]),ai(s["ptr"])) for s in PE["sections"]]
def va2fo(va):
    r=va-BASE
    for sva,rs,ptr in SEC:
        if sva<=r<sva+rs: return ptr+(r-sva)
for va in (0x180412480,0x180412488,0x180412490,0x180412498,0x1804124a0,0x1804124a8):
    b=IMG[va2fo(va):va2fo(va)+8]
    print(hex(va), b.hex(" "), "f64=%.12g"%struct.unpack("<d",b)[0], "f32lo=%.9g"%struct.unpack("<f",b[:4])[0])
# find float 0.015625 / double 1/64 in rdata
rd=[s for s in SEC if s[0]==0x37D000][0]
