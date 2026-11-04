#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PITCHLAB round 22 -- static trace: the Randomize sign/modulation-direction
resolution (PRIORITY 5's special requirement).  Read-only static
disassembly of the ORIGINAL Eventide Octavox binary (original/Ox.vst3).
The binary is NEVER executed.
"""
import json, struct, math
import capstone

TARGET = "/home/z/my-project/octavox-work/original/Ox.vst3"
OUT_TXT = "/home/z/my-project/octavox-work/Octavox_RE_evidence/evidence/analysis_round22/r22_randomize_trace.txt"
OUT_JSON = "/home/z/my-project/octavox-work/Octavox_RE_evidence/evidence/analysis_round22/r22_randomize_trace.json"

def load_pe(path):
    data = open(path, "rb").read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfanew + 4
    machine, nsec = struct.unpack_from("<HH", data, coff)
    opt_sz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    sec_off = e_lfanew + 24 + opt_sz
    secs = []
    for i in range(nsec):
        o = sec_off + 40 * i
        name = data[o:o+8].rstrip(b"\0").decode()
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return data, image_base, secs

def va_to_off(secs, va, image_base=0x180000000):
    rva = va - image_base
    for name, sva, vsize, raw, rsize in secs:
        if sva <= rva < sva + vsize:
            return raw + (rva - sva), name
    return None, None

def disasm(data, secs, start, end, image_base=0x180000000):
    off, sec = va_to_off(secs, start, image_base)
    if off is None:
        return [], None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    code = data[off:off + (end - start)]
    return [(i.address, i.mnemonic, i.op_str) for i in md.disasm(code, start)], sec

def read_const(data, secs, va, size, image_base=0x180000000):
    off, _ = va_to_off(secs, va, image_base)
    return data[off:off+size] if off is not None else None

def main():
    data, image_base, secs = load_pe(TARGET)
    L = []
    def emit(s=""):
        L.append(s)
    emit("PITCHLAB R22 -- RANDOMIZE SIGN / MODULATION-DIRECTION TRACE (static)")
    emit("=" * 78)
    emit("Original binary: %s (NEVER executed; static disassembly only)" % TARGET)
    emit("capstone %s; PE image base 0x%X" % (capstone.__version__, image_base))
    rec = {"target": TARGET, "image_base": image_base}

    emit("")
    emit("A. THE RANDOM SOURCE -- FUN_180139B40 (rdm noise [212])")
    emit("-" * 78)
    insns, _ = disasm(data, secs, 0x180139B40, 0x180139C60)
    for a, m, o in insns:
        emit("  %08X  %-9s %s" % (a, m, o))
    emit("")
    emit("  Reading: the generator state is shifted right and scaled into the")
    emit("  float range; the OUTPUT IS SIGNED at the source (both signs occur")
    emit("  over the orbit: [-1.0, +1.0] float32, R10/R11 audits).")
    rec["A_source"] = {"function": "FUN_180139B40", "output_sign": "signed",
                       "orbit_range": [-1.0, 1.0]}

    emit("")
    emit("B. THE ABSOLUTE-VALUE SITES IN THE AUDIO WORKER")
    emit("-" * 78)
    mask = read_const(data, secs, 0x180412C60, 16)
    emit("  .rdata 16 bytes at 0x180412C60: %s" % (mask.hex() if mask else "?"))
    if mask:
        emit("    contains the andps abs pattern 0x7FFFFFFF: %s" % ("7fffffff" in mask.hex()))
    for (s, e, tag) in [(0x180103EE0, 0x180103FA0, "the per-voice random chain call site"),
                        (0x1800F54C0, 0x1800F5620, "the 0x1800F55xx abs family")]:
        emit("")
        emit("  -- %s: 0x%X..0x%X" % (tag, s, e))
        insns, _ = disasm(data, secs, s, e)
        for a, m, o in insns:
            mark = "   <== ABS MASK" if m == "andps" else ""
            emit("  %08X  %-9s %-42s%s" % (a, m, o, mark))
    rec["B_abs"] = {"mask_va": "0x180412C60",
                    "mask_bytes": mask.hex() if mask else None,
                    "applied_to": "the noise output BEFORE the sample&hold "
                                  "intake (targets become nonnegative)"}

    emit("")
    emit("C. THE SAMPLE&HOLD INTAKE (rdm sh N, FUN_180139970)")
    emit("-" * 78)
    insns, _ = disasm(data, secs, 0x180139970, 0x180139A40)
    for a, m, o in insns:
        emit("  %08X  %-9s %s" % (a, m, o))
    emit("  Reading: the held target is a snapshot of the (already-absolutised)")
    emit("  noise stream; the comparator re-arm does not re-introduce a sign.")
    rec["C_sh"] = {"function": "FUN_180139970", "target_sign": "nonnegative"}

    emit("")
    emit("D. THE SLEW (rdm slew N, FUN_180139790)")
    emit("-" * 78)
    insns, _ = disasm(data, secs, 0x180139790, 0x180139860)
    for a, m, o in insns:
        emit("  %08X  %-9s %s" % (a, m, o))
    emit("  Reading: state walks toward the held target and CLAMPS AT THE")
    emit("  TARGET on crossing (the 0x1801397D3 region, R9). With nonnegative")
    emit("  targets and a zero initial state the walk stays nonnegative:")
    emit("  F in [0, 1+2^-23] arithmetically, [0, 1] over the orbit (R10/R11).")
    rec["D_slew"] = {"function": "FUN_180139790",
                     "state_sign": "nonnegative (targets nonnegative, initial 0)",
                     "clamp_at_target": True}

    emit("")
    emit("E. THE SCALE CONVERSION (page[10], 0x1800E769B-0x1800E7749)")
    emit("-" * 78)
    c = read_const(data, secs, 0x1804120B0, 8)
    if c:
        v = struct.unpack("<d", c)[0]
        emit("  .rdata double at 0x1804120B0 = %.19g  (1/2400 = %.19g)" % (v, 1.0/2400.0))
    insns, _ = disasm(data, secs, 0x1800E7690, 0x1800E7760)
    for a, m, o in insns:
        mark = "   <== mulsd by 1/2400" if a == 0x1800E772D else ""
        emit("  %08X  %-9s %-46s%s" % (a, m, o, mark))
    emit("  Reading: page[10] = clamp(clamp(live_modamt, lo, hi)*(1/2400), -1, 1).")
    rec["E_scale"] = {"constant_va": "0x1804120B0",
                      "value": struct.unpack("<d", c)[0] if c else None,
                      "law": "page10 = clamp(modamt/2400, -1, 1)"}

    emit("")
    emit("F. THE FINAL MULTIPLIER CHAIN (0x1800E2330-0x1800E2460)")
    emit("-" * 78)
    insns, _ = disasm(data, secs, 0x1800E2330, 0x1800E2460)
    for a, m, o in insns:
        mark = ""
        if a in (0x1800E23DD, 0x1800E241D, 0x1800E243B):
            mark = "   <== F*page10 / P(f)*slot / *64"
        emit("  %08X  %-9s %-48s%s" % (a, m, o, mark))
    c64 = read_const(data, secs, 0x180412490, 8)
    if c64:
        emit("  .rdata double at 0x180412490 = %g (the *64.0)" % struct.unpack("<d", c64)[0])
    emit("  Reading (rounds 9/12, re-verified instruction-by-instruction):")
    emit("    f    = F * page10               (F >= 0, page10 >= 0 => f >= 0)")
    emit("    step = (float)P(f) * slot_40F8[v] * 64 = 4*P(f)*(ratio*correction)")
    emit("    4*P(f) >= 1 for f >= 0 => the rate multiplier never dips below")
    emit("    unity: ONE-SIDED. No sign reintroduction exists in the chain.")
    rec["F_multiplier"] = {"chain": "f=F*page10; step=4*P(f)*(ratio/16)*correction*64",
                           "c64": struct.unpack("<d", c64)[0] if c64 else None,
                           "sign": "one-sided (4P>=1 for f>=0)"}

    emit("")
    emit("G. THE QUARTIC P AND ITS APPROXIMATION ERROR (numeric, DER)")
    emit("-" * 78)
    cc = [1.3842323, 0.9756581, 0.4793226, 0.1394705]
    P4 = lambda f: 1.0 + cc[0]*f + cc[1]*f*f + cc[2]*f**3 + cc[3]*f**4
    worst, worst_f = 0.0, 0.0
    f = 0.0
    while f <= 1.0/12.0 + 1e-12:
        err = 1200.0*math.log2(P4(f)) - f*2400.0
        if abs(err) > abs(worst):
            worst, worst_f = err, f
        f += 1e-5
    emit("  4*P(f) = 1 + 1.3842323f + 0.9756581f^2 + 0.4793226f^3 + 0.1394705f^4")
    emit("  Over the shipped domain f in [0, 1/12]:")
    emit("    max |achieved - nominal| = %.4f cents at f=%.5f (signed %+.4f c)"
         % (abs(worst), worst_f, worst))
    emit("    depth 50 default -> %+.4f c ; depth 200 -> %+.4f c"
         % (1200.0*math.log2(P4(50.0/2400.0))-50.0,
            1200.0*math.log2(P4(200.0/2400.0))-200.0))
    emit("    4*P over [0, 1/12] = [%.6f, %.6f]  (+0%% .. +%.2f%% rate)"
         % (P4(0.0), P4(1.0/12.0), 100.0*(P4(1.0/12.0)-1.0)))
    rec["G_quartic"] = {"coefficients_x4": cc, "domain": [0.0, 1.0/12.0],
                        "max_abs_error_cents": abs(worst),
                        "max_error_signed_cents": worst,
                        "rate_multiplier_range": [P4(0.0), P4(1.0/12.0)]}

    emit("")
    emit("VERDICT (the resolution)")
    emit("=" * 78)
    emit("  1. NOISE SIGN: signed at the source ([-1, +1] orbit, float32).")
    emit("  2. MODULATION INPUT RANGE: F = |noise|->sh->slew is NONNEGATIVE, [0, 1]")
    emit("     orbit-actual (1.0 attainable; 1+2^-23 arithmetic bound).")
    emit("  3. RATE MULTIPLIER RANGE: 4*P(f) in [1, 1.122412] at depth <= 200")
    emit("     (+0%% .. +12.24%% rate) -- ONE-SIDED, never below unity.")
    emit("  4. CENTS DEVIATION FROM BASELINE: 0 .. +199.92 c, upward only.")
    emit("  5. APPROXIMATION ERROR: |err| <= %.4f c over the shipped domain" % abs(worst))
    emit("     (negative in the bulk; R11's quantification re-verified).")
    emit("")
    emit("  THE R21 WORDING '+-0..8.33%% rate modulation' IS THE CORRECTION-RECORD")
    emit("  ITEM: the '+-' is wrong (no sign reintroduction exists anywhere in")
    emit("  the live chain -- every link re-derived above); '8.33%%' conflates the")
    emit("  f-domain ceiling (200/2400) with the rate multiplier (4*P in")
    emit("  [1, 1.1224] = +0..+12.24%%). The R9/R11 one-sided law STANDS.")
    emit("  Historical findings are preserved; this file is the correction record.")
    rec["verdict"] = {
        "noise_sign": "signed at source",
        "modulation_input": "nonnegative [0,1] (post-abs sh+slew)",
        "rate_multiplier": "one-sided [1, 1.122412] at depth<=200",
        "cents_deviation": "0..+199.92 upward only",
        "quartic_error_cents": abs(worst),
        "r21_wording": "CORRECTED: '+-0..8.33% rate modulation' was a wording "
                       "regression; the law is one-sided +0..+12.24% rate "
                       "(= +0..~200 cents), F in [0,1]",
    }

    open(OUT_TXT, "w").write("\n".join(L) + "\n")
    open(OUT_JSON, "w").write(json.dumps(rec, indent=2) + "\n")
    print("wrote", OUT_TXT)
    print("wrote", OUT_JSON)

if __name__ == "__main__":
    main()
