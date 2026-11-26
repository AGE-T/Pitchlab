"""R30 static-analysis library — extends r29_lib (same target, same rules).

Target: upload/Ox.vst3  SHA-256 7bb260f66bb9c1b21f27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2
All R30 analysis is STATIC (the binary is never executed). Hash checked pre+post per script.

R30 mandate (from R29 closure / R29_FRESH_FINDINGS F1.8 + F2.3):
  U-2  : the LAST HOP — resolve the __RTDynamicCast call in the outer qI
         (0x18000C230 @ 0x18000C272: rcx=engine[broker+0x38], rdx=0,
          r8=SrcTypeDescriptor*, r9=TargetTypeDescriptor*, isRef=0),
         identify the casted subobject, its qI (vtable slot +0x10 per the
         dispatch thunk 0x180016E14), its IAudioProcessor branch, and the
         returned object's vtable slot 17 (+0x88) = getLatencySamples body
         = the numeric latency the host receives.
  U-3  : the 1337-site non-immediate census narrowed — m02 = engine vtable
         0x180409F60 slot 2 = fn 0x1800E1520 (this, id=rdx, out=r8); ids
         12/13 = the +/-cents telemetry read-outs (engine+0xA58C/+0xA588).
         Find the actual caller (expected: a read-out loop with the id in a
         register/counter), backward-slice the id producer.

New in r30_lib: MSVC x64 RTTI walkers (TypeDescriptor / CompleteObjectLocator /
ClassHierarchyDescriptor / BaseClassDescriptor), all-RVA-relative like the
image itself.
"""
import sys, os, struct
sys.path.insert(0, '/home/z/my-project/octavox-work/r29/scripts')
from r29_lib import (PE, get_disassembler, get_bulk_disassembler, disasm_range,
                     rip_target, opstr_rip_target, EXPECT_SHA256, TARGET,
                     check_hash, f32, annotate, dump, load_pdata, fn_bounds,
                     scan_calls, scan_text, vtable_slots, looks_like_codeptr,
                     TEXT_LO, TEXT_HI, find_bytes, guid_bytes_encA, SDK_IIDS)

R30 = '/home/z/my-project/octavox-work/r30'

# ---- R29-established anchor table (R29_FRESH_FINDINGS F1.x) ----
BROKER             = 0x18000BEF0  # juce::JuceVST3Component ctor (0x480 B)
BROKER_QI_OUTER    = 0x18000C230  # outer qI: aggregation first, then 7-IID fallback
BROKER_QI_INNER    = 0x18000EC90  # inner 7-IID fallback (struct-return ABI)
ENGINE_AGG_CTOR    = 0x1801A7620  # 0x28A0-byte Eventide engine ctor (broker+0x38)
RT_DYNAMIC_CAST    = 0x180160D5C  # __RTDynamicCast
QI_DISPATCH_THUNK  = 0x180016E14  # mov rax,[rcx]; jmp [rax+0x10] (walk-result qI)
TD_SRC             = 0x1804B7448  # TypeDescriptor passed in r8 ([TD]=type_info vftbl 0x18037FE80)
TD_DST             = 0x1804B73E0  # TypeDescriptor passed in r9 (same type_info vftbl)
TYPE_INFO_VFTABLE  = 0x18037FE80  # shared by both TDs (slot0 = deleting dtor 0x18015EAD0)

# ---- R28-established m02 anchors ----
ENGINE_VTABLE      = 0x180409F60  # dsp engine (m01/m02) — NOT the 0x28A0 aggregation host
M02_FN             = 0x1800E1520  # (this, id=rdx, out=r8); jump table 0x1800E1890, ids 7..13
M02_CASE12         = 0x1800E1751  # -cents(engine+0xA58C)
M02_CASE13         = 0x1800E17E1  # +cents(engine+0xA588)

# engine vtables installed by ENGINE_AGG_CTOR (R29 s6h/s6j; +0x00/+0x150/+0x158/+0x2600...)
ENGINE_AGG_VTABLES = [0x1803F05D0, 0x1803F04E8, 0x1803F03B8, 0x1803F05A0,
                      0x1803F0478, 0x1803F23C8, 0x1803F0E68, 0x1803F0B00,
                      0x1803F2A78, 0x1803F2948]


