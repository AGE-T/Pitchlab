"""R29 S6k: vtable B slot-17 — the last hop of U-2 (leg 11).

s6j consolidated layout: A = 0x1803EF580 (11 slots, installed at broker+8,
IComponent-shaped, ends at +0x50); RTTI COL at 0x1803EF5D8 (0x180419D58);
B = 0x1803EF5E0 {slot0=0x180004220(qI?), 0x180003B90, 0x180003C90,
0x180003CE0, 0x180003BE0, 0x180003BA0, 0x180003D30, 0x180003DA0, ...} —
referenced by fn 0x180015F20. B.slot17 (+0x88) = [0x1803EF668] UNREAD.

 (a) B's full slot dump (0..19) + slot17 body disasm;
 (b) fn 0x180015F20 FULL (B's installer/user — the qI helper);
 (c) B.slot0 = 0x180004220 head (is it a GUID-comparing qI?).

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     vtable_slots, looks_like_codeptr)

pe = PE(); check_hash(pe)
md = get_disassembler()

B = 0x1803EF5E0
print('===== (a) vtable B @ 0x%X =====' % B)
slots = vtable_slots(pe, B, 20)
SLOTNAMES = {0: 'queryInterface', 1: 'addRef', 2: 'release',
             3: 'initialize', 4: 'terminate', 5: 'getControllerClassId',
             6: 'setIoMode', 7: 'getBusCount', 8: 'getBusInfo',
             9: 'getRoutingInfo', 10: 'activateBus', 11: 'setActive',
             12: 'setState', 13: 'getState', 14: 'setBusArrangements',
             15: 'canProcessSampleSize', 16: 'getBusArrangements',
             17: 'getLatencySamples', 18: 'process', 19: 'getTailSamples'}
for i, v in enumerate(slots):
    ok = '' if looks_like_codeptr(pe, v) else ' (DATA?)'
    print('  slot %-2d +0x%02X  0x%-10X%s  %s' % (i, 8 * i, v, ok, SLOTNAMES.get(i, '')))
lat = slots[17]
print('\n  --- slot 17 (+0x88) = 0x%X body ---' % lat)
for i in disasm_range(pe, md, lat, lat + 0x60):
    print('   ', annotate(pe, i))
print('\n  --- slot 18 (process?) = 0x%X head ---' % slots[18])
for i in disasm_range(pe, md, slots[18], slots[18] + 0x30):
    print('   ', annotate(pe, i))
print('\n  --- slot 19 (getTailSamples?) = 0x%X body ---' % slots[19])
for i in disasm_range(pe, md, slots[19], slots[19] + 0x40):
    print('   ', annotate(pe, i))

print('\n===== (b) fn 0x180015F20 (Bs user) =====')
for i in disasm_range(pe, md, 0x180015F20, 0x180016060):
    print(' ', annotate(pe, i))

print('\n===== (c) B.slot0 0x180004220 head =====')
for i in disasm_range(pe, md, 0x180004220, 0x1800042D0):
    print(' ', annotate(pe, i))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
