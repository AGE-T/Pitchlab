"""Scan .text for memory operands using given struct displacements.

Reports, per target displacement: instruction sites, containing function,
and whether the memory operand is the destination (write) or a source (read).

Usage: python disp_scan.py 0x4320 0x4370 0x44a0 ...
"""
import bisect
import csv
import json
import sys

import capstone

BIN = r"F:\VST\OV_DIS\Ox.vst3"
PE = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\pe_info.json"
FUNCS = r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\functions.csv"

pe = json.load(open(PE, encoding='utf-8'))
IMAGE_BASE = int(pe['image_base'], 16)
sections = pe['sections']
TEXT = next(s for s in sections if s['name'] == '.text')
TEXT_LO = IMAGE_BASE + TEXT['va']
data = open(BIN, 'rb').read()

funcs = []
with open(FUNCS, newline='', encoding='utf-8') as fh:
    for row in csv.DictReader(fh):
        funcs.append((int(row['start_va'], 16), int(row['end_va'], 16)))
funcs.sort()
fstarts = [f[0] for f in funcs]


def func_of(va):
    i = bisect.bisect_right(fstarts, va) - 1
    if i >= 0 and funcs[i][0] <= va < funcs[i][1]:
        return funcs[i]
    return None


def main():
    targets = set(int(a, 16) for a in sys.argv[1:])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    md.skipdata = True
    code = data[TEXT['ptr']:TEXT['ptr'] + TEXT['rsize']]
    hits = {t: [] for t in targets}
    for insn in md.disasm(code, TEXT_LO):
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        for idx, op in enumerate(ops):
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp in hits:
                # destination if this operand is the first one of a writing insn
                write = idx == 0 and insn.mnemonic.split()[0] in (
                    'mov', 'movss', 'movsd', 'movups', 'movaps', 'movdqu',
                    'movdqa', 'movd', 'movq', 'lea', 'add', 'sub', 'and',
                    'or', 'xor', 'inc', 'dec', 'mul', 'div', 'imul', 'idiv',
                    'shl', 'shr', 'sar', 'not', 'neg')
                hits[op.mem.disp].append((insn.address, insn.mnemonic, insn.op_str, write))
    for t in sorted(targets):
        sites = hits[t]
        print('==== disp 0x%X : %d sites' % (t, len(sites)))
        byfn = {}
        for (a, mn, ops, w) in sites:
            fn = func_of(a)
            key = fn[0] if fn else 0
            byfn.setdefault(key, []).append((a, mn, ops, w))
        for key in sorted(byfn):
            ss = byfn[key]
            nw = sum(1 for s in ss if s[3])
            print('  func 0x%X  sites=%d  writes=%d' % (key, len(ss), nw))
            for (a, mn, ops, w) in ss[:8]:
                print('      0x%X %-8s %s%s' % (a, mn, ops, '   <WRITE>' if w else ''))
            if len(ss) > 8:
                print('      ... %d more' % (len(ss) - 8))


if __name__ == '__main__':
    main()
