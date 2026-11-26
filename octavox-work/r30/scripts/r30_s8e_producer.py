"""R30 S8e: U-2 producer — who writes VoxAudioProcessor+0x3C?

s8d: host latency = max(0, [engine+0x3C]) (engine = VoxAudioProcessor at
broker+0x38). setupProcessing (broker 0x18000E550) calls the ENGINE's
vtable slot 20 (+0xa0) after storing the ProcessSetup — the engine-side
prepare, the prime candidate for the latency computation.

This probe:
 (a) engine primary vtable 0x1803F05D0: slots 0..23 with sizes;
 (b) disassemble engine vtable slot 20 (+0xa0) — the engine-side prepare;
 (c) streaming .text scan: every 32-bit store to [reg+0x3c] (mov dword
     ptr [r+0x3c], x) with enclosing pdata function;
 (d) 128-bit stores covering +0x3C (movups [r+0x30]) listed too;
 (e) classify each writer-enclosing fn: VoxAudioProcessor family member?
     (reachable from the ten engine vtables / called by the wrappers).

Static only. Hash checked pre+post.
"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, fn_bounds, vtable_slots,
                     looks_like_codeptr, TEXT_LO, TEXT_HI, opstr_rip_target)
from r29_lib import scan_text

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) engine primary vtable 0x1803F05D0: slots 0..23 =====')
slots = vtable_slots(pe, 0x1803F05D0, 24)
for i, v in enumerate(slots):
    sz = ''
    if v and looks_like_codeptr(pe, v):
        bb = fn_bounds(v)
        sz = ('%d B' % (bb[1] - bb[0])) if bb else '?'
    print('  slot %2d (+0x%03X): 0x%-12X %s' % (i, 8 * i, v or 0, sz))
print()

print('===== (b) engine vtable slot 20 (+0xa0) =====')
s20 = slots[20]
print('  slot20 = 0x%X' % s20)
bb = fn_bounds(s20)
lo, hi = bb if bb else (s20, s20 + 0x200)
print('  bounds 0x%X..0x%X (%d B)' % (lo, hi, hi - lo))
dump(lo, min(hi, lo + 0x180), 'engine slot20 (first 0x180 B)')

print('===== (c) 32-bit stores to [reg+0x3c] =====')
pat = re.compile(r'^mov +dword ptr \[(\w+) \+ 0x3c\], (\w+)$')
hits = []
for insn in bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_LO) + (TEXT_HI - TEXT_LO)], TEXT_LO):
    m = pat.match('%s %s' % (insn.mnemonic, insn.op_str))
    if m:
        hits.append((insn.address, m.group(1), m.group(2)))
print('  total: %d' % len(hits))
fns = {}
for (va, base, src) in hits:
    fb = fn_bounds(va)
    key = (fb[0], fb[1]) if fb else (None, None)
    fns.setdefault(key, []).append((va, base, src))
for key in sorted(fns, key=lambda k: (k[0] or 0)):
    vlist = fns[key]
    flo, fhi = key
    print('  fn %s (%d hits):' % ('0x%X..0x%X' % (flo, fhi) if flo else 'NO-BOUNDS',
          len(vlist)))
    for (va, base, src) in vlist[:6]:
        print('    0x%X: mov [%s+0x3c], %s' % (va, base, src))
    if len(vlist) > 6:
        print('    ... +%d more' % (len(vlist) - 6))
print()

print('===== (d) 128-bit stores to [reg+0x30] (cover +0x3c) =====')
pat2 = re.compile(r'^movups +xmmword ptr \[(\w+) \+ 0x30\], xmm\d$')
n2 = 0
for insn in bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_LO) + (TEXT_HI - TEXT_LO)], TEXT_LO):
    m = pat2.match('%s %s' % (insn.mnemonic, insn.op_str))
    if m:
        n2 += 1
        if n2 <= 12:
            print('  0x%X  (%s)' % (insn.address, fn_bounds(insn.address) or ''))
print('  total movups [r+0x30]: %d' % n2)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
