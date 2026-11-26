"""R29 S6e: full processor qI decode + vtable hunt via whole-image pointer scan (U-2, leg 5).

s6d: the qI 0x18000EC90 has a struct-return ABI (hidden rdx slot: status@0,
?@8, 0x180015F10@0x10) and continues past 0x18000EE3C with a vendor-GUID
block ("TideOctW..." magic) reading [rcx+0x48]. No adjustor thunks target it;
no .rdata/.data/.text qword equals it — so WHO references it?

This probe:
 (a) full dump 0x18000EC90..0x18000EF00 (complete qI incl. vendor block);
 (b) dump 0x180015F10..0x180015F70 (the constant every success path returns);
 (c) WHOLE-IMAGE scan for qwords 0x18000EC90 and 0x180015F10 (any section,
     incl. .reloc-free raw hits) -> vtable / pointer-table candidates;
 (d) direct callers of 0x18000EC90 (scan_calls);
 (e) MI-ctor pattern hunt: functions storing [rcx+8] from a rip-lea within
     a few instructions (the vtable installs of the processor class) —
     report the lea targets (candidate vtables) with their slot-0..17.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate, scan_calls, opstr_rip_target,
                     looks_like_codeptr, vtable_slots, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()

print('===== (a) FULL processor qI 0x18000EC90..0x18000EF00 =====')
for i in disasm_range(pe, md, 0x18000EC90, 0x18000EF00):
    print(' ', annotate(pe, i))

print('\n===== (b) the constant 0x180015F10 =====')
for i in disasm_range(pe, md, 0x180015F10, 0x180015F70):
    print(' ', annotate(pe, i))

print('\n===== (c) whole-image qword scan =====')
for needle in (0x18000EC90, 0x180015F10, 0x18000EC90 + 0):
    hits = []
    raw = struct.pack('<Q', needle)
    off = 0
    while True:
        i = pe.data.find(raw, off)
        if i < 0:
            break
        va = pe.fo2va(i)
        if va is not None:
            hits.append(va)
        off = i + 1
    print('  qword 0x%X found at VAs: %s' % (needle, ['0x%X [%s]' % (h, pe.sec_of(h)) for h in hits] or 'NOWHERE'))

print('\n===== (d) direct callers of 0x18000EC90 =====')
for (site, fn) in scan_calls(pe, md, 0x18000EC90):
    print('  call at 0x%X inside fn 0x%X' % (site, fn))

print('\n===== (e) MI-ctor pattern: lea rip -> store [rcx+8] within 6 insns =====')
window = []
hits = []
for insn in bmd.disasm(pe.data[pe.va2fo(TEXT_LO):pe.va2fo(TEXT_HI)], TEXT_LO):
    window.append(insn)
    if len(window) > 7:
        window.pop(0)
    if insn.mnemonic == 'mov' and insn.op_str.startswith('qword ptr [rcx + 8],'):
        for w in window[:-1]:
            if w.mnemonic == 'lea' and 'rip' in w.op_str:
                t = opstr_rip_target(w)
                if t is not None and 0x180380000 <= t < 0x1804C0000:
                    hits.append((insn.address, t))
                    print('  store@0x%X  <- lea 0x%X (0x%X)' % (insn.address, w.address, t))
                    break
print('  (%d MI-ctor [rcx+8] installs)' % len(hits))

print('\n===== (f) slot-0/qI sanity for the found vtables =====')
seen = set()
for (site, vt) in hits:
    if vt in seen:
        continue
    seen.add(vt)
    s0 = pe.read_u64(vt)
    s17 = None
    try:
        s17 = pe.read_u64(vt + 0x88)
    except Exception:
        pass
    print('  vtable 0x%X: slot0=0x%X  slot17(+0x88)=%s' % (vt, s0, '0x%X' % s17 if s17 else '?'))

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
