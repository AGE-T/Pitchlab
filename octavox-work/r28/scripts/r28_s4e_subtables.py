"""R28 S4e: dump all sub-vtables of the component object with COM slot names,
plus the thunk cluster 0x180018560..0x1800185A0.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import PE, check_hash, get_disassembler, dump

pe = PE(); check_hash(pe)
md = get_disassembler()

names = {0x00: 'queryInterface', 0x08: 'addRef', 0x10: 'release',
         0x18: 'initialize', 0x20: 'terminate', 0x28: 'getControllerClass',
         0x30: 'setIoMode', 0x38: 'getBusCount', 0x40: 'getBusInfo',
         0x48: 'getRoutingInfo', 0x50: 'activateBus', 0x58: 'setActive',
         0x60: 'setState', 0x68: 'getState',
         0x70: 'setBusArrangements', 0x78: 'getBusArrangement',
         0x80: 'canProcessSampleSize', 0x88: 'getLatencySamples',
         0x90: 'setupProcessing', 0x98: 'setProcessing', 0xA0: 'process',
         0xA8: 'getTailSamples'}

for vt, tag in ((0x1803F04E8, 'sub-vtable @this+0x150'),
                (0x1803F0560, 'sub-vtable @this+0x158 region A'),
                (0x1803F0570, 'sub-vtable @this+0x158 region B'),
                (0x1803F0580, 'sub-vtable @this+0x2600/0x2810')):
    print('=== %s @0x%X ===' % (tag, vt))
    for off in range(0, 0xB0, 8):
        v = pe.read_u64(vt + off)
        nm = names.get(off, '')
        sec = pe.sec_of(v) if v else '?'
        print('  +0x%02X %-22s = 0x%X  (%s)' % (off, nm, v or 0, sec))
    print()

dump(pe, md, 0x180018560, 0x1800185A0, 'thunk cluster 0x180018560..0x1800185A0')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
