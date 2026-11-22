import struct, capstone, sys

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
print("image base", hex(ib))
for n,va,vsize,raw,rsize in secs:
    print("sec", n, hex(va+ib), hex(vsize), "raw", hex(raw), hex(rsize))

off = va2off(secs,0x180409E20,ib)
print("vtable off", hex(off))
out=[]
for i in range(20):
    q = struct.unpack_from("<Q",data,off+8*i)[0]
    out.append(f"slot {i:2d}: {q:#x}")
print("\n".join(out))
