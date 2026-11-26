"""R30 S8l: the mode-table builder — objects tagged 'live'/'inst'/... and
their vtable slot 0 (the 0.5-threshold float source)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, vtable_slots,
                     looks_like_codeptr, vtable_col, parse_type_descriptor,
                     demangle_msvc)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

TAGS = {'live': 0x6c697665, 'inst': 0x696e7374, 'keys': 0x6b657973,
        'mclk': 0x6d6c636b, 'klck': 0x6b6c636b, 'mctl': 0x6d63746c}
print('===== (a) tag-store sites (mov dword [r+0x10], imm32tag) =====')
hits = []
for insn in bmd.disasm(pe.data[pe.va2fo(0x180001000):pe.va2fo(0x180001000) + (0x18037D000 - 0x180001000)], 0x180001000):
    op = '%s %s' % (insn.mnemonic, insn.op_str)
    m = re.match(r'^mov +dword ptr \[(\w+) \+ 0x10\], (0x[0-9a-f]+)$', op)
    if m:
        v = int(m.group(2), 16)
        if v in TAGS.values():
            hits.append((insn.address, [k for k, tv in TAGS.items() if tv == v][0]))
for (va, tag) in hits:
    print('  0x%X: %r  (fn %s)' % (va, tag, fn_bounds(va) or '?'))
print()

print('===== (b) contexts of the tag stores =====')
fns = {}
for (va, tag) in hits:
    fb = fn_bounds(va)
    if fb:
        fns.setdefault(fb, []).append((va, tag))
for fb, lst in fns.items():
    print('  fn 0x%X..0x%X: %s' % (fb[0], fb[1], [t for _, t in lst]))
print()

if fns:
    fb = sorted(fns)[0]
    dump(fb[0], min(fb[1], fb[0] + 0x200), 'first mode-builder fn head')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
