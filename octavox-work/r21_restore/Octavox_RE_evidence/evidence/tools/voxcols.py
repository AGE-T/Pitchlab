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
def cstr(va,n=200):
    fo=va2fo(va); e=IMG.find(b"\0",fo,fo+n)
    return IMG[fo:e].decode("latin-1","replace")
sz={}
with open(A/"functions.csv",newline="",encoding="utf-8") as fh:
    for row in csv.DictReader(fh): sz[int(row["start_va"],16)]=int(row["size"])
# find all COLs whose type name contains VoxAudioProcessor
cols=[]
rva0,size0,ptr0,_=[s for s in SEC if s[3]==".rdata"][0]
for off in range(ptr0,ptr0+size0-0x14,4):
    sig,o,cd,ptd,pcd=struct.unpack("<IIIII",IMG[off:off+0x14])
    if sig!=1 or o>0x2000 or cd>0x2000: continue
    if not(0<ptd<0x600000 and 0<pcd<0x600000): continue
    nm=None
    for k in (0x10,8,0):
        try: c=cstr(BASE+ptd+k,200)
        except Exception: continue
        if c.startswith(".?A"): nm=c; break
    if nm and "VoxAudioProcessor" in nm:
        cols.append((fo2va(off),o,cd,ptd,pcd))
for c in cols: print("COL",hex(c[0]),"off",c[1],"cd",c[2],"td",hex(c[3]))
# vtable headers: qword equal to COL va
for col in [c[0] for c in cols]:
    needle=struct.pack("<Q",col)
    i=0; found=[]
    while True:
        i=IMG.find(needle,i)
        if i<0: break
        v=fo2va(i); i+=1
        if v: found.append(v)
    print("COL",hex(col),"qword refs:",[hex(x) for x in found])
    # also dump at col+0x18
    fo=va2fo(col+0x18)
    if fo:
        vals=[struct.unpack("<Q",IMG[fo+k*8:fo+k*8+8])[0] for k in range(8)]
        print("   col+0x18:",[hex(v) for v in vals])
