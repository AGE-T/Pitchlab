"""R28 S4b: verify the default stub's return value + the vtable's owner class.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800055F0, 0x180005650, 'shared default stub 0x1800055F0 (raw bytes)')
print('raw bytes at 0x1800055F0:', pe.read(0x1800055F0, 16).hex())

dump(pe, md, 0x180260210, 0x180260260, 'process slot +0xA0 target 0x180260210 (head)')
dump(pe, md, 0x180313BD0, 0x180313C20, 'setupProcessing slot +0x90 target 0x180313BD0')

# find the constructor that installs vtable 0x1803F05D0
pat = struct.pack('<Q', 0x1803F05D0)
hits = []
for lo, hi in load_pdata():
    if not (0x180001000 <= lo < 0x18037D000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i, ins in enumerate(insns):
        if ins.mnemonic == 'mov' and ins.op_str.endswith('0x1803f05d0'):
            hits.append((ins.address, lo))
            # print context
            for f in insns[i:i+4]:
                print('  ctor-site 0x%X  %s %s' % (f.address, f.mnemonic, f.op_str))
            print('  ---')
print('vtable 0x1803F05D0 installed at: %s' % [hex(a) for a, _ in hits])

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
