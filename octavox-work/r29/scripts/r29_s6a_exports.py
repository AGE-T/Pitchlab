"""R29 S6a: TRUE export-table parse + GetPluginFactory pin (U-2, leg 1).

Fixes the s4f defect (buggy ordinal handling — R28 F4.1): parse the export
directory exactly per the PE spec (name table -> ordinal INDEX -> functions
RVA), print every export, then follow the REAL GetPluginFactory: dump its
body (following jmp/call chains), the factory ctor 0x180194BB0, and the
factory vtable 0x1803EEF98 slots against the IPluginFactory/2/3 layout.

Static only. Hash checked pre+post.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     export_dir, vtable_slots, looks_like_codeptr,
                     SDK_IIDS, find_bytes, guid_bytes_encA, guid_bytes_encB)

pe = PE(); check_hash(pe)
md = get_disassembler()

print('===== (a) export directory, spec-correct parse =====')
exps = export_dir(pe)
for (name, va, ordi, fwd) in exps:
    if va is None:
        print('  %-24s ord#%d  FORWARDED -> %s' % (name, ordi, fwd))
    else:
        print('  %-24s ord#%-3d VA 0x%X  [%s]' % (name, ordi, va, pe.sec_of(va)))
print('  (total named exports: %d)' % len(exps))

gpf = [e for e in exps if e[0] == 'GetPluginFactory']
if not gpf:
    print('!! no GetPluginFactory export — dumping all names above for review')
    sys.exit(0)
gva = gpf[0][1]
print('\n===== (b) TRUE GetPluginFactory body @ 0x%X =====' % gva)
for i in disasm_range(pe, md, gva, gva + 0x80):
    print(' ', annotate(pe, i))

# follow a direct jmp tail if present
import capstone
for i in disasm_range(pe, md, gva, gva + 0x10):
    if i.mnemonic == 'jmp':
        try:
            tgt = int(i.op_str, 16)
        except ValueError:
            continue
        print('\n--- tail-jmp -> 0x%X ---' % tgt)
        for j in disasm_range(pe, md, tgt, tgt + 0x80):
            print(' ', annotate(pe, j))
        break

print('\n===== (c) factory ctor 0x180194BB0 (F4.2 candidate) =====')
for i in disasm_range(pe, md, 0x180194BB0, 0x180194C60):
    print(' ', annotate(pe, i))

print('\n===== (d) factory vtable 0x1803EEF98 slots =====')
slots = vtable_slots(pe, 0x1803EEF98, 12)
NAMES = {0: 'queryInterface', 1: 'addRef', 2: 'release',
         3: 'getFactoryInfo', 4: 'numClasses/getClassCount',
         5: 'getClassInfo', 6: 'createInstance',
         7: 'getClassInfo2 (IPluginFactory2)',
         8: 'getClassInfoUnicode (IPluginFactory3)'}
for i, v in enumerate(slots):
    tag = NAMES.get(i, '?')
    ok = 'code' if looks_like_codeptr(pe, v) else 'DATA?'
    print('  slot %-2d +0x%02X  0x%016X  %-5s  %s' % (i, 8 * i, v, ok, tag))

print('\n===== (e) GUID presence check (which encoding does the image use) =====')
for nm, q in SDK_IIDS.items():
    a = find_bytes(pe, guid_bytes_encA(q))
    b = find_bytes(pe, guid_bytes_encB(q))
    print('  %-18s encA(classic-GUID)=%d hits %s   encB(4xLE-dword)=%d hits %s'
          % (nm, len(a), ['0x%X' % x for x in a[:4]], len(b), ['0x%X' % x for x in b[:4]]))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
