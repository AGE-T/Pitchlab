#!/usr/bin/env python3
# PITCHLAB R25 -- stage 2: precision extraction of the tracker decimation FIR
# coefficients + rate-class constants, straight from the .text bytes.
# STATIC / READ-ONLY. Binary NEVER executed, NEVER modified.
#
# Findings of stage 1 (r25_fir_dump.py):
#   * m01 setparam FUN_1800E10B0, dispatch table 4-byte offsets at 0x1800E14D0
#     (entry[5] -> 0x1800E1137 = SR / rate-class handler).
#   * Coefficients are INLINE float32 immediates in three class blocks inside
#     the case-5 handler; stored via engine base rbx at eng+0xA3C8.. (=T+0x5EB0..).
#   * SR thresholds are .rdata: 0x180412770 = 50000.0f, 0x180412774 = 100000.0f.
# This script re-extracts everything precisely and analyzes the tables.
import hashlib
import json
import struct
import capstone

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
SHA = "7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2"
OUT = "/home/z/my-project/octavox-work/analysis_r25/r25_fir_tables.json"


def load_pe(path):
    data = open(path, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff)[0]
    opt_sz = struct.unpack_from("<H", data, e + 20)[0]
    ib = struct.unpack_from("<Q", data, coff + 20 + 24)[0]
    so = e + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = so + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, ib, secs


def va2off(secs, va, ib):
    rva = va - ib
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva)
    return None


DATA, IB, SECS = load_pe(TARGET)
assert hashlib.sha256(DATA).hexdigest() == SHA

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
X86 = capstone.x86
T_OFF = 0x4518  # tracker T = engine + 0x4518


def f32(w):
    return struct.unpack("<f", struct.pack("<I", w & 0xFFFFFFFF))[0]


def disasm(start, end):
    off = va2off(SECS, start, IB)
    return list(md.disasm(DATA[off:off + (end - start)], start))


# ---------------------------------------------------------------- extract
# case-5 handler: 0x1800E1137 .. 0x1800E13D5 (3 class blocks, each ending in ret)
CLASS_BLOCKS = [
    ("A", 0x1800E1137, 0x1800E11E1),   # SR < 50000
    ("B", 0x1800E11DD, 0x1800E12A2),   # 50000 <= SR < 100000
    ("C", 0x1800E129B, 0x1800E1398),   # SR >= 100000
]

result = {
    "target": TARGET,
    "sha256": SHA,
    "image_base": IB,
    "mechanism": "inline float32 immediates in m01 setparam FUN_1800E10B0 case 5 "
                 "(mov dword ptr [rbx+disp32], imm32); no .rdata coefficient tables",
    "dispatch": {},
    "thresholds": {},
    "classes": {},
    "ctor_tracker_init": [],
}

# dispatch table entry[5] verification
off = va2off(SECS, 0x1800E14D0, IB)
for i in range(18):
    w = struct.unpack_from("<I", DATA, off + 4 * i)[0]
    result["dispatch"]["case_%d" % i] = 0x180000000 + w

# SR thresholds from .rdata
for va in (0x180412770, 0x180412774):
    o = va2off(SECS, va, IB)
    w = struct.unpack_from("<I", DATA, o)[0]
    result["thresholds"]["0x%X" % va] = {"u32": w, "f32": f32(w)}

for name, lo, hi in CLASS_BLOCKS:
    coeffs = []
    other = []
    for ins in disasm(lo, hi):
        ops = ins.operands
        if ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86.X86_OP_MEM \
                and ops[1].type == X86.X86_OP_IMM and ops[0].mem.base == X86.X86_REG_RBX:
            disp = ops[0].mem.disp
            imm = ops[1].imm & 0xFFFFFFFF
            if 0xA3C8 <= disp <= 0xA418:
                coeffs.append({
                    "code_va": ins.address,
                    "eng_disp": disp,
                    "trk_disp": disp - T_OFF,
                    "index": (disp - 0xA3C8) // 4,
                    "u32": imm,
                    "f32": f32(imm),
                })
            else:
                other.append({
                    "code_va": ins.address,
                    "eng_disp": disp,
                    "trk_disp": disp - T_OFF,
                    "imm": imm,
                    "note": "cadence/taps int store" if imm < 0x100 else "?",
                })
    result["classes"][name] = {
        "code_range": [lo, hi],
        "n_coeffs": len(coeffs),
        "coeffs": coeffs,
        "int_stores": other,
    }

