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
# pattern: cmp dword ptr [rcx+d32], edx   -> 39 91 dd dd dd dd
#          mov dword ptr [rcx+d32], edx   -> 89 91 dd dd dd dd
blob=IMG[t0:t1]
hits=[]
i=0
pat=b"\x39\x91"
while True:
    i=blob.find(pat,i)
    if i<0: break
    d1=struct.unpack("<I",blob[i+2:i+6])[0]
    # look ahead 40 bytes for matching store with same disp
    win=blob[i:i+48]
    j=win.find(b"\x89\x91"+struct.pack("<I",d1))
    if j>0:
        hits.append((fo2va(t0+i), d1, j))
    i+=1
print("test-and-set hits:",len(hits))
for va,d,j in hits:
    print(hex(va),"disp=0x%x"%d,"store_at=+%d"%j)
