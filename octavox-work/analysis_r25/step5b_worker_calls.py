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

START=0x180103a10; END=0x18010523a
off=va2off(secs,START,ib)
code=data[off:off+(END-START)]
insns=list(md.disasm(code, START))
print("total insns", len(insns))
lines=[]
idx={a:i for i,a in enumerate([x.address for x in insns])}
for i,ins in enumerate(insns):
    if ins.mnemonic=="call":
        tgt=None
        if ins.operands and ins.operands[0].type==capstone.x86.X86_OP_IMM:
            tgt=ins.operands[0].imm
        # context: previous 6 instructions
        ctx=[]
        for j in range(max(0,i-6), i):
            c=insns[j]
            ctx.append(f"{c.mnemonic} {c.op_str}")
        lines.append(f"--- CALL at {ins.address:#x} -> {tgt:#x}" if tgt else f"--- indirect CALL at {ins.address:#x} {ins.op_str}")
        lines.extend("    "+c for c in ctx)
        lines.append(f">>> {ins.mnemonic} {ins.op_str}")
        # next 2
        for j in range(i+1, min(len(insns), i+3)):
            lines.append("    "+f"{insns[j].mnemonic} {insns[j].op_str}")
open("/home/z/my-project/octavox-work/analysis_r25/worker_calls.txt","w").write("\n".join(lines))
print("calls found:", sum(1 for l in lines if l.startswith("---")))
# count of indirect calls
ind=[l for l in lines if "indirect" in l]
print("indirect:", len(ind))
