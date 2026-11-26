"""R30 S9a: U-3 — find the m02 poller loop.

m02 = engine vtable 0x180409F60 slot 2 = fn 0x1800E1520 (this, id=rdx,
out=r8). Ids are non-immediate everywhere -> a poll loop. Signature of a
7..13 loop: a bounds check `cmp var, 0xd` (ja/jbe) or `cmp var, 0xe`
inside a backward-jump loop containing a slot-1..4 call.

Single streaming pass (detail=False) over pdata functions:
 (a) all call [reg+{0x8,0x10,0x18,0x20}] sites;
 (b) per site: id def-slice (edx for slot1/2, r8d for slot3/4, 40 insns,
     stop at call/ret/jmp, allow jcc), receiver def-slice (24 insns);
 (c) loop context: backward jumps in the same function enclosing the site;
 (d) the tight filter: non-immediate id + enclosing loop + a cmp with
     0x7..0xe constant inside the loop body.
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, TEXT_LO, TEXT_HI)
from r29_lib import load_pdata

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

CALL_RE = re.compile(r'^call +qword ptr \[(\w+)(?: \+ (0x8|0x10|0x18|0x20))?\]$')
CMP_RE = re.compile(r'^cmp +(\w+), (0x7|0x8|0x9|0xa|0xb|0xc|0xd|0xe)$')
JCC_RE = re.compile(r'^j\w+ +(0x[0-9a-f]+)$')

ID_REG = {None: ('edx', 'rdx'), '0x8': ('edx', 'rdx'), '0x10': ('edx', 'rdx'),
          '0x18': ('r8d', 'r8'), '0x20': ('r8d', 'r8')}

def defslice(insns, idx, names, maxback, stop_at_jmp=True):
    for j in range(idx - 1, max(idx - maxback, -1), -1):
        mn, op = insns[j].mnemonic, insns[j].op_str
        if mn in ('call', 'ret'):
            return None
        if mn == 'jmp' and stop_at_jmp:
            return None
        if ', ' not in op:
            continue
        dst, src = op.split(', ', 1)
        if any(dst == n or dst.endswith(' ' + n) or dst == n for n in names):
            if mn == 'mov' and src.startswith('0x'):
                try:
                    return ('imm', int(src, 16), insns[j].address)
                except ValueError:
                    pass
            if mn == 'xor':
                return ('imm', 0, insns[j].address)
            if mn in ('mov', 'lea', 'movzx', 'movsxd', 'add', 'inc'):
                return (mn, src, insns[j].address)
            return (mn, op, insns[j].address)
    return None

fns = [b for b in load_pdata() if TEXT_LO <= b[0] < TEXT_HI]
print('pdata functions: %d' % len(fns))

candidates = []
nslotcalls = 0
for (flo, fhi) in fns:
    insns = list(bmd.disasm(pe.data[pe.va2fo(flo):pe.va2fo(flo) + (fhi - flo)], flo))
    if not insns:
        continue
    # backward jumps in this function
    backjumps = []
    for k, ins in enumerate(insns):
        m = JCC_RE.match('%s %s' % (ins.mnemonic, ins.op_str))
        if m:
            t = int(m.group(1), 16)
            if flo <= t < ins.address:
                backjumps.append((t, ins.address))
    for k, ins in enumerate(insns):
        op = '%s %s' % (ins.mnemonic, ins.op_str)
        m = CALL_RE.match(op)
        if not m:
            continue
        nslotcalls += 1
        reg, disp = m.group(1), m.group(2)
        idnames = ID_REG.get(disp, ('edx', 'rdx'))
        iddef = defslice(insns, k, idnames, 40)
        if iddef and iddef[0] == 'imm':
            continue  # immediate id — already covered by R28/R29
        recvdef = defslice(insns, k, (reg, reg.replace('r', 'e', 1) if reg.startswith('r') else reg), 24)
        loops = [(t, b) for (t, b) in backjumps if t <= ins.address <= b and (b - t) < 0x400]
        if not loops:
            continue
        # bounds-check consts inside the tightest enclosing loop
        t0, b0 = min(loops, key=lambda tb: tb[1] - tb[0])
        cmps = []
        for j2 in insns:
            if t0 <= j2.address <= b0:
                cm = CMP_RE.match('%s %s' % (j2.mnemonic, j2.op_str))
                if cm:
                    cmps.append((cm.group(1), int(cm.group(2), 16)))
        if not cmps:
            continue
        candidates.append({'va': ins.address, 'fn': (flo, fhi), 'slot': disp or '0x0',
                           'recv': reg, 'recvdef': recvdef, 'iddef': iddef,
                           'loop': (t0, b0), 'cmps': cmps})

print('slot-call sites total: %d' % nslotcalls)
print('candidates (non-imm id + loop + 7..14 cmp in loop): %d' % len(candidates))
for c in candidates[:40]:
    print('  0x%X fn 0x%X..0x%X slot%s recv=%s recvdef=%s iddef=%s loop=0x%X..0x%X cmps=%s'
          % (c['va'], c['fn'][0], c['fn'][1], c['slot'], c['recv'], c['recvdef'],
             c['iddef'], c['loop'][0], c['loop'][1], c['cmps']))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
