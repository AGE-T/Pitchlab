"""R29 S6j: the ENGINE-side IAudioProcessor — U-2 closure (leg 10).

s6i: 0x18037FE80 = {pred=0x18015EAD0, NULL}; the outer qI walks the engine's
RTTI graph with that predicate, then queries the result via slot-2 (thunk
0x180016E14). The engine (0x28A0-byte class, ctor 0x1801A7620) installs
vtables {0x1803F05D0, 0x1803F04E8, 0x1803F03B8, 0x1803F05A0, 0x1803F0478}
(consecutive at 0x1801A784F..0x1801A7883) + more later; its qI-family lives
at {0x180004490, 0x1800048E0, 0x180004480, 0x1800074B0} with adjustor subs
{0x10,0x18,0x30,0x38,0x40,0x68,0x70,0x78}.

This probe:
 (a) predicate 0x18015EAD0;
 (b) the engine qI 0x180004490 FULL (interface map: offset per IID);
 (c) qI-family heads 0x1800048E0 / 0x180004480 / 0x1800074B0;
 (d) engine ctor vtable-install context 0x1801A7840..0x1801A7890 (offsets);
 (e) slot 17 (+0x88) of every engine vtable candidate + the primary
     0x1803F05D0 — getLatencySamples bodies.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     vtable_slots, looks_like_codeptr)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) predicate 0x18015EAD0 =====')
dump(0x18015EAD0, 0x18015EB60, 'predicate')

print('===== (b) engine qI 0x180004490 (FULL) =====')
dump(0x180004490, 0x180004740, 'engine qI')

print('===== (c) qI-family heads =====')
dump(0x1800048E0, 0x180004990, 'qI 0x1800048E0')
dump(0x180004480, 0x180004560, 'qI 0x180004480')
dump(0x1800074B0, 0x180007590, 'qI 0x1800074B0')

print('===== (d) engine ctor install context =====')
dump(0x1801A7840, 0x1801A7890, 'ctor vtable installs')

print('===== (e) engine vtables: slot 17 =====')
for vt in (0x1803F05D0, 0x1803F04E8, 0x1803F03B8, 0x1803F05A0, 0x1803F0478,
           0x1803F23C8, 0x1803F0E68, 0x1803F0B00, 0x1803F2A78, 0x1803F2948):
    slots = vtable_slots(pe, vt, 20)
    code = sum(1 for v in slots[:18] if looks_like_codeptr(pe, v))
    s17 = slots[17]
    print('  vt 0x%X: code-slots %d/18, slot17(+0x88) = %s'
          % (vt, code, '0x%X' % s17 if s17 else '?'))
    if looks_like_codeptr(pe, s17):
        for i in disasm_range(pe, md, s17, s17 + 0x40):
            print('     ', annotate(pe, i))
        print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
