"""R30 S8d: U-2 FINAL — the true getLatencySamples body.

s8c: the broker +8 view (0x1803EF580, 11 slots) IS the IAudioProcessor
vtable (SDK: 3 FUnknown + 8 own methods, getLatencySamples = own #4 =
slot 6 = +0x30). The R26-R29 "slot 17 (+0x88)" premise was a phantom
(that linear offset lands in the adjacent Steinberg::String vtable).

This probe:
 (a) disassemble 0x18000E540 (slot 6) FULLY — the host-facing latency;
 (b) disassemble all 11 slots of the IAudioProcessor view for shape
     cross-identification (process() must be the huge SIMD one,
     canProcessSampleSize the kResultTrue-on-32 switch, etc.);
 (c) follow any engine-field read in (a) to the writer of that field
     (the plugin-side setLatencySamples producer).

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     fn_bounds, vtable_slots)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

IAPI_VT = 0x1803EF580
NAMES = ['queryInterface', 'addRef', 'release', 'setBusArrangements',
         'getBusArrangement', 'canProcessSampleSize', 'getLatencySamples',
         'setupProcessing', 'setProcessing', 'process', 'getTailSamples']

print('===== (a) IAudioProcessor view 0x1803EF580: all 11 slots =====')
slots = vtable_slots(pe, IAPI_VT, 11)
for i, v in enumerate(slots):
    print('  slot %2d (+0x%02X) %-22s = 0x%X' % (i, 8 * i, NAMES[i], v or 0))
print()

print('===== (b) getLatencySamples 0x18000E540 (slot 6) =====')
b = fn_bounds(slots[6])
lo, hi = b if b else (slots[6], slots[6] + 0x100)
print('  bounds: 0x%X..0x%X' % (lo, hi))
dump(lo, hi, 'getLatencySamples')

print('===== (c) shape cross-check: slot sizes =====')
for i, v in enumerate(slots):
    bb = fn_bounds(v)
    if bb:
        print('  %-22s 0x%X..0x%X (%d bytes)' % (NAMES[i], bb[0], bb[1], bb[1] - bb[0]))
    else:
        print('  %-22s 0x%X (no pdata bounds)' % (NAMES[i], v or 0))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
