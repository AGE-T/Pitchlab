"""R29 S6i: .text extent + aggregator path + process() sanity (U-2, leg 9).

s6h: the broker ctor (0x18000BEF0, 0x480-byte object) installs REAL vtables:
[+0]=0x1803EF8A0, [+8]=0x1803EF580 (IAudioProcessor, clean slots),
[+0x10]=0x1803EFDC0, [+0x18]=0x1803EF0D0, [+0x20]=0x1803F01E8, [+0x28]=0x1803F0210;
engine (0x28A0B) at +0x38 (aggregated FIRST by the outer qI 0x18000C230 via
lookup 0x180160D5C + thunk 0x180016E14).

This probe:
 (a) PE section table — the TRUE .text extent (are the 0x18041xxxx 'DATA?'
     vtable slots actually code stubs beyond R26's TEXT_HI=0x18037D000?);
 (b) disasm the suspicious slots {0x180419D58, 0x1804185C0, 0x180417D80,
     0x180419670, 0x180417790} + process() 0x180003D30 head;
 (c) the aggregation thunk 0x180016E14 + the lookup 0x160D5C head;
 (d) the engine-side COM surface: follow the engine ctor 0x1801A7620's vtable
     installs (the 0x28A0 class) — find ITS IAudioProcessor view + slot17.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     opstr_rip_target, get_bulk_disassembler, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

print('===== (a) section table =====')
for s in pe.sections:
    print('  %-8s VA 0x%X..0x%X (vsize 0x%X, raw 0x%X)'
          % (s['name'], pe.image_base + s['va'],
             pe.image_base + s['va'] + max(s['vsize'], s['rsize']),
             s['vsize'], s['rsize']))

print('\n===== (b) suspicious vtable slots + process() =====')
for va in (0x180419D58, 0x1804185C0, 0x180417D80, 0x180419670, 0x180417790):
    print('--- 0x%X [%s] ---' % (va, pe.sec_of(va)))
    for i in disasm_range(pe, md, va, va + 0x20):
        print(' ', annotate(pe, i))
print('--- process() 0x180003D30 (head) ---')
for i in disasm_range(pe, md, 0x180003D30, 0x180003DC0):
    print(' ', annotate(pe, i))

print('\n===== (c) aggregation thunk 0x180016E14 + lookup 0x180160D5C =====')
for i in disasm_range(pe, md, 0x180016E00, 0x180016E40):
    print(' ', annotate(pe, i))
print('--- lookup 0x180160D5C (head) ---')
for i in disasm_range(pe, md, 0x180160D5C, 0x180160E40):
    print(' ', annotate(pe, i))

print('\n===== (d) engine ctor 0x1801A7620 — vtable installs =====')
count = 0
for i in bmd.disasm(pe.data[pe.va2fo(0x1801A7620):pe.va2fo(0x1801A7620) + 0x900], 0x1801A7620):
    if i.mnemonic == 'lea' and 'rip' in i.op_str:
        t = opstr_rip_target(i)
        if t is not None and 0x1803E0000 <= t < 0x180400000:
            print('  0x%X  lea -> 0x%X' % (i.address, t))
            count += 1
print('  (%d vtable-ish leas in engine ctor head)' % count)

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
