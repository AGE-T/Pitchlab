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

def rd(va, n=8):
    off=va2off(secs,va,ib)
    return data[off:off+n] if off is not None else None

out=[]

def dis(va, length, label):
    off=va2off(secs,va,ib)
    lines=[f"===== {label} {va:#x} len={length:#x} ====="]
    for ins in md.disasm(data[off:off+length], va):
        extra=""
        for op in ins.operands:
            if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                ea = ins.address+ins.size+op.mem.disp
                o2=va2off(secs,ea,ib)
                extra=f"  ; ->{ea:#x}"
                if o2 is not None:
                    d=struct.unpack_from("<d",data,o2)[0]
                    f=struct.unpack_from("<f",data,o2)[0]
                    q=struct.unpack_from("<Q",data,o2)[0]
                    dd=struct.unpack_from("<d",data,o2)[0]
                    extra+=f" d={d:.6g} f32={f:.6g} q={q:#x} f64={dd:.6g}"
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==capstone.x86.X86_OP_IMM:
            extra=f"  ; -> {ins.operands[0].imm:#x}"
        lines.append(f"{ins.address:#x}: {ins.mnemonic:<9} {ins.op_str}{extra}")
    return lines

# scan .text for callers of these leaf functions
targets={0x1800d8230:"leafA",0x1800d7930:"leafB",0x1800d8230:"leafA"}
tname,tva,tvsize,traw,trsize = secs[0]
code = data[traw:traw+tvsize]; base=ib+tva
callers={t:[] for t in targets}
for i in range(len(code)-5):
    if code[i]==0xE8:
        rel=struct.unpack_from("<i",code,i+1)[0]
        tgt=base+i+5+rel
        if tgt in callers: callers[tgt].append(base+i)
for t,lst in callers.items():
    out.append(f"callers of {t:#x} ({targets[t]}): "+", ".join(hex(x) for x in lst))
open("/home/z/my-project/octavox-work/analysis_r25/step5_callers.txt","w").write("\n".join(out))
print("\n".join(out))

# dump leafA and leafB
la = dis(0x1800d8230, 0x100, "leafA 0x1800d8230")
lb = dis(0x1800d7930, 0x200, "leafB 0x1800d7930")
open("/home/z/my-project/octavox-work/analysis_r25/step5_leafA.txt","w").write("\n".join(la))
open("/home/z/my-project/octavox-work/analysis_r25/step5_leafB.txt","w").write("\n".join(lb))
print("leafA/leafB dumped")

# dump data constants referenced by ctor
for va in (0x1800d7458+0x3329cb, 0x1800d7488+0x330e6a, 0x1800d758d+0x330dab, 0x1800d75c8+0x330d70):
    off=va2off(secs,va,ib)
    if off is None:
        print(f"{va:#x} no off"); continue
    b=data[off:off+24]
    print(f"{va:#x}: str={b.split(b'\\0')[0][:24]!r} hex={b.hex()}")

# dump vtable at [ctor this] = 0x1800d745e (0x1800d744e lea rip 0x3329cb -> 0x1800d7455+7+0x3329cb)
vt = 0x1800d7455+7+0x3329cb
off=va2off(secs,vt,ib)
qs=[struct.unpack_from("<Q",data,off+8*i)[0] for i in range(24)]
print(f"vtable@{vt:#x}: "+", ".join(hex(q) for q in qs))