# ctor tracker-init constants (rdi = T = engine+0x4518), region 0x1800E0D1F..0x1800E0EA1
ctor_init = []
for ins in disasm(0x1800E0D1F, 0x1800E0EA1):
    ops = ins.operands
    if ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86.X86_OP_MEM \
            and ops[1].type == X86.X86_OP_IMM and ops[0].mem.base == X86.X86_REG_RDI:
        disp = ops[0].mem.disp
        imm = ops[1].imm & 0xFFFFFFFFFFFFFFFF
        sz = 4 if "dword" in ins.op_str else (8 if "qword" in ins.op_str else (1 if "byte" in ins.op_str else 2))
        rec = {"code_va": ins.address, "trk_disp": disp, "imm": imm, "size": sz,
               "op_str": ins.op_str}
        if sz == 4:
            rec["f32"] = f32(imm)
        ctor_init.append(rec)
    elif ins.mnemonic in ("movss", "mulss") and any(
            o.type == X86.X86_OP_MEM and o.mem.base == X86.X86_REG_RIP for o in ins.operands):
        for o in ins.operands:
            if o.type == X86.X86_OP_MEM and o.mem.base == X86.X86_REG_RIP:
                tgt = ins.address + ins.size + o.mem.disp
                ctor_init.append({"code_va": ins.address, "op": ins.mnemonic,
                                  "rip_target": tgt,
                                  "note": "rdata const ref in tracker init"})
result["ctor_tracker_init"] = ctor_init

# ctor window-class thresholds (0x1000/0x2000/0x4000 at [r15+0x409c])
result["ctor_window_class"] = {
    "code_range": [0x1800E062F, 0x1800E0659],
    "note": "comiss vs .rdata 0x180412774 (100000.0) then 0x180412770 (50000.0); "
            "eax = 0x4000 / 0x2000 / 0x1000 -> [r15+0x409c] (engine analysis window)",
}

# ---------------------------------------------------------------- analysis
def s(classname):
    return [c["f32"] for c in result["classes"][classname]["coeffs"]]

A, B, C = s("A"), s("B"), s("C")
fwd = {"A": 5, "B": 10, "C": 20}
bwd = {"A": 6, "B": 11, "C": 21}
cadence = {"A": 2, "B": 4, "C": 8}

analysis = {}
for name, coeffs in (("A", A), ("B", B), ("C", C)):
    n = len(coeffs)
    total = sum(coeffs)
    # symmetric kernel [c_{N-1} ... c_0 | c_0 ... c_{N-2}] (2N-1 taps),
    # DC gain = c[N-1] + 2*sum(c[0..N-2])
    dc = coeffs[-1] + 2 * sum(coeffs[:-1])
    analysis["class_%s" % name] = {
        "n_taps_kernel": 2 * n - 1,
        "n_coeffs": n,
        "fwd_taps_T+0x5F04": fwd[name],
        "bwd_taps_T+0x5F08": bwd[name],
        "cadence_reload_T+0x5F14": cadence[name],
        "sum_coeffs": total,
        "dc_gain_symmetric_kernel": dc,
    }
# cross-class structure: B[2k] == A[k]/2, C[2k] == B[k]/2 ?
pairs_AB = []
for k in range(len(A)):
    if 2 * k < len(B):
        pairs_AB.append((A[k], B[2 * k], A[k] / 2.0))
