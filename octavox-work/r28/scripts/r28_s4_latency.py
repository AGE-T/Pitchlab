"""R28 S4: host-reported latency (U-2) — VST3 component vtable slot analysis.

VST3 SingleComponentEffect vtable layout (single inheritance chain):
  FUnknown:      queryInterface(+0x00) addRef(+0x08) release(+0x10)
  IComponent:    initialize(+0x18) terminate(+0x20) getControllerClass(+0x28)
                 setIoMode(+0x30) getBusCount(+0x38) getBusInfo(+0x40)
                 getRoutingInfo(+0x48) activateBus(+0x50) setActive(+0x58)
                 setState(+0x60) getState(+0x68)
  IAudioProcessor: setBusArrangements(+0x70) getBusArrangement(+0x78)
                 canProcessSampleSize(+0x80) getLatencySamples(+0x88)
                 setupProcessing(+0x90) setProcessing(+0x98) process(+0xA0)
                 getTailSamples(+0xA8)
Dump the JuceVST3Component vtable (0x1803F05D0) and decompile the
getLatencySamples slot target (+0x88).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

VT = 0x1803F05D0
names = {0x00: 'queryInterface', 0x08: 'addRef', 0x10: 'release',
         0x18: 'initialize', 0x20: 'terminate', 0x28: 'getControllerClass',
         0x30: 'setIoMode', 0x38: 'getBusCount', 0x40: 'getBusInfo',
         0x48: 'getRoutingInfo', 0x50: 'activateBus', 0x58: 'setActive',
         0x60: 'setState', 0x68: 'getState',
         0x70: 'setBusArrangements', 0x78: 'getBusArrangement',
         0x80: 'canProcessSampleSize', 0x88: 'getLatencySamples',
         0x90: 'setupProcessing', 0x98: 'setProcessing', 0xA0: 'process',
         0xA8: 'getTailSamples', 0xB0: 'connect(IConnectionPoint)',
         0xB8: 'disconnect'}

print('=== JuceVST3Component vtable @0x%X ===' % VT)
slots = {}
for off in range(0, 0xC0, 8):
    v = pe.read_u64(VT + off)
    slots[off] = v
    nm = names.get(off, '')
    sec = pe.sec_of(v) if v else '?'
    print('  +0x%02X %-22s = 0x%X  (%s)' % (off, nm, v or 0, sec))

lat = slots.get(0x88)
print()
print('getLatencySamples slot target: 0x%X' % lat)
if lat:
    fb = None
    for a, b in load_pdata():
        if a <= lat < b:
            fb = (a, b)
            break
    print('function bounds: %s' % (repr([hex(x) for x in fb]) if fb else 'NO PDATA SCOPE'))
    if fb:
        dump(pe, md, fb[0], fb[1], 'getLatencySamples implementation')

# also dump canProcessSampleSize for signature sanity (both return uint32 via eax)
for off, nm in ((0x80, 'canProcessSampleSize'), (0xA8, 'getTailSamples')):
    t = slots.get(off)
    if t:
        fb = None
        for a, b in load_pdata():
            if a <= t < b:
                fb = (a, b)
                break
        if fb and fb[1] - fb[0] < 0x200:
            dump(pe, md, fb[0], fb[1], '%s implementation @0x%X' % (nm, t))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
