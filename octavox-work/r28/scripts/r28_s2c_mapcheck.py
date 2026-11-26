"""R28 S2c: definitive userscale-map validation.

(a) dump 0x1803BB5C0..0x1803BB610 as qwords + resolve pointer targets;
(b) scan whole .text for RIP-relative refs into 0x1803BB5F0..0x1803BB660
    (the claimed userscale map 0x1803BB60C and the keymap 0x1803BB610);
(c) dump the string at the descriptor's first pointer target.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     load_pdata, dump_json)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

print('=== 0x1803BB5C0..0x1803BB614 as qwords ===')
base = 0x1803BB5C0
blob = pe.read(base, 0x50)
for i in range(0, 0x50, 8):
    q = struct.unpack('<Q', blob[i:i+8])[0]
    note = ''
    if 0x180000000 <= q < 0x180500000:
        s = pe.read_cstr(q, 48)
        if s and len(s) > 2:
            note = ' -> "%s"' % s[:44]
        else:
            note = ' -> VA'
    print('0x%X: 0x%016X%s' % (base + i, q, note))

LO, HI = 0x1803BB5F0, 0x1803BB660
refs = []
for lo, hi in load_pdata():
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    for insn in disasm_range(pe, md, lo, hi):
        try:
            t = None
            for op in insn.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    t = insn.address + insn.size + op.mem.disp
                    break
        except Exception:
            t = None
        if t is not None and LO <= t < HI:
            refs.append({'va': hex(insn.address), 'target': hex(t),
                         'insn': insn.mnemonic + ' ' + insn.op_str, 'fn': hex(lo)})
print()
print('=== .text refs into 0x%X..0x%X: %d ===' % (LO, HI, len(refs)))
for r in refs:
    print('  0x%s -> 0x%s  %s  (fn 0x%s)' % (r['va'][2:], r['target'][2:], r['insn'], r['fn'][2:]))
dump_json('r28_s2c_map_refs.json', refs)

# also: who references the KEYMAP (0x1803BB610) and scale patterns (0x1803BB780)?
for probe in (0x1803BB610, 0x1803BB780, 0x1803BB690):
    hits = [r for r in refs_full] if False else None
# (covered by the LO..HI scan for keymap; separate quick scan for the others)
for probe_lo, probe_hi, lbl in ((0x1803BB690, 0x1803BB6E0, 'temperament'),
                                (0x1803BB780, 0x1803BB7D0, 'scale_patterns')):
    hits = []
    for lo, hi in load_pdata():
        if not (0x180001000 <= lo < 0x18037D000):
            continue
        for insn in disasm_range(pe, md, lo, hi):
            try:
                t = None
                for op in insn.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        t = insn.address + insn.size + op.mem.disp
                        break
            except Exception:
                t = None
            if t is not None and probe_lo <= t < probe_hi:
                hits.append((hex(insn.address), hex(t), insn.mnemonic + ' ' + insn.op_str, hex(lo)))
    print('=== refs into %s 0x%X..0x%X: %d ===' % (lbl, probe_lo, probe_hi, len(hits)))
    for h in hits[:8]:
        print('  %s -> %s  %s  (fn %s)' % h)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
