"""Step 10: scan .text for pan-law math: sqrtss/sqrtsd, sin/cos calls, refs to pi/4 consts.
Also map referenced functions via .pdata bounds."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
text = [s for s in b.secs if s[0] == ".text"][0]
TEXT_RAW, TEXT_SIZE = text[3], text[4]
TEXT_VA = IB + text[1]

# .pdata bounds map
pd = [s for s in b.secs if s[0] == ".pdata"][0]
FUNCS = []
n = pd[4] // 12
for i in range(n):
    off = pd[3] + 12 * i
    s_rva, e_rva = struct.unpack_from("<II", b.data, off)
    if s_rva:
        FUNCS.append((IB + s_rva, IB + e_rva))
FUNCS.sort()


def fn_of(va):
    import bisect
    i = bisect.bisect_right(FUNCS, (va, 0xFFFFFFFFFFFFFFF)) - 1
    if i >= 0 and FUNCS[i][0] <= va < FUNCS[i][1]:
        return FUNCS[i]
    return None


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
data = b.data

print("== A) sqrtss / sqrtsd instruction scan (with owning function) ==")
hits = 0
for off in range(TEXT_RAW, TEXT_RAW + TEXT_SIZE - 5):
    w = data[off:off + 4]
    if w[0] in (0xF3, 0xF2) and w[1] == 0x0F and w[2] == 0x51:
        va = b.off2va(off)
        f = fn_of(va)
        hits += 1
        if hits <= 80:
            print(f"  {va:#x} sqrt{'sd' if w[0]==0xF2 else 'ss'} in {f[0]:#x}..{f[1]:#x}" if f else f"  {va:#x} (no fn)")
print(f"  total sqrt hits: {hits}")

print("\n== B) references to pi/4-family double constants ==")
PI4 = 0.7853981633974483
targets = {}
for va in (0x180412E30, 0x180414560, 0x180414638, 0x1804146B0, 0x1804152D8):
    d = b.f64(va)
    print(f"  const {va:#x} = {d!r}")
    targets[va] = d

# scan for rip-relative refs to these VAs: walk all instructions is too slow; scan for 8B/4B patterns
# approach: find all "movsd xmm, [rip+disp]" (F2 0F 10) etc. via quick byte scan + decode
print("\n  scanning .text for rip-relative refs to those constants...")
ref_hits = []
i = TEXT_RAW
end = TEXT_RAW + TEXT_SIZE
while i < end - 8:
    # opcode patterns with rip-relative memory: modrm with mod=00 rm=101
    # quick check: any 0x05 modrm byte preceded by opcode... too broad; use capstone on windows
    # instead scan common SSE loads: F2/F3 0F 10/11 with modrm 05
    b0 = data[i]
    if b0 in (0xF2, 0xF3):
        if data[i + 1] == 0x0F and data[i + 2] in (0x10, 0x11, 0x2A, 0x5C, 0x59, 0x5E, 0x5A, 0x51, 0x58, 0x5D, 0xC2) and (data[i + 3] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", data, i + 4)[0]
            va = b.off2va(i)
            tgt = va + (7 if data[i+3] == 0x05 else 7) + disp - 7  # length varies; compute below
            # compute actual length via decode
            insns = list(md.disasm(data[i:i + 16], va, count=1))
            if insns:
                ins = insns[0]
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in targets:
                            f = fn_of(va)
                            ref_hits.append((va, tgt, f))
    i += 1
for va, tgt, f in ref_hits[:60]:
    fs = f"{f[0]:#x}..{f[1]:#x}" if f else "?"
    print(f"  {va:#x} refs {tgt:#x} = {targets[tgt]!r} in fn {fs}")
print(f"  total: {len(ref_hits)}")
