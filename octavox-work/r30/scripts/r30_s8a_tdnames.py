"""R30 S8a: U-2 last hop, leg 1 — the __RTDynamicCast descriptors named.

The outer qI 0x18000C230 @ 0x18000C272 calls 0x180160D5C with
(rcx=engine[broker+0x38], rdx=0, r8=TD_SRC@0x1804B7448, r9=TD_DST@0x1804B73E0,
isRef=0) — the MSVC __RTDynamicCast signature. Both .data slots hold
0x18037FE80 = the shared type_info vftable, i.e. both args are TypeDescriptors.

This probe:
 (a) the exact bytes of the dispatch thunk 0x180016E14 (which vtable slot of
     the walk result receives the (sub, iid, &out) call);
 (b) both TypeDescriptors fully parsed: vftable / spare / NAME STRING;
 (c) __RTDynamicCast 0x160160D5C full disassembly (bounds via .pdata);
 (d) .data context around both TDs (qwords +-0x20);
 (e) the type_info vftable 0x18037FE80 slots (slot0 = 0x18015EAD0 dtor?).

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     fn_bounds, parse_type_descriptor, demangle_msvc,
                     TD_SRC, TD_DST, RT_DYNAMIC_CAST, QI_DISPATCH_THUNK,
                     TYPE_INFO_VFTABLE)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) dispatch thunk 0x180016E14 =====')
dump(QI_DISPATCH_THUNK, QI_DISPATCH_THUNK + 0x20, 'thunk')

print('===== (b) TypeDescriptors =====')
for label, tdva in (('SRC (r8)', TD_SRC), ('DST (r9)', TD_DST)):
    td = parse_type_descriptor(pe, tdva)
    print('  %s @ 0x%X: vftable=0x%X spare=0x%X name=%r -> %s'
          % (label, tdva, td['vftable'] or 0, td['spare'] or 0, td['name'],
             demangle_msvc(td['name'])))
print()

print('===== (c) __RTDynamicCast 0x180160D5C =====')
b = fn_bounds(RT_DYNAMIC_CAST)
print('  pdata bounds:', ('0x%X..0x%X' % b) if b else 'NOT FOUND')
lo, hi = b if b else (RT_DYNAMIC_CAST, RT_DYNAMIC_CAST + 0x400)
dump(lo, hi, '__RTDynamicCast')

print('===== (d) .data context around the TDs =====')
for tdva in (TD_SRC, TD_DST):
    print('  around 0x%X:' % tdva)
    for off in range(-0x20, 0x30, 8):
        v = pe.read_u64(tdva + off)
        print('    [0x%X%+d] = 0x%X' % (tdva, off, v or 0))
print()

print('===== (e) type_info vftable 0x18037FE80 =====')
for i in range(4):
    v = pe.read_u64(TYPE_INFO_VFTABLE + 8 * i)
    print('  slot%d (+0x%X): 0x%X' % (i, 8 * i, v or 0))
print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
