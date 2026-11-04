"""Print disassembly around one or more VAs (default +-0x40 bytes window)."""
import json
import sys

import capstone

BIN = r"F:\VST\OV_DIS\Ox.vst3"
PE = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\pe_info.json"
pe = json.load(open(PE, encoding='utf-8'))
IMAGE_BASE = int(pe['image_base'], 16)
sections = pe['sections']
data = open(BIN, 'rb').read()


def va2fo(va):
    rva = va - IMAGE_BASE
    for s in sections:
        if s['va'] <= rva < s['va'] + s['rsize']:
            return s['ptr'] + (rva - s['va'])
    return None


def main():
    va = int(sys.argv[1], 16)
    before = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x30
    after = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x30
    lo = va - before
    fo = va2fo(lo)
    code = data[fo:fo + before + after]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    for insn in md.disasm(code, lo):
        mark = '>>' if insn.address == va else '  '
        print('%s 0x%X  %-10s %s' % (mark, insn.address, insn.mnemonic, insn.op_str))


if __name__ == '__main__':
    main()
