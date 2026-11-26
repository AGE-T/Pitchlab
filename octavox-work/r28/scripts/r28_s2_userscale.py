"""R28 S2: userscale path (U-7) — no-writer/no-consumer proof.

(a) dump the userscale map region at 0x1803BB60C and its neighbours;
(b) scan whole .text for RIP-relative references INTO the map region;
(c) dump the userscale initializer site 0x1800E4A64 (pitch-config initializer
    FUN_1800E3350) and follow the dirty-block applier it installs.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     dump, load_pdata, scan_calls, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

# (a) the map region
print('=== userscale map region 0x1803BB580..0x1803BB700 (ints) ===')
base = 0x1803BB580
blob = pe.read(base, 0x180)
for i in range(0, 0x180, 16):
    vals = struct.unpack('<4i', blob[i:i+16])
    print('0x%X: %s' % (base + i, ' '.join('%4d' % v for v in vals)))

# (b) RIP-relative references into 0x1803BB5C0..0x1803BB680
LO, HI = 0x1803BB5C0, 0x1803BB680
refs = []
fns = load_pdata()
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    for insn in disasm_range(pe, md, lo, hi):
        try:
            t = None
            import capstone
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
print('=== .text references into 0x%X..0x%X: %d ===' % (LO, HI, len(refs)))
for r in refs:
    print('  0x%s -> 0x%s  %s   (fn 0x%s)' % (r['va'][2:], r['target'][2:], r['insn'], r['fn'][2:]))
dump_json('r28_s2_userscale_refs.json', refs)

# (c) userscale initializer context in FUN_1800E3350
dump(pe, md, 0x1800E49C0, 0x1800E4B60, 'userscale initializer site 0x1800E4A64 context')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
