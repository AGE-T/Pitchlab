"""R26 S9: LatencySamples write region + DebugReport callers + latency source trace."""
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

print('=== FUN_180310F20 latency key region (0x1803119c0..0x180311b20) ===')
for i in disasm_range(pe, md, 0x1803119c0, 0x180311b20):
    ann(i)

print()
print('=== whole-text scan: callers of DebugReport builder 0x180347500 ===')
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
        if insn.mnemonic in ('call', 'jmp') and ops[0].type == capstone.x86.X86_OP_IMM and ops[0].imm == 0x180347500:
            hits.append(hex(insn.address))
    base = end
print('callers of 0x180347500:', hits)

print()
print('=== VST3 latency strings in binary ===')
for s in [b'LatencySamples', b'getLatencySamples', b'setLatencySamples', b'DebugReport', b'PluginEnvironment', b'latency']:
    pos = 0
    hits2 = []
    while True:
        i = pe.data.find(s, pos)
        if i < 0: break
        va = pe.fo2va(i)
        if va and 0x18037D000 <= va < 0x180509000:
            hits2.append(hex(va))
        pos = i + 1
    print('  %-20s in rdata/data: %s' % (s.decode(), hits2[:12]))
