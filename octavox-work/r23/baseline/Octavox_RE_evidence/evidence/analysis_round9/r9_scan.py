"""Round-9 read-only scanner for the Octavox target (Linux port of dis_range/find_disp_refs).

Reads the ORIGINAL image bytes in place (never writes to the target).
Two modes:
  scan  <disp32> [<disp32> ...]   exhaustive .text scan for modrm disp32 uses
  range <start> <end> [out.txt]   capstone disassembly of a VA range

Outputs go to /home/z/octavox/analysis/ (never into the original tree).
"""
import json
import struct
import sys
from pathlib import Path

import capstone

TARGET = Path("/home/z/octavox/original/Ox.vst3")
OUTDIR = Path("/home/z/octavox/analysis")


def load_pe():
    data = TARGET.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
    machine, nsec = struct.unpack_from("<HH", data, e_lfanew + 4)
    opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt_off = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt_off)[0]
    assert magic == 0x20B, hex(magic)
    image_base = struct.unpack_from("<Q", data, opt_off + 24)[0]
    sec_off = opt_off + opt_size
    secs = []
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode()
        vsize, va, rsize, ptr = struct.unpack_from("<IIII", data, o + 8)
        secs.append(dict(name=name, va=va, vsize=vsize, rsize=rsize, ptr=ptr))
    return data, image_base, secs


DATA, BASE, SECS = load_pe()
TEXT = next(s for s in SECS if s["name"] == ".text")
TBLOB = DATA[TEXT["ptr"]:TEXT["ptr"] + TEXT["rsize"]]
TVA = BASE + TEXT["va"]

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def va2fo(va):
    r = va - BASE
    for s in SECS:
        if s["va"] <= r < s["va"] + s["rsize"]:
            return s["ptr"] + (r - s["va"])
    return None


def disasm_at(va, n=16):
    """Disassemble up to n instructions starting at va."""
    fo = va2fo(va)
    if fo is None:
        return []
    out = []
    for ins in MD.disasm(DATA[fo:fo + 128], va):
        out.append(ins)
        if len(out) >= n:
            break
    return out


def scan_disp(disp):
    """Every occurrence of the 4-byte LE displacement in .text, decoded in context."""
    needle = struct.pack("<I", disp)
    hits = []
    start = 0
    while True:
        i = TBLOB.find(needle, start)
        if i < 0:
            break
        start = i + 1
        va = TVA + i
        # try to find an instruction whose memory operand uses this disp:
        # disassemble a window ending at the disp (disp is the last 4 bytes of
        # the instruction when modrm disp32 is the trailing operand)
        ctx = None
        for back in range(1, 12):
            begin = i - back
            if begin < 0:
                break
            try:
                insns = list(MD.disasm(TBLOB[begin:i + 4], TVA + begin, count=1))
            except Exception:
                continue
            if insns and insns[0].size == back + 4:
                ins = insns[0]
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.disp == disp:
                        ctx = ins
                        break
                if ctx is not None:
                    break
        if ctx is not None:
            access = ctx.op_str
            kind = ctx.mnemonic
        else:
            access = "(imm32 operand, not a decoded disp)"
            kind = "raw"
        hits.append((va, kind, access))
    return hits


def fmt_range(start, end):
    fo = va2fo(start)
    fo2 = va2fo(end)
    code = DATA[fo:fo2]
    lines = []
    for ins in MD.disasm(code, start):
        ops = []
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM:
                ops.append(f"[base={op.mem.base} idx={op.mem.index} scale={op.mem.scale} disp={op.mem.disp:#x}]")
            elif op.type == capstone.x86.X86_OP_IMM:
                ops.append(f"imm={op.imm:#x}")
            else:
                ops.append(f"reg={op.reg}")
        lines.append("%016X  %-26s %-8s %-40s %s" % (
            ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str, "; ".join(ops)))
    return lines


def main():
    mode = sys.argv[1]
    if mode == "scan":
        out = []
        for arg in sys.argv[2:]:
            disp = int(arg, 16)
            hits = scan_disp(disp)
            out.append(f"== displacement {disp:#x}: {len(hits)} decoded uses ==")
            for va, kind, access in hits:
                out.append(f"  {va:#012x}  {kind:10s}  {access}")
        txt = "\n".join(out) + "\n"
        path = OUTDIR / "r9_disp_scan.txt"
        path.write_text(txt, encoding="utf-8")
        print(txt)
        print("wrote", path)
    elif mode == "range":
        start = int(sys.argv[2], 16)
        end = int(sys.argv[3], 16)
        out = sys.argv[4] if len(sys.argv) > 4 else "r9_range.txt"
        lines = fmt_range(start, end)
        (OUTDIR / out).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print("\n".join(lines))
        print("wrote", OUTDIR / out, len(lines), "instructions")
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
