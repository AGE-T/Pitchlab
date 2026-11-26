"""R26 static-analysis library: self-contained PE x64 + capstone helpers for Ox.vst3.

Target: upload/Ox.vst3  SHA-256 7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2
All analysis in this round is STATIC (the binary is never executed).
"""
import struct, hashlib, sys

TARGET = '/home/z/my-project/upload/Ox.vst3'
EXPECT_SHA256 = '7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2'

class PE:
    def __init__(self, path=TARGET):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        assert self.data[:2] == b'MZ', 'not a PE'
        pe_off = struct.unpack_from('<I', self.data, 0x3C)[0]
        assert self.data[pe_off:pe_off+4] == b'PE\0\0'
        coff = pe_off + 4
        machine, nsec, _, _, _, opt_size, _ = struct.unpack_from('<HHIIIHH', self.data, coff)
        assert machine == 0x8664, 'not x64'
        self.nsec = nsec
        opt = coff + 20
        magic = struct.unpack_from('<H', self.data, opt)[0]
        assert magic == 0x20b, 'not PE32+'
        self.image_base = struct.unpack_from('<Q', self.data, opt + 24)[0]
        self.sec_align = struct.unpack_from('<I', self.data, opt + 32)[0]
        sec = opt + opt_size
        self.sections = []
        for i in range(nsec):
            off = sec + 40 * i
            name = self.data[off:off+8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, rsize, ro = struct.unpack_from('<IIII', self.data, off + 8)
            self.sections.append({'name': name, 'vsize': vsize, 'va': va, 'rsize': rsize, 'ro': ro})
    def va2fo(self, va):
        rva = va - self.image_base
        for s in self.sections:
            if s['va'] <= rva < s['va'] + max(s['vsize'], s['rsize']):
                d = rva - s['va']
                if d < s['rsize']:
                    return s['ro'] + d
        return None
    def fo2va(self, fo):
        for s in self.sections:
            if s['ro'] <= fo < s['ro'] + s['rsize']:
                return self.image_base + s['va'] + (fo - s['ro'])
        return None
    def sec_of(self, va):
        rva = va - self.image_base
        for s in self.sections:
            if s['va'] <= rva < s['va'] + s['vsize']:
                return s['name']
        return None
    def read(self, va, n):
        fo = self.va2fo(va)
        return None if fo is None else self.data[fo:fo+n]
    def read_cstr(self, va, maxlen=256):
        fo = self.va2fo(va)
        if fo is None: return None
        end = self.data.find(b'\0', fo)
        if end < 0 or end - fo > maxlen: return None
        try:
            return self.data[fo:end].decode('utf-8', 'replace')
        except Exception:
            return None
    def read_u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b else None
    def read_u64(self, va):
        b = self.read(va, 8)
        return struct.unpack('<Q', b)[0] if b else None
    def read_f32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<f', b)[0] if b else None
    def read_f64(self, va):
        b = self.read(va, 8)
        return struct.unpack('<d', b)[0] if b else None

def get_disassembler():
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    md.skipdata = True
    return md

def disasm_range(pe, md, va_lo, va_hi):
    """Yield capstone instructions for [va_lo, va_hi)."""
    fo = pe.va2fo(va_lo)
    if fo is None:
        return []
    code = pe.data[fo:fo + (va_hi - va_lo)]
    return list(md.disasm(code, va_lo))

def rip_target(insn):
    """Return RIP-relative target VA of an instruction, else None."""
    import capstone
    for op in insn.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            return insn.address + insn.size + op.mem.disp
    return None

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()
