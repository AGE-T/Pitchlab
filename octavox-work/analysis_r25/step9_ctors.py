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

def dump_dis(va, length, label, fname):
    off=va2off(secs,va,ib)
    lines=[f"===== {label} {va:#x} ====="]
    code=data[off:off+length]
    pos=0
    while pos<len(code):
        got=False
        for ins in md.disasm(code[pos:], va+pos, count=80):
            got=True
            extra=""
            for op in ins.operands:
                if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
                    ea=ins.address+ins.size+op.mem.disp
                    o2=va2off(secs,ea,ib)
                    extra=f"  ; ->{ea:#x}"
                    if o2 is not None:
                        f=struct.unpack_from("<f",data,o2)[0]
                        d=struct.unpack_from("<d",data,o2)[0]
                        extra+=f" f32={f:.6g} f64={d:.6g}"
                        if 0x18037d000<=ea<0x1804a5000:
                            s=data[o2:o2+24].split(b"\0")[0]
                            if len(s)>2 and all(32<=c<127 for c in s[:16]):
                                extra+=f" str={s.decode()!r}"
            if ins.mnemonic=="call" and ins.operands and ins.operands[0].type==capstone.x86.X86_OP_IMM:
                extra=f"  ; -> {ins.operands[0].imm:#x}"
            lines.append(f"{ins.address:#x}: {ins.mnemonic:<9} {ins.op_str}{extra}")
            pos = ins.address-va+ins.size
        if not got: pos+=1
    open(fname,"w").write("\n".join(lines))
    return len(lines)

# generate float/double constant dumps near known regions
def const_report():
    out=[]
    for va in (0x180412850,0x1804120e0,0x180412328,0x18041213c,0x180412c90,0x180412810,0x180412858,0x1804123b0,0x180411e48,0x180411e58):
        off=va2off(secs,va,ib)
        d=struct.unpack_from("<d",data,off)[0]
        f=struct.unpack_from("<f",data,off)[0]
        out.append(f"{va:#x}: f64={d:.17g} f32={f:.9g}")
    return out

import sys
n1=dump_dis(0x1800d8fc0, 0x500, "FUN_1800d8fc0 (ctor2: pan/gain/sr/ms init)", "fun_d8fc0.txt")
n2=dump_dis(0x1800d7c40, 0x380, "FUN_1800d7c40 (ctor3: extension)", "fun_d7c40.txt")
print("d8fc0 lines:", n1, " d7c40 lines:", n2)
print("\n".join(const_report()))
