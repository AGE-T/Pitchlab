"""R26 S9 deep-dive: serializer caller identity, kernel thunk callers, harmony base region."""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256
import capstone

pe = PE()
assert pe.sha256 == EXPECT_SHA256
md = get_disassembler()

def ann(insn):
    try:
        t = rip_target(insn)
    except capstone.CsError:
        t = None
    extra = ''
    if t is not None:
        s = pe.read_cstr(t, 96)
        if s and s.isprintable() and len(s) > 2:
            extra = '  ; "%s"' % s[:72]
        else:
            b = pe.read(t, 8)
            if b:
                q = struct.unpack('<Q', b)[0]
                f32 = struct.unpack('<f', b[:4])[0]
                extra = '  ; q=0x%X f32=%r' % (q, f32)
    print('0x%X  %-8s %-44s%s' % (insn.address, insn.mnemonic, insn.op_str, extra))

print('=== caller head: FUN @0x180347500 ===')
for i in disasm_range(pe, md, 0x180347500, 0x1803475d0):
    ann(i)

print()
print('=== serializer FUN_180310F20 (head + key writes) ===')
for i in disasm_range(pe, md, 0x180310F20, 0x180311200):
    ann(i)

print()
print('=== callers of kernel-table thunk 0x18032B720 (full-text scan) ===')
TEXT_LO, TEXT_HI = 0x180001000, 0x18037D000
hits = []
base = TEXT_LO
while base < TEXT_HI:
    end = min(base + 0x40000, TEXT_HI)
    chunk = pe.data[pe.va2fo(base):pe.va2fo(end)]
    for insn in md.disasm(chunk, base):
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        if not ops: continue
        if insn.mnemonic in ('call', 'jmp') and ops[0].type == capstone.x86.X86_OP_IMM and ops[0].imm == 0x18032B720:
            hits.append(hex(insn.address))
        else:
            try:
                t = rip_target(insn)
            except capstone.CsError:
                t = None
            if t == 0x18032B720:
                hits.append(hex(insn.address) + ':mem')
    base = end
print('refs to 0x18032B720:', hits)

print()
print('=== memory 0x1803BB5E0..0x1803BB700 (int32 dump) ===')
fo = pe.va2fo(0x1803BB5E0)
vals = struct.unpack_from('<77i', pe.data, fo)  # 0x134 bytes / 4
for i in range(0, 77, 12):
    print('  0x%X: %s' % (0x1803BB5E0 + 4*i, list(vals[i:i+12])))

print()
print('=== context at 0x1800C9A73 (ref to 0x1803BB5F0) ===')
for i in disasm_range(pe, md, 0x1800C9A40, 0x1800C9AC0):
    ann(i)
