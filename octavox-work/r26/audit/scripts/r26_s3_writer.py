"""R26 S3/S9: record-ring writer (FUN_1800E0190) + consumer sites in prepare.

Re-derives the 4-float record layout from raw disassembly: writer order,
consumer order, the dead-field status, and the page-slot coupling.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256

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
            q = struct.unpack('<Q', b)[0]
            f32 = struct.unpack('<f', b[:4])[0]
            f64 = struct.unpack('<d', b)[0]
            s = pe.read_cstr(t, 64)
            if s and s.isprintable() and len(s) > 2:
                extra = '  ; "%s"' % s[:56]
            elif 1e-6 < abs(f64) < 1e6 and abs(f64-round(f64)) > 1e-9:
                extra = '  ; f64=%r' % f64
            else:
                extra = '  ; q=0x%X f32=%r' % (q, f32)
    print('0x%X  %-8s %-46s%s' % (insn.address, insn.mnemonic, insn.op_str, extra))

print('=== record writer FUN_1800E0190 (full) ===')
for i in disasm_range(pe, md, 0x1800E0190, 0x1800E0340):
    ann(i)
