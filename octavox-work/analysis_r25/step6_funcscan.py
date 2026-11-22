import struct, capstone, math

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

FUNCS = {
 0x18013d6f0:"graph-pre?", 0x180139b40:"A-special", 0x180139970:"A-fn1", 0x180139790:"A-fn2", 0x180139480:"A-fn3",
 0x180138e70:"B-fn1", 0x180138b00:"B-fn2", 0x180138870:"B-fn3", 0x1800d0d50:"B-fn4", 0x1800ee420:"B-fn5",
 0x1801383d0:"B-fn6", 0x180138170:"B-fn7", 0x180137f70:"B-fn8", 0x180137e90:"B-fn9", 0x1800e2030:"engine-prepare",
 0x1800e8b50:"C-fn1", 0x1800ea650:"C-fn2", 0x1800d38b0:"C-fn3",
 0x1800d7f10:"M-fn1", 0x1800cec80:"M-fn2", 0x1800dc430:"M-fn3", 0x1801370b0:"M-fn4", 0x180136ee0:"M-fn5",
}

CRT = lambda t: 0x180350000 <= t < 0x180360000

lines=[]
for va,name in sorted(FUNCS.items()):
    off=va2off(secs,va,ib)
    if off is None: continue
    code=data[off:off+0x700]
    insns=list(md.disasm(code, va))
    fl={}
    consts=[]; calls=[]; loops=0
    for ins in insns:
        m=ins.mnemonic
        if m.endswith(("ss","sd")) or m.endswith(("ps","pd")):
            fl[m]=fl.get(m,0)+1
        if m in ("loop","jne","jb","ja","jl","jg") : pass
        if m=="call" and ins.operands and ins.operands[0].type==capstone.x86.X86_OP_IMM:
            t=ins.operands[0].imm
            calls.append(t)
        for op in ins.operands:
            if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                ea=ins.address+ins.size+op.mem.disp
                o2=va2off(secs,ea,ib)
                if o2 is not None and ins.mnemonic in ("movss","mulss","addss","subss","divss","movsd","mulsd","addsd","subsd","divsd","comiss","comisd","ucomiss","xorps","andps","maxss","minss","cvtsi2ss","sqrtss"):
                    f=struct.unpack_from("<f",data,o2)[0]
                    d=struct.unpack_from("<d",data,o2)[0]
                    note=""
                    for label,v in (("(pi/4)",math.pi/4),("(1/sqrt2)",1/math.sqrt(2)),("(0.5)",0.5),("(2)",2.0),("(1)",1.0),("(0)",0.0),("(pi/2)",math.pi/2),("(pi)",math.pi),("(3)",3.0),("(10)",10.0),("(1e-? small)",None)):
                        pass
                    consts.append((hex(ea), f, d, m))
    lines.append(f"### {name} {va:#x}: n={len(insns)} floats={ {k:v for k,v in fl.items() if k not in ('nopss','nop',)} }")
    if consts:
        uniq={}
        for c in consts:
            k=(c[0],c[1],c[2])
            uniq[k]=uniq.get(k,0)+1
        for (ea,f,d),cnt in sorted(uniq.items(), key=lambda kv:-kv[1])[:12]:
            lines.append(f"    const {ea} f32={f:.9g} f64={d:.9g} x{cnt}")
    crt=[c for c in calls if CRT(c)]
    lines.append(f"    calls={len(calls)} crt_math={[hex(c) for c in sorted(set(crt))]}")
    allc=", ".join(hex(c) for c in calls[:25])
    lines.append(f"    call list: {allc}")
    lines.append("")
out="\n".join(lines)
open("/home/z/my-project/octavox-work/analysis_r25/func_scan.txt","w").write(out)
print(out)
