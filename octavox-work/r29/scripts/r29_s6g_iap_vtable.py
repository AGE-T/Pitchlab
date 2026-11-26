"""R29 S6g: THE IAudioProcessor vtable (U-2, leg 7 — the final read).

s6f: the outer qI = fn 0x18000C230 (called by adjustor thunks
sub {8,0x10,0x18,0x20}; delegates to the inner qI 0x18000EC90 whose
IAudioProcessor answer = this+8). Therefore the vtable installed at this+8
has slot 0 = thunk 0x180016ED0 (sub rcx,8; jmp 0x18000C230).

This probe:
 (a) find .rdata/.data qwords == the four adjustor thunk VAs
     {0x180016DB4, 0x180016E40, 0x180016ED0, 0x180016F98} -> vtable bases;
 (b) dump each found vtable's slots 0..19 (+ which slot equals which thunk);
 (c) RTTI: the qword before each vtable (complete object locator) ->
     walk to the type descriptor -> the CLASS NAME string;
 (d) from the this+8 vtable (slot0 = 0x180016ED0): read slot 17 (+0x88)
     = getLatencySamples and DUMP ITS BODY (the U-2 answer).

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     vtable_slots, looks_like_codeptr, find_bytes)

pe = PE(); check_hash(pe)
md = get_disassembler()

THUNKS = {0x180016DB4: 'sub 0x18', 0x180016E40: 'sub 0x20',
          0x180016ED0: 'sub 0x8  <<< IAudioProcessor(this+8)',
          0x180016F98: 'sub 0x10'}
SLOTNAMES = {0: 'queryInterface', 1: 'addRef', 2: 'release',
             3: 'initialize', 4: 'terminate',
             5: 'getControllerClassId', 6: 'setIoMode', 7: 'getBusCount',
             8: 'getBusInfo', 9: 'getRoutingInfo', 10: 'activateBus',
             11: 'setActive', 12: 'setState', 13: 'getState',
             14: 'setBusArrangements', 15: 'canProcessSampleSize',
             16: 'getBusArrangements', 17: 'getLatencySamples',
             18: 'process', 19: 'getTailSamples'}

print('===== (a) vtable discovery via adjustor-thunk qwords =====')
vtbases = {}
for tv, desc in THUNKS.items():
    for hit in find_bytes(pe, struct.pack('<Q', tv), ('.rdata', '.data')):
        print('  thunk 0x%X (%s) referenced at 0x%X [%s]' % (tv, desc, hit, pe.sec_of(hit)))
        vtbases[hit] = tv

def read_rtti_name(vt_va):
    """MSVC: [vt-8] = CompleteObjectLocator* -> +0xC offset to TypeDescriptor
    (from image base); TypeDescriptor+0x10 = the name string."""
    try:
        col = pe.read_u64(vt_va - 8)
        if col is None or not (0x180000000 <= col < 0x181000000):
            return None
        td_off = pe.read_u32(col + 0xC)
        td = 0x180000000 + td_off
        sig = pe.read_u32(col)
        name = pe.read_cstr(td + 0x10, 96)
        if name and all(31 < ord(c) < 127 for c in name):
            return 'sig=%d name=%s' % (sig, name)
    except Exception:
        pass
    return None

print('\n===== (b)+(c) vtables + RTTI names =====')
iap_vt = None
for hit in sorted(vtbases):
    tv = vtbases[hit]
    rtti = read_rtti_name(hit)
    print('--- candidate vtable @ 0x%X (slot0 = 0x%X %s)  RTTI: %s ---'
          % (hit, tv, THUNKS[tv], rtti or 'n/a'))
    slots = vtable_slots(pe, hit, 20)
    for i, v in enumerate(slots):
        nm = SLOTNAMES.get(i, '')
        ok = '' if looks_like_codeptr(pe, v) else ' (DATA?)'
        print('   slot %-2d +0x%02X  0x%-10X%s  %s' % (i, 8 * i, v, ok, nm))
    if tv == 0x180016ED0:
        iap_vt = hit

print('\n===== (d) getLatencySamples (slot 17, +0x88) =====')
if iap_vt is None:
    print('!! IAudioProcessor vtable not identified via slot0 == 0x180016ED0')
else:
    lat = pe.read_u64(iap_vt + 0x88)
    print('  IAudioProcessor vtable @ 0x%X  ->  slot17 (+0x88) = 0x%X' % (iap_vt, lat))
    print('  --- getLatencySamples body ---')
    for i in disasm_range(pe, md, lat, lat + 0x80):
        print('   ', annotate(pe, i))
    # bonus: the neighbours for context
    for idx in (14, 15, 16, 18, 19):
        v = pe.read_u64(iap_vt + 8 * idx)
        print('  context slot %d (%s) = 0x%X' % (idx, SLOTNAMES.get(idx, '?'), v))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
