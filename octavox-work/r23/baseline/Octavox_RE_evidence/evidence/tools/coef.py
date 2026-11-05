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
va=0x1800E1F80
fo=va2fo(va)
b=IMG[fo:fo+0x40]
print(b.hex(" "))
for k in range(0,0x40,4):
    print(hex(va+k), b[k:k+4].hex())
# decode the five stores
import re
for k in range(0,0x40-10):
    if b[k]==0xC7 and b[k+1]==0x86:
        disp=struct.unpack("<I",b[k+2:k+6])[0]
        imm=struct.unpack("<f",b[k+6:k+10])[0]
        print("store [rsi+0x%x] = %.9g  (0x%08x)"%(disp,imm,struct.unpack("<I",b[k+6:k+10])[0]))
