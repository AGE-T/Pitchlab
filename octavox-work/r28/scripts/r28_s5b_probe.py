"""R28 S5b: corrected follow-ups (static only, hash pre+post).
(a2) m02 jump table — the table at 0x1800E1890 uses 4-byte image-RVA entries
     (qword misread in s5 proved it: entries 0x000E1554.. + 0xCCCCCCCC padding).
     Decode ids 7..13 correctly and dump each case body until ret/jmp.
(c2) the env-state constructor fn 0x180312670 — find stores to +0x2C
     (= env+0x24, the LatencySamples diagnostic source, since [X+0x25C8]=X
     self-pointer means env = X+8 and env+0x24 = X+0x2C) and dump their values.
(d2) the remaining slot-4 sites from s5(d): print only the 12 insns before each
     call [rax+0x20] to classify r8d/r8 setup (m02-id path vs plumbing).
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, load_pdata, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()
fns = load_pdata()

# ---------------- (a2) m02 jump table, 4-byte RVA entries ----------------
print('===== (a2) m02 jump table 0x1800E1890, 4-byte RVA entries =====')
jt = 0x1800E1890
for k in range(7):
    rva = struct.unpack('<I', pe.read(jt + 4 * k, 4))[0]
    print('id %2d -> 0x%X' % (7 + k, 0x180000000 + rva))
for k in range(7):
    rva = struct.unpack('<I', pe.read(jt + 4 * k, 4))[0]
    q = 0x180000000 + rva
    print('\n--- id %2d case @ 0x%X ---' % (7 + k, q))
    n = 0
    for insn in disasm_range(pe, md, q, q + 0x100):
        print(annotate(pe, insn))
        n += 1
        if insn.mnemonic in ('ret', 'jmp') and n > 3:
            break
        if n >= 26:
            break

# ---------------- (c2) ctor +0x2C stores ----------------
print('\n===== (c2) fn 0x180312670 stores to +0x2C =====')
lo, hi = fn_bounds(0x180312670)
print('fn bounds: 0x%X..0x%X' % (lo, hi))
insns = list(disasm_range(pe, md, lo, hi))
for i, insn in enumerate(insns):
    if insn.mnemonic.startswith('mov') and '+ 0x2c]' in insn.op_str:
        print('\n--- store @ 0x%X ---' % insn.address)
        for j in range(max(0, i - 6), min(len(insns), i + 4)):
            print(annotate(pe, insns[j]))

# also: any OTHER function storing to +0x2C on an object whose +0x25C8 was
# initialised by fn 0x180312670 is impossible to pin without SSA; instead we
# dump the ctor's first 40 insns (the field-init head) for context.
print('\n--- ctor head (first 40 insns) ---')
n = 0
for insn in disasm_range(pe, md, lo, lo + 0x200):
    print(annotate(pe, insn))
    n += 1
    if n >= 40:
        break

# ---------------- (d2) remaining slot-4 sites ----------------
print('\n===== (d2) slot-4 sites: 12 insns before call [rax+0x20] =====')
SITES = [0x1800B2A27, 0x1801A01F2, 0x18028AA04,
         0x1802973FC, 0x1802D03B0, 0x1802D4E79, 0x18030130B]
for va in SITES:
    b = fn_bounds(va)
    if b is None:
        print('\n--- site 0x%X: NO PDATA BOUNDS ---' % va)
        continue
    lo2, hi2 = b
    print('\n--- site 0x%X (fn 0x%X..0x%X) ---' % (va, lo2, hi2))
    for i in disasm_range(pe, md, max(lo2, va - 0x40), min(hi2, va + 0x10)):
        print(annotate(pe, i))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
