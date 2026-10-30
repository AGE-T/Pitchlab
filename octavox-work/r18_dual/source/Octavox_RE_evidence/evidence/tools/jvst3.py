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
def fo2va(fo):
    for sva,rs,ptr,n in SEC:
        if ptr<=fo<ptr+rs: return BASE+sva+(fo-ptr)
sz={}
with open(A/"functions.csv",newline="",encoding="utf-8") as fh:
    for row in csv.DictReader(fh): sz[int(row["start_va"],16)]=int(row["size"])
cols=[0x180417210,0x1804178e0,0x1804182a0,0x1804185c0,0x180419b48,0x180419d30]
for col in cols:
    b=IMG[va2fo(col):va2fo(col)+0x14]
    sig,off,cd,ptd,pcd=struct.unpack("<IIIII",b)
    needle=struct.pack("<Q",col); i=0; hdrs=[]
    while True:
        i=IMG.find(needle,i)
        if i<0: break
        v=fo2va(i); i+=1
        if v: hdrs.append(v)
    print("COL",hex(col),"off",off,"headers",[hex(h) for h in hdrs])
    for h in hdrs:
        fo=va2fo(h+8)
        vals=[struct.unpack("<Q",IMG[fo+k*8:fo+k*8+8])[0] for k in range(10)]
        print("   vt",hex(h+8),[ (hex(v),sz.get(v,0)) for v in vals])
