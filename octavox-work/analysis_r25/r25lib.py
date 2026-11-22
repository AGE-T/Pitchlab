"""r25 shared PE/disasm toolkit (read-only)."""
import struct
import capstone

PATH = "/home/z/my-project/octavox-work/original/Ox.vst3"
IB = 0x180000000


def load_pe(path=PATH):
    data = open(path, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff)[0]
    opt_sz = struct.unpack_from("<H", data, e + 20)[0]
    ib = struct.unpack_from("<Q", data, coff + 20 + 24)[0]
    so = e + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = so + 40 * i
        name = data[o:o+8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, ib, secs


class Bin:
    def __init__(self, path=PATH):
        self.data, self.ib, self.secs = load_pe(path)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def va2off(self, va):
        rva = va - self.ib
        for name, sva, vsize, raw, rsize in self.secs:
            if sva <= rva < sva + vsize:
                return raw + (rva - sva)
        return None

    def off2va(self, off):
        for name, sva, vsize, raw, rsize in self.secs:
            if raw <= off < raw + rsize:
                return self.ib + sva + (off - raw)
        return None

    def sec_of(self, va):
        rva = va - self.ib
        for name, sva, vsize, raw, rsize in self.secs:
            if sva <= rva < sva + vsize:
                return name
        return None

    def read(self, va, n):
        o = self.va2off(va)
        if o is None:
            return None
        return self.data[o:o+n]

    def u64(self, va):
        b = self.read(va, 8)
        return struct.unpack("<Q", b)[0] if b else None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if b else None

    def f32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<f", b)[0] if b else None

    def f64(self, va):
        b = self.read(va, 8)
        return struct.unpack("<d", b)[0] if b else None

    def disasm(self, va, size):
        """Return list of capstone instructions for VA range."""
        b = self.read(va, size)
        return list(self.md.disasm(b, va))

    def disasm_until(self, va, max_instr=4000, stop_at_ret=False):
        out = []
        cur = va
        for _ in range(max_instr):
            b = self.read(cur, 16)
            if not b:
                break
            insns = list(self.md.disasm(b[:16], cur, count=1))
            if not insns:
                break
            i = insns[0]
            out.append(i)
            cur += i.size
            if stop_at_ret and i.mnemonic in ("ret",) :
                break
        return out

    def fmt(self, insns, resolve=True):
        lines = []
        for i in insns:
            op = i.op_str
            # annotate rip-relative targets and immediates that look like VAs
            note = ""
            if resolve:
                for tok in self._rip_targets(i):
                    note += f"  ; -> {tok}"
                imm = None
                if len(i.operands) == 1 and i.operands[0].type == capstone.x86.X86_OP_IMM:
                    imm = i.operands[0].imm
                elif "ptr" in i.op_str:
                    pass
                if imm is not None and IB <= imm < IB + 0x2000000:
                    sec = self.sec_of(imm)
                    if sec:
                        note += f"  ; imm VA {imm:#x} ({sec})"
            lines.append(f"{i.address:#x}: {i.bytes.hex():<24} {i.mnemonic} {op}{note}")
        return "\n".join(lines)

    def _rip_targets(self, i):
        toks = []
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                tgt = i.address + i.size + op.mem.disp
                toks.append(f"{tgt:#x}")
        return toks

    def cstr(self, va, maxlen=64):
        o = self.va2off(va)
        if o is None:
            return None
        end = self.data.find(b"\0", o, o + maxlen)
        if end < 0:
            return None
        return self.data[o:end].decode("ascii", "replace")
