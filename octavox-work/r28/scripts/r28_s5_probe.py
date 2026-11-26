"""R28 S5: targeted follow-up probes proposed by the s1-s4 analysis round
(agent-proposed next actions; static only, hash pre+post).

(a) m02 jump-table order — read the 7 qwords at 0x1800E1890 (ids 7..13) and
    dump each case body's head, to pin which id is the cents-from-C4 read-out
    (the 1200*log2(f/261.6256) case at ~0x1800E1568) and what the others read.
(b) U-7 closer — scan all .text for reads at [reg+0x3E0] (the userscale
    parameter storage cell at pitch-config+0x3E0). Zero dsp readers = full
    UI-legacy closure; any hit gets a context dump.
(c) U-2 helper — scan all .text for +0x25C8 displacements (the PluginEnvironment
    serialiser-state pointer on the engine core) and dump context, hunting the
    writers of env+0x24 (the LatencySamples diagnostic source).
(d) the 9 slot-4 'mov edx,1' candidate sites — dump context; when routed through
    the engine's slot-4 demux (children[1]->m02(r8d, r9)) the m02 id travels in
    r8d, so the dumps look for r8d setup.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, rip_target,
                     annotate, dump, load_pdata, fn_bounds, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()
fns = load_pdata()
print('pdata functions: %d' % len(fns))

# ---------------- (a) m02 jump table 0x1800E1890 ----------------
print('\n===== (a) m02 jump table at 0x1800E1890 (ids 7..13) =====')
jt = 0x1800E1890
targets = []
for k in range(7):
    q = struct.unpack('<Q', pe.read(jt + 8 * k, 8))[0]
    targets.append(q)
    print('id %2d -> 0x%X' % (7 + k, q))
seen = set()
for k, q in enumerate(targets):
    if q in seen:
        print('\n--- id %d -> 0x%X (same target as an earlier id) ---' % (7 + k, q))
        continue
    seen.add(q)
    print('\n--- id %2d case @ 0x%X (first 30 insns) ---' % (7 + k, q))
    n = 0
    for insn in disasm_range(pe, md, q, q + 0x180):
        print(annotate(pe, insn))
        n += 1
        if insn.mnemonic in ('ret', 'jmp') and n > 4:
            break
        if n >= 30:
            break

# ---------------- (b) reads of [reg+0x3E0] (userscale storage) ----------------
print('\n===== (b) .text scan: +0x3E0 displacement accesses (userscale storage) =====')
hits_b = []
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    for insn in disasm_range(pe, md, lo, hi):
        if '0x3e0]' in insn.op_str:
            hits_b.append((lo, insn))
print('total +0x3E0 hits: %d' % len(hits_b))
for lo, insn in hits_b:
    print('  fn 0x%X  0x%X  %s %s' % (lo, insn.address, insn.mnemonic, insn.op_str))
shown = 0
for lo, insn in hits_b:
    if shown >= 12:
        break
    print('\n--- context @ 0x%X (fn 0x%X) ---' % (insn.address, lo))
    for i in disasm_range(pe, md, insn.address - 0x18, insn.address + 0x20):
        print(annotate(pe, i))
    shown += 1

# ---------------- (c) +0x25C8 accesses (env-state pointer) ----------------
print('\n===== (c) .text scan: +0x25C8 displacement accesses =====')
hits_c = []
for lo, hi in fns:
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    for insn in disasm_range(pe, md, lo, hi):
        if '0x25c8]' in insn.op_str:
            hits_c.append((lo, insn))
print('total +0x25C8 hits: %d' % len(hits_c))
for lo, insn in hits_c:
    print('  fn 0x%X  0x%X  %s %s' % (lo, insn.address, insn.mnemonic, insn.op_str))
for lo, insn in hits_c[:8]:
    print('\n--- context @ 0x%X (fn 0x%X) ---' % (insn.address, lo))
    for i in disasm_range(pe, md, insn.address - 0x30, insn.address + 0x50):
        print(annotate(pe, i))

# ---------------- (d) the 9 slot-4 candidate sites ----------------
print('\n===== (d) slot-4 mov edx,1 candidate sites =====')
SITES = [0x1800044B2, 0x1800AEB85, 0x1800B2A27, 0x1801A01F2, 0x18028AA04,
         0x1802973FC, 0x1802D03B0, 0x1802D4E79, 0x18030130B]
for va in SITES:
    b = fn_bounds(va)
    if b is None:
        print('\n--- site 0x%X: NO PDATA BOUNDS ---' % va)
        continue
    lo, hi = b
    print('\n--- site 0x%X (fn 0x%X..0x%X) ---' % (va, lo, hi))
    for i in disasm_range(pe, md, max(lo, va - 0x50), min(hi, va + 0x38)):
        print(annotate(pe, i))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
