"""R30 S8b: U-2 last hop, leg 2 — the engine's RTTI hierarchy and the
VST3ClientExtensions subobject.

s8a established: outer qI = dynamic_cast<juce::VST3ClientExtensions*>(
juce::AudioProcessor* engine[broker+0x38]) then ->queryIidExtensions(iid, &out)
via thunk 0x180016E14 (vtable slot +0x10 of the casted view).

This probe:
 (a) the engine's PRIMARY vtable (0x1803F05D0, installed at engine+0x00):
     COL -> most-derived class name + full base list (TD names, mdisp);
 (b) locate the VST3ClientExtensions base (TD == 0x1804B73E0) -> offset;
 (c) dump the engine ctor 0x1801A7620 vtable installs with offsets to map
     the view vtable at that offset;
 (d) for every engine-agg vtable candidate: its COL TD name (which base
     view it serves);
 (e) read the VST3ClientExtensions view vtable slots; identify slot+0x10.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     fn_bounds, class_bases, parse_type_descriptor,
                     demangle_msvc, vtable_col, vtable_slots, looks_like_codeptr,
                     TD_DST, ENGINE_AGG_CTOR, ENGINE_AGG_VTABLES,
                     TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) engine primary vtable 0x1803F05D0: class + bases =====')
prim = class_bases(pe, 0x1803F05D0)
if prim:
    td = parse_type_descriptor(pe, prim['col']['td'])
    print('  most-derived TD @ 0x%X name=%r -> %s'
          % (prim['col']['td'], td['name'], demangle_msvc(td['name'])))
    print('  COL: sig=%d offset=%d cdoff=%d; CHD attrs=0x%X nbases=%d'
          % (prim['col']['sig'], prim['col']['offset'], prim['col']['cdoff'],
             prim['chd']['attrs'], prim['chd']['nbases']))
    print('  bases (in array order):')
    vst3ce_off = None
    for i, b in enumerate(prim['bases']):
        nm = demangle_msvc(b['td_name'])
        print('    [%2d] mdisp=%+#6x pdisp=%+#6x vdisp=%+#6x ncont=%d attrs=0x%X td=0x%X %s'
              % (i, b['mdisp'], b['pdisp'], b['vdisp'], b['num_contained'],
                 b['attrs'], b['td'], nm))
        if b['td'] == TD_DST:
            vst3ce_off = b['mdisp']
    print()
    print('  >>> VST3ClientExtensions base offset = %s'
          % ('%+#x' % vst3ce_off if vst3ce_off is not None else 'NOT FOUND'))
else:
    print('  (no COL on primary vtable)')
    vst3ce_off = None
print()

print('===== (b) engine ctor 0x1801A7620: all vtable installs =====')
b = fn_bounds(ENGINE_AGG_CTOR)
lo, hi = b if b else (ENGINE_AGG_CTOR, ENGINE_AGG_CTOR + 0x800)
print('  bounds: 0x%X..0x%X' % (lo, hi))
print('  (stores of lea-loaded vtables: pattern lea rax,[rip->vt]; mov [rsi+off],rax)')
dump(lo, hi, 'engine ctor')

print('===== (c) engine-agg vtable candidates: COL TD names =====')
for vt in ENGINE_AGG_VTABLES:
    col = vtable_col(pe, vt)
    if col is None:
        print('  vt 0x%X: no COL' % vt)
        continue
    td = parse_type_descriptor(pe, col['td'])
    nm = demangle_msvc(td['name']) if td else None
    slots = vtable_slots(pe, vt, 6)
    print('  vt 0x%X: COL@0x%X off=%d TD=%s' % (vt, col['va'], col['offset'], nm))
    for i in range(6):
        v = slots[i]
        print('      slot%d (+0x%X): %s' % (i, 8 * i,
              ('0x%X' % v) if v else '?'))
print()

print('===== (d) VST3ClientExtensions view: qword read at engine+offset =====')
if vst3ce_off is not None:
    # the ctor installs; find which install offset matches vst3ce_off by
    # checking every vtable whose slot+0x10 differs - the real check is in
    # the ctor dump above. Also dump slot layouts of ALL agg vtables.
    for vt in ENGINE_AGG_VTABLES:
        slots = vtable_slots(pe, vt, 5)
        s2 = slots[2] if len(slots) > 2 else None
        if s2 and looks_like_codeptr(pe, s2):
            print('  vt 0x%X slot2(+0x10) = 0x%X' % (vt, s2))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
