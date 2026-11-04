import json, struct
from pathlib import Path
A=Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE=json.loads((A/"pe_info.json").read_text(encoding="utf-8")); BASE=int(PE["image_base"],16)
IMG=Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()
def ai(v): return v if isinstance(v,int) else int(v,16)
SEC=[(ai(s["va"]),ai(s["rsize"]),ai(s["ptr"]),s["name"]) for s in PE["sections"]]
T=[s for s in SEC if s[3]==".text"][0]; RD=[s for s in SEC if s[3]==".rdata"][0]
t0,t1=T[2],T[2]+T[1]
def fo2va(fo):
    for sva,rs,ptr,n in SEC:
        if ptr<=fo<ptr+rs: return BASE+sva+(fo-ptr)
reg64={0:"rax",2:"rdx",1:"rcx",3:"rbx",6:"rsi",7:"rdi"}
hits=[]
for off in range(t0,t1-16):
    b=IMG[off:off+16]
    # mov r64,[rcx]  = 48 8B modrm(00 rrr 001)
    if b[0]==0x48 and b[1]==0x8B and (b[2]&0xC7)==0x01:
        r1=(b[2]>>3)&7; i=3; d1=0
    elif b[0]==0x48 and b[1]==0x8B and (b[2]&7)==1 and ((b[2]>>6)&3) in (1,2):
        r1=(b[2]>>3)&7; m=(b[2]>>6)&3
        if m==1: d1=struct.unpack("<b",b[3:4])[0]; i=4
        else: d1=struct.unpack("<i",b[3:7])[0]; i=7
    else: continue
    if r1 not in reg64: continue
    if b[i]!=0x8B: continue
    m2=b[i+1]
    if (m2&7)!=r1 or ((m2>>3)&7)!=0: continue
    m=(m2>>6)&3
    if m==1: d2=struct.unpack("<b",b[i+2:i+3])[0]; j=i+3
    elif m==2: d2=struct.unpack("<i",b[i+2:i+6])[0]; j=i+6
    else: continue
    if b[j]==0xC3:
        hits.append((fo2va(off),reg64[r1],d1,d2))
refs={}
rva0,size0,ptr0,_=RD
for off in range(ptr0,ptr0+size0-8,8):
    v=struct.unpack("<Q",IMG[off:off+8])[0]; refs.setdefault(v,[]).append(fo2va(off))
print("hits",len(hits))
for va,reg,d1,d2 in hits:
    print(("VT" if va in refs else "  "),hex(va),reg,"[rcx+0x%x]->[%s+0x%x]"%(d1&0xffffffff,reg,d2&0xffffffff),[hex(x) for x in refs.get(va,[])[:2]])
