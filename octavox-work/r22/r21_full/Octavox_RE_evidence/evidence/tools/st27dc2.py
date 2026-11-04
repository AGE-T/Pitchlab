import json, struct
from pathlib import Path
A=Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE=json.loads((A/"pe_info.json").read_text(encoding="utf-8")); BASE=int(PE["image_base"],16)
IMG=Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()
def ai(v): return v if isinstance(v,int) else int(v,16)
SEC=[(ai(s["va"]),ai(s["rsize"]),ai(s["ptr"]),s["name"]) for s in PE["sections"]]
T=[s for s in SEC if s[3]==".text"][0]
t0,t1=T[2],T[2]+T[1]
def fo2va(fo):
    for sva,rs,ptr,n in SEC:
        if ptr<=fo<ptr+rs: return BASE+sva+(fo-ptr)
D=b"\xdc\x27\x00\x00"
i=t0; hits=[]
while True:
    i=IMG.find(D,i,t1)
    if i<0: break
    op=IMG[i-2]; modrm=IMG[i-1]
    hits.append((fo2va(i-2),op,modrm,IMG[i-2:i+8].hex(" ")))
    i+=1
from collections import Counter
print(Counter((h[1],h[2]) for h in hits))
for h in hits:
    if h[1] in (0x89,0xC7,0x88) and (h[2]>>6)==2:
        print("STORE",hex(h[0]),h[3])
