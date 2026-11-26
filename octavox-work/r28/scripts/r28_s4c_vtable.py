"""R28 S4c: locate the REAL JuceVST3Component vtable.
Dump the qword region around 0x1803F05D0, find the pointer-run boundaries,
and scan .text for ANY RIP-relative reference into the region (ctor installs).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, load_pdata)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

base = 0x1803F0400
blob = pe.read(base, 0x300)
print('=== qwords 0x1803F0400..0x1803F0700 ===')
run = []
for i in range(0, 0x300, 8):
    q = struct.unpack('<Q', blob[i:i+8])[0]
    va = base + i
    is_text = 0x180001000 <= q < 0x18037D000
    if is_text:
        run.append(va)
    print('0x%X: 0x%016X %s' % (va, q, '<- .text' if is_text else ''))

print()
print('pointer runs (starts of consecutive .text qwords):')
prev = None
start = None
for va in run:
    if prev is None or va != prev + 8:
        if start is not None:
            print('  run 0x%X..0x%X (%d slots)' % (start, prev, (prev - start) // 8 + 1))
        start = va
    prev = va
if start is not None:
    print('  run 0x%X..0x%X (%d slots)' % (start, prev, (prev - start) // 8 + 1))

# scan .text for RIP-relative refs into 0x1803F0400..0x1803F0700
LO, HI = 0x1803F0400, 0x1803F0700
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
            refs.append((hex(insn.address), hex(t), insn.mnemonic + ' ' + insn.op_str, hex(lo)))
print()
print('=== .text refs into the vtable region: %d ===' % len(refs))
for r in refs:
    print('  %s -> %s  %s  (fn %s)' % r)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
