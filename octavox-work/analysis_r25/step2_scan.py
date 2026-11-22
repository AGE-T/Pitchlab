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

INTERESTING = {0x180351F50:"pow",0x180352DC0:"logf",0x180416360:"PANTABLE",0x180412E30:"pi/4?",0x180414560:"pi/4?",0x180414638:"pi/4?",0x1804146B0:"pi/4?",0x1804152D8:"pi/4?"}

targets = [0x1800d9e80,0x1800c5730,0x1800c5740,0x1800c5750,0x1800c5770,0x1800c5790,0x1800c57b0,0x1804362a0,0x1800ce090,0x1804357c8,0x1800d4ac0]

def sec_of(va):
    rva = va-ib
    for name,sva,vsize,raw,rsize in secs:
        if sva<=rva<sva+vsize: return name
    return None

lines=[]
for t in targets:
    s = sec_of(t)
    if s not in (".text",):
        lines.append(f"### target {t:#x} section={s} (not .text, skip)")
        continue
    off = va2off(secs,t,ib)
    code = data[off:off+0x400]
    ops = md.disasm(code, t)
    mn={}
    riprefs=[]; calls=[]; imms=[]
    n=0
    for ins in ops:
        n+=1
        mn[ins.mnemonic]=mn.get(ins.mnemonic,0)+1
        if ins.mnemonic in ("call","jmp") and ins.operands and ins.operands[0].type==capstone.x86.X86_OP_IMM:
            tgt=ins.operands[0].imm
            calls.append((ins.address,tgt))
            if tgt in INTERESTING: riprefs.append(f"CALL {tgt:#x} ({INTERESTING[tgt]}) @ {ins.address:#x}")
        # rip-relative mem operands
        if ins.operands:
            for op in ins.operands:
                if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                    ea = ins.address+ins.size+op.mem.disp
                    tag = INTERESTING.get(ea)
                    riprefs.append(f"RIP {ea:#x}{' ('+tag+')' if tag else ''} @ {ins.address:#x} {ins.mnemonic} {ins.op_str}")
        if ins.mnemonic=="mov" and "rip" not in ins.op_str and ins.operands and ins.operands[-1].type==capstone.x86.X86_OP_IMM:
            v=ins.operands[-1].imm
            if v in INTERESTABLE if False else ():
                pass
    floatops = {k:v for k,v in mn.items() if k.endswith("ss") or k.endswith("sd") or k in ("divps","mulps","addps","subps")}
    lines.append(f"### target {t:#x} sec={s} insn_count={n}")
    lines.append(f"  float ops: {floatops}")
    lines.append(f"  notables: {[k for k in mn if k.startswith(('sqrt','rcp','cvts'))]}")
    for r in riprefs[:40]:
        lines.append("  "+r)
    lines.append(f"  calls({len(calls)}): "+", ".join(f"{a:#x}->{g:#x}" for a,g in calls[:30]))
    lines.append("")

print("\n".join(lines))