# ---------------- MSVC x64 RTTI ----------------

def rva2va(pe, rva):
    return pe.image_base + rva


def parse_type_descriptor(pe, va):
    """MSVC TypeDescriptor: {vftable(8), spare(8), name[]}."""
    vft = pe.read_u64(va)
    spare = pe.read_u64(va + 8)
    name = pe.read_cstr(va + 0x10, 256)
    return {'va': va, 'vftable': vft, 'spare': spare, 'name': name}


def parse_col(pe, va):
    """RTTI Complete Object Locator (x64: RVAs relative to image base).
    +0 sig, +4 offset, +8 cdOffset, +0xC pTypeDescriptor(RVA),
    +0x10 pClassDescriptor(RVA)."""
    b = pe.read(va, 0x14)
    if not b or len(b) < 0x14:
        return None
    sig, off, cdoff, td_rva, chd_rva = struct.unpack('<IIIII', b)
    return {'va': va, 'sig': sig, 'offset': off, 'cdoff': cdoff,
            'td': rva2va(pe, td_rva), 'chd': rva2va(pe, chd_rva)}


def parse_chd(pe, va):
    """Class Hierarchy Descriptor: +0 sig, +4 attrs, +8 numBases, +0xC pBaseArray(RVA)."""
    b = pe.read(va, 0x10)
    if not b or len(b) < 0x10:
        return None
    sig, attrs, nbases, arr_rva = struct.unpack('<IIII', b)
    return {'va': va, 'sig': sig, 'attrs': attrs, 'nbases': nbases,
            'base_array': rva2va(pe, arr_rva)}


def parse_bcd(pe, va):
    """Base Class Descriptor: +0 pTypeDescriptor(RVA), +4 numContained,
    +8 PMD.mdisp(i32), +0xC PMD.pdisp(i32), +0x10 PMD.vdisp(i32),
    +0x14 attrs(u32), +0x18 pCHD(RVA)."""
    b = pe.read(va, 0x1C)
    if not b or len(b) < 0x1C:
        return None
    td_rva, ncont, mdisp, pdisp, vdisp, attrs, chd_rva = struct.unpack('<IIiiiII', b)
    return {'va': va, 'td': rva2va(pe, td_rva), 'num_contained': ncont,
            'mdisp': mdisp, 'pdisp': pdisp, 'vdisp': vdisp, 'attrs': attrs,
            'chd': rva2va(pe, chd_rva)}


def vtable_col(pe, vt_va):
    """The COL of a vtable (qword at vt-8), parsed; None if absent/invalid."""
    col_ptr = pe.read_u64(vt_va - 8)
    if not col_ptr or not (0x18037D000 <= col_ptr < 0x1804A5000):
        return None
    return parse_col(pe, col_ptr)


def class_bases(pe, vt_va, with_names=True):
    """Full base list of the class owning vtable vt_va:
    {col, chd, bases:[BCD...]} via COL -> CHD -> BaseClassArray -> BCDs."""
    col = vtable_col(pe, vt_va)
    if col is None:
        return None
    chd = parse_chd(pe, col['chd'])
    if chd is None:
        return None
    out = []
    for i in range(chd['nbases']):
        bcd_rva = pe.read_u32(chd['base_array'] + 4 * i)
        if bcd_rva is None:
            break
        bcd = parse_bcd(pe, rva2va(pe, bcd_rva))
        if bcd is None:
            break
        if with_names:
            td = parse_type_descriptor(pe, bcd['td'])
            bcd['td_name'] = td['name'] if td else None
        out.append(bcd)
    return {'col': col, 'chd': chd, 'bases': out}


def demangle_msvc(name):
    """Minimal MSVC name pretty-print for .?AV<class@@ns@...@@ style TD names."""
    if not name:
        return None
    if name.startswith('.?AV') and name.endswith('@@'):
        inner = name[4:-2]
        parts = inner.split('@')
        parts = [p for p in parts if p]
        # last part = the class, earlier parts = namespaces (reversed)
        return '::'.join(reversed(parts)) if parts else inner
    return name


def td_matches(name, needle):
    """Case-insensitive substring test on the demangled TD name."""
    if not name:
        return False
    return needle.lower() in demangle_msvc(name).lower()
