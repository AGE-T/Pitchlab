"""R29 static-analysis library — extends r28_lib (same target, same rules).

Target: upload/Ox.vst3  SHA-256 7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2
All R29 analysis is STATIC (the binary is never executed). Hash checked pre+post per script.

R29 mandate (from R28 closure §3 / F4.8 / F1.4):
  U-2  : real COM-vtable hunt — fix the export parser, pin the true
         GetPluginFactory, walk the factory -> class-registration array ->
         processor Create -> GUID-comparing queryInterface -> read the true
         IAudioProcessor vtable slot 17 (+0x88) = getLatencySamples.
  U-3  : m02 ids 12/13 publisher — the id arrives via a NON-IMMEDIATE
         register; new surface: the container demux thunks shift the id into
         R8D at slot-3/4 call sites (call [reg+0x18] / call [reg+0x20]),
         which the R28 scan did not cover.
"""
import sys, os, struct
sys.path.insert(0, '/home/z/my-project/octavox-work/r28/scripts')
from r28_lib import (PE, get_disassembler, disasm_range, rip_target,
                     EXPECT_SHA256, TARGET, check_hash, f32, annotate, dump,
                     load_pdata, fn_bounds, scan_calls,
                     TEXT_LO, TEXT_HI)

R29 = '/home/z/my-project/octavox-work/r29'

# ---------------- PE export directory (the s4f parser was buggy) ----------------

def export_dir(pe):
    """Properly parsed PE export directory: list of (name, va, ordinal).
    Handles the name table + ordinal table the way the PE spec defines:
        name[i] -> ordinal[i] (an INDEX into AddressOfFunctions, not the
        export ordinal); functions[ordinal] is an RVA; forwarded exports
        (RVA landing inside the export directory itself) are reported as
        ('<forwarder>', name-string, None).
    """
    d = pe.data
    pe_off = struct.unpack_from('<I', d, 0x3C)[0]
    coff = pe_off + 4
    opt = coff + 20
    opt_size = struct.unpack_from('<H', d, coff + 16)[0]
    nrva = struct.unpack_from('<I', d, opt + 108)[0]
    exp_rva, exp_size = struct.unpack_from('<II', d, opt + 112)
    if exp_rva == 0:
        return []
    def rva2fo(rva):
        for s in pe.sections:
            if s['va'] <= rva < s['va'] + max(s['vsize'], s['rsize']):
                o = rva - s['va']
                if o < s['rsize']:
                    return s['ro'] + o
        return None
    base_fo = rva2fo(exp_rva)
    (n_funcs, n_names, a_funcs, a_names, a_ords) = struct.unpack_from(
        '<IIIII', d, base_fo + 0x14)
    dll_name_rva = struct.unpack_from('<I', d, base_fo + 0xC)[0]
    out = []
    fo_names = rva2fo(a_names)
    fo_ords = rva2fo(a_ords)
    fo_funcs = rva2fo(a_funcs)
    for i in range(n_names):
        nrva = struct.unpack_from('<I', d, fo_names + 4 * i)[0]
        name = d[rva2fo(nrva):d.index(b'\0', rva2fo(nrva))].decode('ascii', 'replace')
        ordi = struct.unpack_from('<H', d, fo_ords + 2 * i)[0]
        frva = struct.unpack_from('<I', d, fo_funcs + 4 * ordi)[0]
        # forwarded? (RVA inside the export directory range)
        if exp_rva <= frva < exp_rva + exp_size:
            sfo = rva2fo(frva)
            fwd = d[sfo:d.index(b'\0', sfo)].decode('ascii', 'replace')
            out.append((name, None, ordi, fwd))
        else:
            out.append((name, pe.image_base + frva, ordi, None))
    return out

# ---------------- GUID helpers (U-2) ----------------

# DECLARE_CLASS_IID quadruples from the vendored VST3 SDK headers
# (octavox-work/vst3sdk/pluginterfaces/...). FUID(a,b,c,d) byte layout is
# NOT assumed: we search BOTH plausible encodings and report which one the
# image uses.
SDK_IIDS = {
    'IPluginFactory':    (0x7A4D811C, 0x52114A1F, 0xAED9D2EE, 0x0B43BF9F),
    'IPluginFactory2':   (0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB),
    'IPluginFactory3':   (0x4555A2AB, 0xC1234E57, 0x9B122910, 0x36878931),
    'IPluginBase':       (0x22888DDB, 0x156E45AE, 0x8358B348, 0x08190625),
    'IComponent':        (0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802),
    'IAudioProcessor':   (0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D),
}

