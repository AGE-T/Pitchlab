"""R29 S6b: COM chain walk — GUID xrefs + GetPluginFactory body (U-2, leg 2).

s6a: true GetPluginFactory @ 0x180194440 (allocates a 0x1F0-byte object,
builds "Eventide" strings); 0x180194BB0 builds a 24-byte {vftable 0x1803EEF98,
GUID} C++ object whose slot0 0x180003A90 is a DELETING DESTRUCTOR (test dl,1;
delete(this,0x18)) — NOT COM. GUID encoding = classic; IAudioProcessor IID
at .rdata 0x18038E818 (+ .data registry 0x1804A64A0).

This probe (memory-safe streaming scans):
 (a) ONE .text pass collecting every reference (rip-relative + movabs halves)
     to all SDK IIDs — the qI compare sites;
 (b) full GetPluginFactory body (fn_bounds) — the real factory vtable install;
 (c) .data registry region dump 0x1804A6100..0x1804A6520;
 (d) CID-by-index 0x18030A010; (e) createInstance slot 0x180016EA0.

Static only. Hash checked pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r29_lib import (PE, check_hash, get_disassembler, get_bulk_disassembler,
                     disasm_range, annotate,
                     fn_bounds, load_pdata, vtable_slots, looks_like_codeptr,
                     SDK_IIDS, guid_bytes_encA, find_bytes, scan_text,
                     guid_compare_sites)

pe = PE(); check_hash(pe)
md = get_disassembler()
bmd = get_bulk_disassembler()   # detail=False + skipdata for the full-.text scans

def dump_fn(va, label, maxlen=0x160):
    lo, hi = fn_bounds(va)
    if hi - lo > maxlen:
        hi = lo + maxlen
    print('--- %s  (fn 0x%X..0x%X) ---' % (label, lo, hi))
    for i in disasm_range(pe, md, lo, hi):
        print(' ', annotate(pe, i))
    print()

print('===== (a) all SDK-IID reference sites in .text (single streaming pass) =====')
for nm, q in SDK_IIDS.items():
    sites = guid_compare_sites(pe, bmd, q)
    rips = ['0x%X->0x%X' % (i.address, v) for (i, v) in sites['rip']]
    imms = ['0x%X (half 0x%X)' % (i.address, v) for (i, v) in sites['imm']]
    print('  %-18s rip-refs: %s' % (nm, rips if rips else 'NONE'))
    print('  %-18s movabs : %s' % ('', imms if imms else 'NONE'))

print('\n===== (b) FULL GetPluginFactory 0x180194440 =====')
dump_fn(0x180194440, 'GetPluginFactory', 0x230)

print('===== (c) .data registry region 0x1804A6100..0x1804A6520 =====')
for va in range(0x1804A6100, 0x1804A6520, 8):
    v = pe.read_u64(va)
    tag = ''
    if looks_like_codeptr(pe, v):
        tag = 'code'
    elif 0x180000000 <= v < 0x181000000:
        s = pe.read_cstr(v, 40)
        if s and len(s) >= 2 and all(32 <= ord(c) < 127 for c in s[:4]):
            tag = 'str "%s"' % s[:32]
    print('  0x%X: 0x%016X %s' % (va, v, tag))

print('\n===== (d) CID-by-index 0x18030A010 =====')
dump_fn(0x18030A010, 'CID-by-index', 0x100)

print('===== (e) createInstance slot 0x180016EA0 =====')
dump_fn(0x180016EA0, 'createInstance', 0x100)

pe2 = PE(); check_hash(pe2)
print('\npost-pass hash OK')
