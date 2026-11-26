"""R28 S5c: precision decode (static only, hash pre+post).
(a3) decode the float constants used by the m02 case bodies (ids 7..13) by
     resolving each rip-relative operand via capstone and printing f32/f64.
(c3) re-derive the env pointer at the DebugReport serialiser call site
     (fn 0x180347500 @ ~0x180347848..0x180347857): dump the context to see
     whether rcx = rbx (= X+0x25C8) or rcx = [rbx]+8; then grep the ctor
     fn 0x180312670 for stores to the matching env+0x24 offset.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range,
                     rip_target, annotate, load_pdata, fn_bounds)

pe = PE(); check_hash(pe)
md = get_disassembler()
fns = load_pdata()

# ---------------- (a3) constants of the m02 cases ----------------
print('===== (a3) m02 case constants =====')
CASES = {
    'id7  0x1800E1568..': (0x1800E1554, 0x1800E15CB),
    'id8  0x1800E15CB..': (0x1800E15CB, 0x1800E161E),
    'id9  0x1800E161E..': (0x1800E161E, 0x1800E16A5),
    'id10 0x1800E16A5..': (0x1800E16A5, 0x1800E16DB),
    'id11 0x1800E16DB..': (0x1800E16DB, 0x1800E1751),
    'id12 0x1800E1751..': (0x1800E1751, 0x1800E17E1),
    'id13 0x1800E17E1..': (0x1800E17E1, 0x1800E1880),
}
for name, (lo, hi) in CASES.items():
    print('\n--- %s ---' % name)
    for insn in disasm_range(pe, md, lo, hi):
        if '[rip' in insn.op_str and ('ss' in insn.mnemonic or 'sd' in insn.mnemonic):
            try:
                t = rip_target(insn)
            except Exception:
                continue
            if t is None:
                continue
            b = pe.read(t, 8)
            f32v = struct.unpack('<f', b[:4])[0]
            f64v = struct.unpack('<d', b)[0]
            print('  0x%X  %-8s %-40s -> 0x%X  f32=%r  f64=%r' %
                  (insn.address, insn.mnemonic, insn.op_str, t, f32v, f64v))

# ---------------- (c3) serialiser call-site context ----------------
print('\n===== (c3) DebugReport serialiser call site (fn 0x180347500) =====')
print('--- 0x180347820..0x180347880 ---')
for i in disasm_range(pe, md, 0x180347820, 0x180347880):
    print(annotate(pe, i))

print('\n--- serialiser head FUN_180310F20 0x180310F20..0x180310FA0 ---')
for i in disasm_range(pe, md, 0x180310F20, 0x180310FA0):
    print(annotate(pe, i))

print('\n--- LatencySamples read at 0x180311A10 (context) ---')
for i in disasm_range(pe, md, 0x1803119E0, 0x180311A40):
    print(annotate(pe, i))

# ---------------- (c3b) ctor stores near +0x25C8..+0x2600 ----------------
print('\n===== (c3b) ctor fn 0x180312670: stores to +0x25C0..+0x2600 =====')
lo, hi = fn_bounds(0x180312670)
insns = list(disasm_range(pe, md, lo, hi))
for i, insn in enumerate(insns):
    if insn.mnemonic.startswith('mov') and ('0x25c]' in insn.op_str or
                                             '0x25d]' in insn.op_str or
                                             '0x25e]' in insn.op_str or
                                             '0x25f]' in insn.op_str or
                                             '0x260]' in insn.op_str):
        print('  0x%X  %s %s' % (insn.address, insn.mnemonic, insn.op_str))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
