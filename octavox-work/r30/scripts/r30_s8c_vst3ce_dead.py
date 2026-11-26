"""R30 S8c: U-2 last hop, leg 3 — is the VST3ClientExtensions cast dead code?
and the FULL re-read of the broker's +8 view (the inner-qI IAudioProcessor answer).

s8b: VoxAudioProcessor (engine, broker+0x38) has 10 bases, VST3ClientExtensions
NOT among them -> dynamic_cast<VST3ClientExtensions*> appears to ALWAYS return
NULL -> the aggregation path would be dead and the 7-IID fallback (which answers
IAudioProcessor -> broker+8) would be the live path — contradicting R29 F1.7.

This probe:
 (a) scan ALL of .rdata/.data for 4-byte RVAs equal to the VST3ClientExtensions
     TD (0x4B73E0) — every hit that sits at a BCD +0 position (followed by a
     sane numContained + PMD) identifies a class that DERIVES it;
 (b) for every hit: reconstruct the owning hierarchy (the BCD's pCHD field ->
     CHD -> base array -> the most-derived TD name);
 (c) the broker +8 view vtable 0x1803EF580: 24 slots with code-ptr validation,
     the qword at +0x58 (is it really a COL? sig check), and slot 17 (+0x88)
     target disassembled;
 (d) the same full re-read for the other five broker views (0x1803EF8A0,
     0x1803EFDC0, 0x1803EF0D0, 0x1803F01E8, 0x1803F0210): slot counts,
     COL sigs, slot-17 targets.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r30_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     vtable_slots, looks_like_codeptr, parse_col,
                     parse_type_descriptor, demangle_msvc, parse_chd,
                     TD_DST, TEXT_LO, TEXT_HI)

pe = PE(); check_hash(pe)
md = get_disassembler()
IB = pe.image_base

def dump(lo, hi, label):
    print('--- %s (0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) who derives VST3ClientExtensions? (BCD scan for TD 0x1804B73E0) =====')
td_rva = TD_DST - IB
hits = []
for s in pe.sections:
    if s['name'] not in ('.rdata', '.data', '_RDATA'):
        continue
    blob = pe.data[s['ro']:s['ro'] + s['rsize']]
    off = 0
    while True:
        i = blob.find(struct.pack('<I', td_rva), off)
        if i < 0:
            break
        va = IB + s['va'] + i
        hits.append(va)
        off = i + 4
print('  raw u32-RVA hits for TD 0x%X: %d' % (TD_DST, len(hits)))
bcds = []
for va in hits:
    # BCD plausibility: at +4 numContained (small), +8 mdisp, +0xC pdisp==-1
    # (non-virtual) or >=0, +0x18 pCHD RVA in range
    nc = pe.read_u32(va + 4)
    pdisp = struct.unpack('<i', pe.read(va + 0xC, 4) or b'\xff' * 4)[0]
    chd_rva = pe.read_u32(va + 0x18)
    ok = (nc is not None and nc < 32 and pdisp >= -1 and chd_rva is not None
          and 0x37D000 <= chd_rva < 0x4CD000)
    print('  @0x%X: numContained=%s pdisp=%d pCHD_rva=0x%X %s'
          % (va, nc, pdisp, chd_rva or 0, 'BCD-PLAUSIBLE' if ok else ''))
    if ok:
        bcds.append(va)
print()

print('===== (b) owning hierarchies of the plausible BCDs =====')
for va in bcds:
    chd_va = IB + pe.read_u32(va + 0x18)
    chd = parse_chd(pe, chd_va)
    if not chd:
        continue
    # walk the base array; entry 0 = most-derived of THIS hierarchy
    md_rva = pe.read_u32(chd['base_array'])
    if md_rva is None:
        continue
    bcd0_va = IB + md_rva
    td0_rva = pe.read_u32(bcd0_va)
    td0 = parse_type_descriptor(pe, IB + td0_rva) if td0_rva else None
    mdisp = struct.unpack('<i', pe.read(va + 8, 4) or b'\0' * 4)[0]
    ncont = pe.read_u32(va + 4)
    print('  BCD@0x%X (mdisp=%+#x ncont=%d) belongs to hierarchy of: %s'
          % (va, mdisp, ncont, demangle_msvc(td0['name']) if td0 else '?'))
print()

print('===== (c) broker +8 view vtable 0x1803EF580: full re-read =====')
slots = vtable_slots(pe, 0x1803EF580, 24)
for i, v in enumerate(slots):
    tag = ''
    if v is None:
        tag = '(unreadable)'
    elif looks_like_codeptr(pe, v):
        tag = 'CODE'
    else:
        sec = pe.sec_of(v)
        tag = 'DATA(%s)' % sec if sec else 'PTR=0x%X' % v
    print('  slot%2d (+0x%03X): 0x%-12X %s' % (i, 8 * i, v or 0, tag))
print()
q58 = pe.read_u64(0x1803EF580 + 0x58)
if q58 and 0x18037D000 <= q58 < 0x1804A5000:
    col = parse_col(pe, q58)
    if col:
        td = parse_type_descriptor(pe, col['td'])
        print('  [+0x58]=0x%X parses as COL: sig=%d off=%d td=%s'
              % (q58, col['sig'], col['offset'],
                 demangle_msvc(td['name']) if td else '?'))
    else:
        print('  [+0x58]=0x%X NOT a parseable COL' % q58)
else:
    print('  [+0x58]=0x%X not a .rdata ptr' % (q58 or 0))
print()
s17 = slots[17] if len(slots) > 17 else None
print('  slot 17 (+0x88) = 0x%X' % (s17 or 0))
if s17 and looks_like_codeptr(pe, s17):
    dump(s17, s17 + 0x40, 'slot17 body')
print()

print('===== (d) all six broker views: boundaries + slot 17 =====')
for vt in (0x1803EF8A0, 0x1803EF580, 0x1803EFDC0, 0x1803EF0D0,
           0x1803F01E8, 0x1803F0210):
    sl = vtable_slots(pe, vt, 24)
    ncode = 0
    for i, v in enumerate(sl):
        if v and looks_like_codeptr(pe, v):
            ncode = i + 1
        elif v is not None and not looks_like_codeptr(pe, v):
            break
    col_ptr = pe.read_u64(vt - 8)
    col = parse_col(pe, col_ptr) if col_ptr and 0x18037D000 <= col_ptr < 0x1804A5000 else None
    td = parse_type_descriptor(pe, col['td']) if col else None
    s17 = sl[17] if len(sl) > 17 else None
    print('  vt 0x%X: leading-code-slots=%d COL=%s (%s) slot17=0x%X%s'
          % (vt, ncode,
             ('0x%X' % col_ptr) if col_ptr else 'none',
             demangle_msvc(td['name']) if td else '-',
             s17 or 0,
             ' CODE' if s17 and looks_like_codeptr(pe, s17) else ''))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
