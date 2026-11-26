"""R26 S3: consumer side of the record ring + T+0x6058 cache origin."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256
import capstone

pe = PE()
md = get_disassembler()

def ann(insn):
    try:
        t = rip_target(insn)
    except Exception:
        t = None
    extra = ''
    if t is not None:
        b = pe.read(t, 8)
        if b:
            f64 = struct.unpack('<d', b)[0]
            f32 = struct.unpack('<f', b[:4])[0]
            s = pe.read_cstr(t, 60)
            if s and s.isprintable() and len(s) > 2:
                extra = '  ; "%s"' % s[:52]
            elif 1e-6 < abs(f64) < 1e6 and abs(f64-round(f64)) > 1e-9:
                extra = '  ; f64=%r' % f64
            else:
                extra = '  ; f32=%r' % f32
    print('0x%X  %-8s %-46s%s' % (insn.address, insn.mnemonic, insn.op_str, extra))

print('=== prepare record-consumer window 0x1800E2300..0x1800E24A0 ===')
for i in disasm_range(pe, md, 0x1800E2300, 0x1800E24A0):
    ann(i)
print()
print('=== prepare slot writes 0x1800E2680..0x1800E27B0 ===')
for i in disasm_range(pe, md, 0x1800E2680, 0x1800E27B0):
    ann(i)

print()
print('=== scan .text for [reg+0x6058] / [reg+0xA570] touches ===')
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
        for op in (ops or []):
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp in (0x6058, 0xA570, 0x605C, 0x6060):
                hits.append((insn.address, insn.mnemonic, insn.op_str))
                break
    base = end
for a, m, o in hits[:40]:
    print('  0x%X  %s %s' % (a, m, o))
print('total:', len(hits))
