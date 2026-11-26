"""R29 S7a: m02 ids 12/13 — the demux arg-shift scan (U-3).

R28 F1.4 searched only `call [reg+0x10]` with SMALL IMMEDIATE rdx (found
ids {1,1,1,1,1,2,2,4}). But the container demux thunks SHIFT the id:
  slot3 (+0x18) = children[[this+0x10]+rdx*8]->slot1(r8d, r9)
  slot4 (+0x20) = children[[this+0x10]+rdx*8]->slot2(r8d, r9)
so at `call [reg+0x18]` / `call [reg+0x20]` sites THE ID RIDES IN R8D — a
surface the R28 scan never covered. Also: non-immediate ids (register/memory
sourced) on all four slot shapes.

This probe (single streaming .text pass, detail=False):
 (a) every call [reg+{0x8,0x10,0x18,0x20}] site: classify the id operand
     (immediate value vs register-sourced vs far) with a short backward
     def-slice; report immediates in 7..13 and ALL non-immediate cases;
 (b) focus list: `mov edx/edx-like, imm` with imm in {12,13} near any
     slot-call within the same function (the direct id-12/13 feeds);
 (c) context dumps for the strongest candidates.

Static only. Hash checked pre+post.
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

CALL_RE = re.compile(r'qword ptr \[(\w+)(?: \+ (0x[0-9a-f]+))?\]')
SLOT_OF = {'0x8': 1, '0x10': 2, '0x18': 3, '0x20': 4}

def id_def(insns, idx, reg):
    """Backward def-slice for the id register; returns a classification."""
    reg_edx = ('edx' if reg in ('rdx', 'edx', 'dx', 'dl') else
               'r8d' if reg in ('r8', 'r8d', 'r8w', 'r8b') else reg)
    look = {'edx': ('edx', 'rdx', 'dx', 'dl'), 'r8d': ('r8d', 'r8', 'r8w', 'r8b')}
    names = look.get(reg_edx, (reg_edx,))
    for j in range(idx - 1, max(idx - 22, -1), -1):
        op = insns[j].op_str
        mn = insns[j].mnemonic
        if mn in ('call', 'jmp', 'ret'):
            break
        dst = op.split(', ')[0] if ', ' in op else op
        if any(dst.endswith(n) or dst == n for n in names):
            src = op.split(', ')[-1] if ', ' in op else ''
            if mn == 'mov' and src.startswith('0x'):
                try:
                    return ('imm', int(src, 16), insns[j].address)
                except ValueError:
                    pass
            if mn == 'xor' and op.replace(' ', '').endswith(',' + names[0]):
                return ('imm', 0, insns[j].address)
            if mn in ('mov', 'lea', 'movzx', 'movsxd'):
                return ('src', src, insns[j].address)
            return ('def', mn + ' ' + op, insns[j].address)
    return ('far', None, None)

print('===== (a) slot-call census =====')
sites = {'imm': {}, 'nonimm': []}
immcount = {}
buf = []
stream = bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_HI)], TEXT_LO)
for insn in stream:
    buf.append(insn)
    if len(buf) > 40:
        buf.pop(0)
    if insn.mnemonic != 'call':
        continue
    m = CALL_RE.search(insn.op_str)
    if not m:
        continue
    off = m.group(2) or ''
    if off not in SLOT_OF:
        continue
    slot = SLOT_OF[off]
    idreg = 'edx' if slot in (1, 2) else 'r8d'
    idx = len(buf) - 1
    kind, val, defva = id_def(buf, idx, idreg)
    if kind == 'imm':
        immcount.setdefault((slot, val), 0)
        immcount[(slot, val)] += 1
        if 7 <= val <= 13:
            sites['imm'].setdefault((slot, val), []).append(insn.address)
    else:
        sites['nonimm'].append((insn.address, slot, kind, val, defva))

print('  immediate-id census (slot -> id -> count):')
for (slot, val), n in sorted(immcount.items()):
    mark = ' <<<' if 7 <= val <= 13 else ''
    print('    slot %d  id %-4d x%d%s' % (slot, val, n, mark))
print('\n  immediate ids in 7..13 at:')
for (slot, val), addrs in sorted(sites['imm'].items()):
    print('    slot %d id %d: %s' % (slot, val, ['0x%X' % a for a in addrs[:8]]))
print('\n  non-immediate id sites: %d total' % len(sites['nonimm']))
for (va, slot, kind, val, defva) in sites['nonimm'][:40]:
    print('    0x%X slot%d %-5s %s (def @ %s)'
          % (va, slot, kind, val, '0x%X' % defva if defva else '?'))

print('\n===== (b) context dumps: strongest candidates =====')
shown = 0
for (va, slot, kind, val, defva) in sites['nonimm']:
    if shown >= 5:
        break
    if kind not in ('src', 'def'):
        continue
    print('--- site 0x%X (slot %d, id %s %s) ---' % (va, slot, kind, val))
    for i in disasm_range(pe, md, va - 0x40, va + 0x10):
        print(' ', annotate(pe, i))
    print()
    shown += 1

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
