"""R29 S6h: JuceVST3Component ctor — the +0x10 field verdict (U-2, leg 8).

s6g: RTTI names the class .?AVJuceVST3Component@juce@@; the IAudioProcessor
vtable (installed at [full+8], slot0 = thunk sub-8) = 0x1803EF580;
getLatencySamples (slot 17) = 0x180003BA0 = `mov rax,[rcx+8]; ret`
(rcx = full+8 => reads [full+0x10]); getTailSamples neighbours read the same
field + a flags word at [rcx+0x10] (= full+0x18).

Contradiction to resolve: the qI answers IUnitInfo -> this+0x10 (a vptr?) —
the ctor shows what +0x10 really is (vtable install = subobject; zero/value
init = data member) and confirms which vtable lands at +8.

 (a) find `lea reg,[rip->vt]` for the four JuceVST3Component vtables
     {0x1803EF0D0, 0x1803EF580, 0x1803EFDC0, 0x1803F01E8} in .text
     -> the ctor(s) + any other installers;
 (b) dump the ctor fully (vtable installs + member inits, esp. +8/+0x10/+0x18);
 (c) scan the ctor region for stores to [reg+0x10] / [reg+0x18];
 (d) find writers of the latency field: all .text `mov [reg+8], ...` inside
     functions that ALSO reference vtable 0x1803EF580 — plus JUCE's
     setLatencySamples path hunt: functions storing a 32-bit value to
     [X+0x10] where X was validated as a JuceVST3Component.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, opstr_rip_target, fn_bounds,
                     TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

VTS = {0x1803EF0D0: 'vt(+0x18?) sub0x18', 0x1803EF580: 'vt(+0x8) IAudioProcessor',
       0x1803EFDC0: 'vt(+0x10) sub0x10', 0x1803F01E8: 'vt(+0x20) sub0x20'}

print('===== (a) lea -> vtable references in .text =====')
sites = []
for insn in bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_HI)], TEXT_LO):
    if insn.mnemonic == 'lea' and 'rip' in insn.op_str:
        t = opstr_rip_target(insn)
        if t in VTS:
            sites.append((insn.address, t))
            print('  0x%X  lea -> 0x%X  (%s)' % (insn.address, t, VTS[t]))
print('  (%d sites)' % len(sites))

print('\n===== (b) enclosing functions (ctors/installers) =====')
seen = set()
for (site, vt) in sites:
    try:
        lo, hi = fn_bounds(site)
    except Exception:
        # no pdata: walk back to a plausible prologue boundary heuristically
        lo, hi = site - 0x100, site + 0x100
    if lo in seen:
        continue
    seen.add(lo)
    print('--- fn 0x%X..0x%X (contains lea->0x%X) ---' % (lo, hi, vt))
    for i in disasm_range(pe, md, lo, min(hi, lo + 0x200)):
        print(' ', annotate(pe, i))
    print()

print('===== (c) stores to +0x10/+0x18 in those functions =====')
for lo in list(seen)[:4]:
    try:
        hi = fn_bounds(lo)[1]
    except Exception:
        continue
    for i in disasm_range(pe, md, lo, hi):
        if i.mnemonic == 'mov' and ('+ 0x10],' in i.op_str or '+ 0x18],' in i.op_str) \
           and i.op_str.split(', ')[0].startswith('qword') is False:
            pass  # covered by the full dumps above
        if i.mnemonic == 'mov' and (i.op_str.startswith('qword ptr [r') and
                                    ('+ 0x10]' in i.op_str or '+ 0x18]' in i.op_str)):
            print('  0x%X  %s %s' % (i.address, i.mnemonic, i.op_str))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
