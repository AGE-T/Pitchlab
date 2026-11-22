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

def disasm(va, length, label):
    off=va2off(secs,va,ib)
    lines=[f"===== {label} {va:#x} ({length:#x} bytes) ====="]
    for ins in md.disasm(data[off:off+length], va):
        lines.append(f"{ins.address:#x}: {ins.mnemonic:<8} {ins.op_str}")
    return lines

out=[]
# STEP4: worker tree calls
out+=disasm(0x180103a10, 0x2f0, "FUN_180103a10 worker")
open("/home/z/my-project/octavox-work/analysis_r25/step4_worker.txt","w").write("\n".join(out))
print("worker lines:", len(out))

out2=[]
out2+=disasm(0x1800d7400, 0x300, "ctor FUN_1800d7400")
open("/home/z/my-project/octavox-work/analysis_r25/step4_ctor.txt","w").write("\n".join(out2))
print("ctor lines:", len(out2))
