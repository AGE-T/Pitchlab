"""R26 S7: pan-law hunt — stereomixer3 mix process disassembly + constant search."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256
import capstone

pe = PE()
assert pe.sha256 == EXPECT_SHA256
md = get_disassembler()

def ann(insn):
    try:
        t = rip_target(insn)
    except Exception:
        t = None
    extra = ''
    if t is not None:
        s = pe.read_cstr(t, 64)
        if s and s.isprintable() and len(s) > 2:
            extra = '  ; "%s"' % s[:52]
        else:
            b = pe.read(t, 8)
            if b:
                f64 = struct.unpack('<d', b)[0]
                f32 = struct.unpack('<f', b[:4])[0]
                q = struct.unpack('<Q', b)[0]
                if 1e-6 < abs(f64) < 1e6 and abs(f64 - round(f64)) > 1e-9:
                    extra = '  ; f64=%r f32=%r' % (f64, f32)
                else:
                    extra = '  ; q=0x%X f32=%r' % (q, f32)
    print('0x%X  %-8s %-46s%s' % (insn.address, insn.mnemonic, insn.op_str, extra))

print('=== stereomixer3 composite FUN_180137720 (full) ===')
for i in disasm_range(pe, md, 0x180137720, 0x1801377a0):
    ann(i)
print()
print('=== mix sub ctor FUN_1800D7400 head (find vtable) ===')
for i in disasm_range(pe, md, 0x1800D7400, 0x1800D7500):
    ann(i)
print()
print('=== clip sub ctor FUN_1800D8230 (full) ===')
for i in disasm_range(pe, md, 0x1800D8230, 0x1800D8378):
    ann(i)
