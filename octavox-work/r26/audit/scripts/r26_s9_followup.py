"""R26 S9 follow-up: investigate the NEW xref findings + harmony table region refs."""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256
import capstone

OUT = '/home/z/my-project/octavox-work/r26/audit/evidence'
pe = PE()
assert pe.sha256 == EXPECT_SHA256
md = get_disassembler()

def annotate(insn):
    try:
        t = rip_target(insn)
    except capstone.CsError:
        t = None
    extra = ''
    if t is not None:
        s = pe.read_cstr(t, 96)
        if s and s.isprintable() and len(s) > 2:
            extra = '  ; "%s"' % s[:80]
        else:
            b4 = pe.read(t, 8)
            if b4:
                f = struct.unpack('<d', b4)[0]
                f32 = struct.unpack('<f', b4[:4])[0]
                q = struct.unpack('<Q', b4)[0]
                if 1e-8 < abs(f) < 1e8 and abs(f - round(f)) > 1e-12:
                    extra = '  ; f64=%r f32=%r q=0x%X' % (f, f32, q)
                else:
                    extra = '  ; q=0x%X f32=%r' % (q, f32)
    return '0x%X  %-8s %-45s%s' % (insn.address, insn.mnemonic, insn.op_str, extra)

# ---- (a) who calls the dead serializer? context around 0x180347857 ----
print('=== context around 0x180347857 (caller of FUN_180310F20) ===')
for insn in disasm_range(pe, md, 0x180347810, 0x1803478a0):
    print(annotate(insn))

# find the enclosing function via pdata
fo = pe.va2fo(0x1804CD000)
funcs = []
p = fo
while p + 12 <= fo + 0x26800:
    start, end, _ = struct.unpack_from('<III', pe.data, p)
    if start == 0 and end == 0: break
    funcs.append((0x180000000 + start, 0x180000000 + end))
    p += 12
for s, e in funcs:
    if s <= 0x180347857 < e:
        print('caller function range: 0x%X .. 0x%X (%d bytes)' % (s, e, e - s))
        CALLER = (s, e)
        break

# ---- (b) kernel table 3rd ref at 0x18032b720 ----
print()
print('=== context around 0x18032b720 (3rd kernel-table ref) ===')
for insn in disasm_range(pe, md, 0x18032b6c0, 0x18032b790):
    print(annotate(insn))
for s, e in funcs:
    if s <= 0x18032b720 < e:
        print('enclosing function range: 0x%X .. 0x%X (%d bytes)' % (s, e, e - s))
        break

# ---- (c) harmony region: all rip-targets into 0x1803BB600..0x1803BB800 ----
print()
print('=== rip refs into harmony table region 0x1803BB600..0x1803BB800 ===')
TEXT_LO, TEXT_HI = 0x180001000, 0x18037D000
base = TEXT_LO
hits = []
while base < TEXT_HI:
    end = min(base + 0x40000, TEXT_HI)
    chunk = pe.data[pe.va2fo(base):pe.va2fo(end)]
    for insn in md.disasm(chunk, base):
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        t = rip_target(insn)
        if t is not None and 0x1803BB580 <= t < 0x1803BBD00:
            hits.append((insn.address, t, insn.mnemonic))
    base = end
for a, t, m in hits:
    print('  0x%X -> 0x%X (%s)' % (a, t, m))
print('total harmony-region refs:', len(hits))

# ---- (d) stored pointers to profile selector FUN_18030C6A0 ----
print()
print('=== stored pointers to 0x18030C6A0 / 0x180310F20 / 0x1800E1520 ===')
for want in [0x18030C6A0, 0x180310F20, 0x180347857]:
    pat = struct.pack('<Q', want)
    hits2 = []
    for s in pe.sections:
        if s['name'] in ('.data', '.rdata', '_RDATA', '.pdata', '.reloc'):
            blob = pe.data[s['ro']:s['ro'] + s['rsize']]
            off = 0
            while True:
                i = blob.find(pat, off)
                if i < 0: break
                hits2.append((s['name'], hex(pe.image_base + s['va'] + i)))
                off = i + 1
    print('  0x%X stored at: %s' % (want, hits2[:8]))
