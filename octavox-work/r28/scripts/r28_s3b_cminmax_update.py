"""R28 S3b: c_minmax full add_fn + update function via executor slot scan.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata)

pe = PE(); check_hash(pe)
md = get_disassembler()

dump(pe, md, 0x1800D16E0, 0x1800D1800, 'c_minmax add_fn continued (keys + wiring)')
dump(pe, md, 0x18013AC5D, 0x18013AD80, 'c_minmax variant 0x18013ABD0 continued')

# find executor call sites for the c_minmax slot (+0x6E0) and c_minmax2 (+0xB088)
print('=== executor sites loading slot +0x6E0 (c_minmax) ===')
for lo, hi in load_pdata():
    if not (0x180100000 <= lo < 0x180130000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i, ins in enumerate(insns):
        if ins.mnemonic == 'lea' and '0x6e0]' in ins.op_str and 'rbx' in ins.op_str:
            # print the following 6 instructions (the call)
            for f in insns[i:i+7]:
                print('  0x%X  %s %s' % (f.address, f.mnemonic, f.op_str))
            print('  ---')

print('=== executor sites loading slot +0xB088 (c_minmax2) ===')
for lo, hi in load_pdata():
    if not (0x180100000 <= lo < 0x180130000):
        continue
    insns = list(disasm_range(pe, md, lo, hi))
    for i, ins in enumerate(insns):
        if ins.mnemonic == 'lea' and '0xb088]' in ins.op_str and 'rbx' in ins.op_str:
            for f in insns[i:i+7]:
                print('  0x%X  %s %s' % (f.address, f.mnemonic, f.op_str))
            print('  ---')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
