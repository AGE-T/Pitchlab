import struct, capstone

def load_pe(path):
    data = open(path,"rb").read()
    e = struct.unpack_from("<I",data,0x3C)[0]; coff=e+4
    nsec = struct.unpack_from("<H",data,coff+2)[0]
    opt_sz = struct.unpack_from("<H",data,e+20)[0]
    ib = struct.unpack_from("<Q",data,coff+20+24)[0]
    so = e+24+opt_sz; secs=[]
    for i in range(nsec):
        o = so+40*i
        name = data[o:o+8].rstrip(b"\0").decode("ascii","replace")
        vsize,va,rsize,raw = struct.unpack_from("<IIII",data,o+8)
        secs.append((name,va,vsize,raw,rsize))
    return data,ib,secs

def va2off(secs,va,ib):
    rva=va-ib
    for name,sva,vsize,raw,rsize in secs:
        if sva<=rva<sva+vsize: return raw+(rva-sva)
    return None

data,ib,secs = load_pe("/home/z/my-project/octavox-work/original/Ox.vst3")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

out=[]
# 1) strings at rdata pointers in table
for va in (0x1804362a0,0x1804357c8,0x180409bc8,0x180409f20):
    off=va2off(secs,va,ib)
    if off is None:
        out.append(f"{va:#x}: no section"); continue
    chunk=data[off:off+96]
    # print c-strings / hex
    txt=chunk.split(b"\0")[0]
    out.append(f"{va:#x}: str0={txt[:64]!r}")
    # dump qwords
    qs=[struct.unpack_from("<Q",chunk,8*i)[0] for i in range(6)]
    out.append(f"   qwords: {[hex(q) for q in qs]}")
    # floats
    fs=[struct.unpack_from("<f",chunk,4*i)[0] for i in range(8)]
    out.append(f"   floats: {fs}")

# pan table 0x180416360 - 16 floats
off=va2off(secs,0x180416360,ib)
fs=[struct.unpack_from("<f",data,off+4*i)[0] for i in range(16)]
out.append(f"PANTABLE 0x180416360 floats: {fs}")

print("\n".join(str(x) for x in out))