def guid_bytes_encA(q):
    """classic GUID {D1-D2-D3-D4[8]} little-endian field order:
    LE32(D1) LE16(D2) LE16(D3) D4[0..7] — with D2 = q[1]>>16, D3 = q[1]&0xFFFF,
    i.e. treating the quadruple as (D1, D2<<16|D3, D4[0..3], D4[4..7])."""
    d1 = q[0]
    d23 = q[1]
    lo, hi = q[2], q[3]
    b = struct.pack('<IHH', d1, d23 >> 16, d23 & 0xFFFF)
    b += struct.pack('>II', lo, hi)
    return b

def guid_bytes_encB(q):
    """4 little-endian dwords in quadruple order."""
    return struct.pack('<IIII', *q)

def find_bytes(pe, needle, secnames=('.rdata', '.data', '.text')):
    """All VAs where `needle` occurs inside the named sections."""
    hits = []
    for s in pe.sections:
        if s['name'] not in secnames:
            continue
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        off = 0
        while True:
            i = blob.find(needle, off)
            if i < 0:
                break
            hits.append(pe.image_base + s['va'] + i)
            off = i + 1
    return hits

# ---------------- streaming .text scans (memory-safe: detail=False) ----------------
#
# r26_lib's get_disassembler() = {detail=True, skipdata=True}: correct but
# ~600MB RSS per 1MB of code — the full .text (3.7MB) OOMs the sandbox.
# Bulk scans here use a second disassembler with detail=False + skipdata=True
# and parse rip-targets from op_str (no operand details needed).

import re as _re
import capstone as _cs

_RIP_RE = _re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')

def get_bulk_disassembler():
    md = _cs.Cs(_cs.CS_ARCH_X86, _cs.CS_MODE_64)
    md.detail = False
    md.skipdata = True
    return md

def opstr_rip_target(insn):
    """RIP-relative target VA parsed from op_str (detail=False mode)."""
    m = _RIP_RE.search(insn.op_str)
    if not m:
        return None
    disp = int(m.group(2), 16)
    if m.group(1) == '-':
        disp = -disp
    return insn.address + insn.size + disp

def scan_text(pe, md, want_rip_ranges, want_imm64s=frozenset(), lo=TEXT_LO, hi=TEXT_HI):
    """Single streaming pass over .text. Yields (insn, kind, value) where
    kind='rip' (value = rip target in one of want_rip_ranges) or
    kind='imm' (value = a movabs immediate in want_imm64s).
    Memory-safe: lazy generator, detail=False disassembler expected."""
    for insn in md.disasm(pe.data[pe.va2fo(lo):pe.va2fo(lo) + (hi - lo)], lo):
        op = insn.op_str
        if want_rip_ranges and 'rip' in op:
            t = opstr_rip_target(insn)
            if t is not None:
                for (rlo, rhi) in want_rip_ranges:
                    if rlo <= t < rhi:
                        yield (insn, 'rip', t)
                        break
        if want_imm64s and insn.mnemonic == 'movabs':
            ops = op.split(', ')
            if len(ops) == 2 and ops[1].startswith('0x'):
                try:
                    v = int(ops[1], 16)
                except ValueError:
                    v = None
                if v is not None and v in want_imm64s:
                    yield (insn, 'imm', v)

def rip_xrefs(pe, md, lo, hi):
    """Streaming: instructions in .text whose rip operand targets [lo, hi)."""
    return list(scan_text(pe, md, [(lo, hi)]))

def guid_compare_sites(pe, md, q):
    """All .text sites referencing the GUID q (either .rdata/.data copy via
    rip-relative lea/mov, or movabs immediate halves). Returns dict
    {'rip': [(insn, target)], 'imm': [(insn, halfvalue)]}."""
    needle = guid_bytes_encA(q)
    hits = find_bytes(pe, needle)
    imms = set()
    for h in hits:
        blob = pe.read(h, 16)
        imms.add(struct.unpack('<Q', blob[:8])[0])
        imms.add(struct.unpack('<Q', blob[8:])[0])
    out = {'rip': [], 'imm': []}
    for (insn, kind, v) in scan_text(pe, md, [(h, h + 16) for h in hits], imms):
        out['rip' if kind == 'rip' else 'imm'].append((insn, v))
    return out

# ---------------- vtable reader ----------------

def vtable_slots(pe, va, n=24):
    """Read n qwords from a candidate vtable VA; None entries = not code ptrs."""
    out = []
    for i in range(n):
        v = pe.read_u64(va + 8 * i)
        out.append(v)
    return out

def looks_like_codeptr(pe, v):
    if v is None:
        return False
    return TEXT_LO <= v < TEXT_HI
