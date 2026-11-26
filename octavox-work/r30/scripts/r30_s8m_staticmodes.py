"""R30 S8m: static mode objects — tag dwords in .data/.rdata, vtable at
tag-0x10, then the mode-object vtable slot 0 body (the 0.5-flag float)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range,
                     annotate, fn_bounds, vtable_slots, looks_like_codeptr,
                     vtable_col, parse_type_descriptor, demangle_msvc)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

TAGS = {0x6c697665: 'live', 0x696e7374: 'inst', 0x6b657973: 'keys',
        0x6d6c636b: 'mclk', 0x6b6c636b: 'klck', 0x6d63746c: 'mctl'}
print('===== (a) tag dwords in .data/.rdata with qword context =====')
cands = []
for sec in ('.data', '.rdata', '_RDATA'):
    for s in pe.sections:
        if s['name'] != sec:
            continue
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        for tag, name in TAGS.items():
            off = 0
            while True:
                i = blob.find(struct.pack('<I', tag), off)
                if i < 0:
                    break
                va = pe.image_base + s['va'] + i
                # object start = va - 0x10; vtable = [va-0x10]
                vt = pe.read_u64(va - 0x10)
                q8 = pe.read_u64(va - 8)
                ok = vt is not None and 0x18037D000 <= vt < 0x1804CD000
                print('  %-5r @0x%X [%s]: [obj-0x10]=0x%X [obj-8]=0x%X %s'
                      % (name, va, sec, vt or 0, q8 or 0,
                         'VTABLE!' if ok else ''))
                if ok:
                    cands.append((name, va - 0x10, vt))
                off = i + 4
print()

print('===== (b) distinct mode-object vtables + slot0 bodies =====')
seen = {}
for (name, objva, vt) in cands:
    if vt in seen:
        seen[vt].append(name)
        continue
    seen[vt] = [name]
for vt, names in seen.items():
    col = vtable_col(pe, vt)
    td = parse_type_descriptor(pe, col['td']) if col else None
    print('  vt 0x%X (modes: %s) owner=%s' % (vt, names,
          demangle_msvc(td['name']) if td else '?'))
    slots = vtable_slots(pe, vt, 4)
    for i, v in enumerate(slots):
        print('    slot%d: 0x%X %s' % (i, v or 0,
              'CODE' if v and looks_like_codeptr(pe, v) else ''))
    s0 = slots[0]
    if s0 and looks_like_codeptr(pe, s0):
        dump(s0, s0 + 0x80, 'mode slot0')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