pairs_BC = []
for k in range(len(B)):
    if 2 * k < len(C):
        pairs_BC.append((B[k], C[2 * k], B[k] / 2.0))
analysis["cross_class"] = {
    "B_even_index_eq_A_half": ["%r vs %r (A/2=%r)" % p for p in pairs_AB],
    "C_even_index_eq_B_half": ["%r vs %r (B/2=%r)" % p for p in pairs_BC],
}
result["analysis"] = analysis

# sinc fit (DER, curiosity): c[k] = peak * sinc(alpha*(N-1-k)) * window
import math


def sinc(x):
    return 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)


def fit(name, coeffs):
    n = len(coeffs)
    peak = coeffs[-1]
    # estimate alpha from first ratio
    r = coeffs[n - 2] / peak
    # binary search alpha in (0,1) so sinc(alpha) == r
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if sinc(mid) < r:
            hi = mid
        else:
            lo = mid
    alpha = (lo + hi) / 2
    fitted = [peak * sinc(alpha * (n - 1 - i)) for i in range(n)]
    err = max(abs(a - b) for a, b in zip(fitted, coeffs))
    return {"alpha_per_tap": alpha, "max_abs_err_vs_pure_sinc": err,
            "note": "windowed sinc; pure-sinc fit error shows the window term"}


result["analysis"]["sinc_fit_DER"] = {name: fit(name, c) for name, c in
                                      (("A", A), ("B", B), ("C", C))}

with open(OUT, "w") as f:
    json.dump(result, f, indent=1)

# ---------------------------------------------------------------- print
print("wrote", OUT)
print("\n== dispatch: case 5 -> 0x%X" % result["dispatch"]["case_5"])
print("== thresholds:", result["thresholds"])
for cn in ("A", "B", "C"):
    cl = result["classes"][cn]
    print("\n== CLASS %s (code 0x%X..0x%X): %d coeffs, fwd=%d bwd=%d cadence=%d" %
          (cn, cl["code_range"][0], cl["code_range"][1], cl["n_coeffs"],
           fwd[cn], bwd[cn], cadence[cn]))
    for c in cl["coeffs"]:
        print("   c[%2d]  eng+0x%04X (T+0x%04X)  @0x%08X   0x%08X   %.9g" %
              (c["index"], c["eng_disp"], c["trk_disp"], c["code_va"], c["u32"], c["f32"]))
    for o in cl["int_stores"]:
        print("   int   eng+0x%04X (T+0x%04X)  @0x%08X   = %d" %
              (o["eng_disp"], o["trk_disp"], o["code_va"], o["imm"]))
print("\n== ctor tracker init (rdi = T = engine+0x4518):")
for r in ctor_init:
    if "f32" in r:
        print("   T+0x%04X  @0x%08X  0x%08X  f32=%r  (%s)" %
              (r["trk_disp"], r["code_va"], r["imm"], r["f32"], r["op_str"]))
    elif "rip_target" in r:
        print("   @0x%08X  %s -> 0x%X" % (r["code_va"], r["op"], r["rip_target"]))
    else:
        print("   T+0x%04X  @0x%08X  = 0x%X  (%s)" %
              (r["trk_disp"], r["code_va"], r["imm"], r["op_str"]))
print("\n== analysis:")
print(json.dumps(analysis["class_A"], indent=1))
print(json.dumps(analysis["class_B"], indent=1))
print(json.dumps(analysis["class_C"], indent=1))
print("\ncross-class B[2k] vs A[k]/2:")
for p in pairs_AB:
    print("   A=%r  B=%r  A/2=%r" % p)
print("cross-class C[2k] vs B[k]/2:")
for p in pairs_BC:
    print("   B=%r  C=%r  B/2=%r" % p)
print("\nsinc fits:", json.dumps(result["analysis"]["sinc_fit_DER"], indent=1))
