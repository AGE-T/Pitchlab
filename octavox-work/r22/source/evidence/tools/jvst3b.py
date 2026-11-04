import json, struct, csv
from pathlib import Path
A=Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE=json.loads((A/"pe_info.json").read_text(encoding="utf-8")); BASE=int(PE["image_base"],16)
IMG=Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()
def ai(v): return v if isinstance(v,int) else int(v,16)
SEC=[(ai(s["va"]),ai(s["rsize"]),ai(s["ptr"]),s["name"]) for s in PE["sections"]]
T=[s for s in SEC if s[3]==".text"][0]; lo,hi=BASE+T[0],BASE+T[0]+T[1]
def va2fo(va):
    r=va-BASE
    for sva,rs,ptr,n in SEC:
        if sva<=r<sva+rs: return ptr+(r-sva)
sz={}
with open(A/"functions.csv",newline="",encoding="utf-8") as fh:
    for row in csv.DictReader(fh): sz[int(row["start_va"],16)]=int(row["size"])
vt=0x1803EF8A0
fo=va2fo(vt)
for k in range(34):
    v=struct.unpack("<Q",IMG[fo+k*8:fo+k*8+8])[0]
    print("[%2d] %s %s %d"%(k,hex(v),"CODE" if lo<=v<hi else "----",sz.get(v,0)))
