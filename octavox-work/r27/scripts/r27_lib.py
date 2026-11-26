"""R27 static-analysis library — extends r26_lib (same target, same rules).

Target: upload/Ox.vst3  SHA-256 7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2
All R27 analysis is STATIC (the binary is never executed). Hash checked pre+post per script.
"""
import sys, os, struct, json
sys.path.insert(0, '/home/z/my-project/octavox-work/r26/audit/scripts')
from r26_lib import (PE, get_disassembler, disasm_range, rip_target,
                     EXPECT_SHA256, TARGET)
import capstone

R26 = '/home/z/my-project/octavox-work/r26/audit'
R27 = '/home/z/my-project/octavox-work/r27'

def check_hash(pe):
    assert pe.sha256 == EXPECT_SHA256, 'HASH MISMATCH: %s' % pe.sha256
    return pe.sha256

def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]

def annotate(pe, insn):
    """One annotated disasm line: rip-target + str/f64/f32 interpretations."""
    try:
        t = rip_target(insn)
    except Exception:
        t = None
    extra = ''
    if t is not None:
        s = pe.read_cstr(t, 64)
        if s and s.isprintable() and len(s) > 2:
            extra = '  ; "%s"' % s[:56]
        else:
            b = pe.read(t, 8)
            if b:
                f64 = struct.unpack('<d', b)[0]
                f32v = struct.unpack('<f', b[:4])[0]
                q = struct.unpack('<Q', b)[0]
                cand = []
                if 1e-8 < abs(f64) < 1e8 and (abs(f64 - round(f64)) > 1e-9 or f64 in (0.5, 2.0, 1.0)):
                    cand.append('f64=%r' % f64)
                if 1e-8 < abs(f32v) < 1e8 and abs(f32v - round(f32v)) > 1e-6:
                    cand.append('f32=%r' % f32v)
                if not cand:
                    cand.append('q=0x%X' % q)
                extra = '  ; ' + ' '.join(cand)
    # immediate float operands
    try:
        ops = insn.operands
    except Exception:
        ops = []
    for op in ops:
        if op.type == capstone.x86.X86_OP_IMM:
            v = op.imm & 0xFFFFFFFFFFFFFFFF
            if insn.mnemonic in ('mov', 'cmp'):
                if v <= 0xFFFFFFFF:
                    fv = struct.unpack('<f', struct.pack('<I', v))[0]
                    if 1e-6 < abs(fv) < 1e6 and abs(fv - round(fv)) > 1e-5:
                        extra += '  ; imm_f32=%r' % fv
                    dv = struct.unpack('<d', struct.pack('<Q', v))[0] if v > 0xFFFFFFFF else None
                    if dv and 1e-6 < abs(dv) < 1e6 and abs(dv - round(dv)) > 1e-9:
                        extra += '  ; imm_f64=%r' % dv
                else:
                    dv = struct.unpack('<d', struct.pack('<Q', v))[0]
                    if 1e-9 < abs(dv) < 1e9 and abs(dv - round(dv)) > 1e-12:
                        extra += '  ; imm_f64=%r' % dv
    return '0x%X  %-8s %-44s%s' % (insn.address, insn.mnemonic, insn.op_str, extra)

def dump(pe, md, lo, hi, title=None):
    if title:
        print('=== %s ===' % title)
    for i in disasm_range(pe, md, lo, hi):
        print(annotate(pe, i))
    print()

def load_pdata():
    pf = json.load(open(R26 + '/evidence/r26_pdata_functions.json'))
    return [(int(a, 16), int(b, 16)) for a, b in pf]

def fn_bounds(va):
    for lo, hi in load_pdata():
        if lo <= va < hi:
            return lo, hi
    return None

def scan_calls(pe, md, target, text_lo=0x180001000, text_hi=0x18037D000, pdata=True):
    """Find direct call sites to `target` VA across .text (pdata-bounded, else linear)."""
    hits = []
    if pdata:
        fns = [b for b in load_pdata() if text_lo <= b[0] < text_hi]
    else:
        fns = [(text_lo, text_hi)]
    for lo, hi in fns:
        for i in disasm_range(pe, md, lo, hi):
            if i.mnemonic in ('call', 'jmp') and i.op_str.startswith('0x'):
                try:
                    dst = int(i.op_str, 16)
                except ValueError:
                    continue
                if dst == target:
                    hits.append((i.address, lo, hi))
    return hits
