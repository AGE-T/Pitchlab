"""R29 S6c: the qI cluster decode + GetPluginFactory full body (U-2, leg 3).

s6b found the qI compare sites: processor-class qI = 0x18000EC9F..0x18000ED1B
(compares IPluginBase -> IComponent -> IAudioProcessor), factory qI around
0x18000F20F..0x18000F260 (IPluginFactory3/2/1), two more IPluginBase qIs at
0x1800048F3 and 0x180008E80 (controller/other classes). CID-by-index
0x18030A010 builds JUCE CIDs from the string "Octavox" at runtime
("565354|565345" prefix + 4F637457 + hex("octavox")) — no static class array.

This probe dumps (manual ranges — fn_bounds is fragmented/absent there):
 (a) processor qI 0x18000EC80..0x18000ED80 — WHAT POINTER is returned on the
     IAudioProcessor match (the subobject offset -> the vtable);
 (b) factory qI 0x18000F1D0..0x18000F2B0;
 (c) the other two IPluginBase qI functions (0x1800048C0.., 0x180008E50..);
 (d) createInstance thunk region 0x180016E80..0x180016F60;
 (e) GetPluginFactory full body 0x180194440..0x180194670.

Static only. Hash checked pre+post.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, disasm_range, annotate)

pe = PE(); check_hash(pe)
md = get_disassembler()

def dump(lo, hi, label):
    print('--- %s  (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) processor-class queryInterface =====')
dump(0x18000EC80, 0x18000ED80, 'processor qI')

print('===== (b) factory queryInterface =====')
dump(0x18000F1D0, 0x18000F2B0, 'factory qI')

print('===== (c) other IPluginBase-comparing qIs =====')
dump(0x1800048C0, 0x180004990, 'qI site A (0x1800048F3)')
dump(0x180008E50, 0x180008F20, 'qI site B (0x180008E80)')

print('===== (d) createInstance thunk region =====')
dump(0x180016E80, 0x180016F60, 'createInstance + siblings')

print('===== (e) GetPluginFactory full body =====')
dump(0x180194440, 0x180194670, 'GetPluginFactory')

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
