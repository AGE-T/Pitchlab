"""Round-10 independent numerical verification of the round-9 pitch-law numbers.

Read-only; no target bytes are written. All inputs are bit patterns / instruction
facts re-derived from the shipped image (fresh capstone decode: r10_decode.txt).

ROUND-11 CORRECTION (2025): two output labels in this script were wrong and are
fixed here; the original output is preserved as r10_verify_results_v1_superseded.txt.
  * "max |cents - walk*depth| ... (at F=1, depth=200 corner)" -> the maximum is
    an INTERIOR one near f ~= 0.0561 (F ~= 0.673 at depth 200, i.e. depth ~=
    134.6 at F = 1); the corner error is only 0.0769 cents.
  * "one-sided upward" (verdict) -> describes only the modulation DIRECTION
    (4*P >= 1 raises the pitch above the base ratio); as a description of the
    signed approximation error it was wrong: the residual
    1200*log2(4*float32(P(f))) - 2400*f is negative across the bulk of the
    shipped domain (achieved cents slightly BELOW walk*depth), zero at f = 0,
    with positive excursions bounded by half a float32 P-ulp (+1.03e-4 cents)
    only for f < ~2.9e-5, and it changes sign twice beyond the shipped range.
See evidence/analysis_round11/r11_numeric_audit.py for the high-resolution audit
that establishes these facts (5e-10 sweep, orbit scan, depth sweep).

Verifies:
  1. The quartic coefficients (float32 bit patterns at engine+0x40D8..+0x40E8).
  2. cents(F, depth) = 1200*log2(4*float32(P(f))) with the target's exact
     arithmetic (double quartic, observed operation order, CVTPD2PS rounding).
  3. The round-9 doc table (claimed 4P and cents values) - exact vs claimed.
  4. The walk*depth law and the quartic's approximation error over [0, 1/12].
  5. The noise scale constant 0x34000001 and the arithmetic output range.
  6. The full-period orbit of the decoded noise recurrence (default config):
     period, emitted extremes, attainable |noise|, attainable F, max cents.
"""
import math
import struct

import numpy as np

f32 = lambda x: float(np.float32(x))
f64 = lambda x: float(np.float64(x))
LOG2 = math.log(2.0)

# ---------------------------------------------------------------- constants
C_BITS = [0x3E800000, 0x3EB12E86, 0x3E79C4BB, 0x3DF569C6, 0x3D0ED15E]
C = [struct.unpack("<f", struct.pack("<I", b))[0] for b in C_BITS]
SCALE = struct.unpack("<f", struct.pack("<I", 0x34000001))[0]
INV2400 = struct.unpack("<d", bytes.fromhex("4f1be8b4814e3b3f"))[0]  # 0x1804120B0


def P_double(f):
    """Quartic in double, operation order per the decoded prepare (0x1800E236D..0x1800E23D9)."""
    f = float(f)
    f2 = f * f
    f4 = f2 * f2
    f3 = f2 * f
    t = C[3] * f3          # c3*f^3
    t += C[4] * f4         # + c4*f^4
    t += C[2] * f2         # + c2*f^2
    lin = C[1] * f + C[0]  # c1*f + c0
    t += lin
    return t


def four_P(f, round_f32=True):
    """4*P(f) with (round_f32) the target's CVTPD2PS@0x1800E2408, or pure double."""
    p = P_double(f)
    if round_f32:
        p = f32(p)
    return 4.0 * p


def cents(f, round_f32=True):
    v = four_P(f, round_f32)
    return 1200.0 * math.log2(v)


def page10(depth):
    """page[10] = float32( double(live) * (1/2400) ) per R-46 (mulsd, cvtpd2ps)."""
    return f32(f64(f32(depth)) * INV2400)


def f_of(F, depth):
    """f = double(F) * double(page10)  (two cvtps2pd + mulsd @0x1800E236D)."""
    return f64(f32(F)) * f64(page10(depth))


print("=" * 78)
print("ROUND-10 NUMERICAL VERIFICATION  (target-exact arithmetic, OBS constants)")
print("=" * 78)

print("\n[1] Coefficients (float32 bit patterns -> values)")
for i, (b, v) in enumerate(zip(C_BITS, C)):
    print(f"  c{i} = 0x{b:08X} = {v!r}")
print(f"  scale 0x34000001 = {SCALE!r} = 2^-23*(1+2^-23): "
      f"{SCALE == math.ldexp(1.0, -23) * (1 + math.ldexp(1.0, -23))}")
print(f"  P(0) = {P_double(0.0)!r} (exactly 0.25: {P_double(0.0) == 0.25}); "
      f"4*P(0) with f32 rounding = {four_P(0.0)!r}")

print("\n[2] Round-9 doc table, claimed vs exact  (f = depth/2400 rows of doc §3)")
doc_rows = [
    ("1/96  (walk 1, depth 25)", 1 / 96, 1.014568, 25.02),
    ("1/48  (walk 1, depth 50)", 1 / 48, 1.029268, 50.13),
    ("1/24  (walk 1, depth 100)", 1 / 24, 1.059047, 100.55),
    ("1/12  (walk 1, depth 200)", 1 / 12, 1.122414, 201.68),
    ("0.5", 0.5, 2.004663, 1204.0),
    ("1.0", 1.0, 3.978684, 2386.6),
]
print(f"  {'f':<26}{'4P exact':>12}{'4P doc':>10}{'cents exact':>13}"
      f"{'cents doc':>11}{'err':>9}")
for label, f, doc_4p, doc_c in doc_rows:
    e4 = four_P(f)
    ec = cents(f)
    print(f"  {label:<26}{e4:>12.6f}{doc_4p:>10.4f}{ec:>13.3f}{doc_c:>11.2f}"
          f"{doc_c - ec:>+9.2f}")
print("  Internal consistency of the doc's own columns (1200*log2 of the doc 4P):")
for label, f, doc_4p, doc_c in doc_rows:
    implied = 1200.0 * math.log2(doc_4p)
    print(f"    {label:<26} 1200*log2({doc_4p}) = {implied:9.3f}  vs doc cents "
          f"{doc_c:7.2f}  (diff {doc_c - implied:+.2f})")

print("\n[3] Task grid: cents(F, depth) for F in {0, .25, .5, .75, 1} and -> 1")
for depth in (50, 200):
    print(f"  depth = {depth}  (page10 = {page10(depth)!r})")
    print(f"    {'F':>8}{'f':>16}{'cents':>12}{'F*depth':>10}{'dev':>9}")
    for F in (0.0, 0.25, 0.5, 0.75, 1.0):
        fv = f_of(F, depth)
        cv = cents(fv)
        print(f"    {F:>8}{fv:>16.10f}{cv:>12.4f}{F * depth:>10.2f}"
              f"{cv - F * depth:>+9.4f}")
    lim = cents(f_of(1.0, depth))
    print(f"    limit F->1 (continuous) = {lim:.4f} cents; nominal walk*depth = "
          f"{depth} cents; shortfall {depth - lim:.4f}")

print("\n[4] The walk*depth law and the quartic fit over the shipped range")
fs = np.linspace(0.0, 1 / 12, 20001)
quad = np.array([four_P(float(x)) for x in fs])
exac = np.power(4.0, fs)
rel = np.abs(quad / exac - 1.0)
cents_err = np.abs(1200.0 * np.log2(quad / exac))
inom = 2400.0 * fs
cents_vs_nominal = 1200.0 * np.log2(quad) - inom
print(f"  max |4P/4^f - 1| for f in [0,1/12]:  {rel.max():.3e}  "
      f"(round-9 claimed <= 6e-5)")
print(f"  max |cents - 2400f| (fit error):     {np.abs(cents_vs_nominal).max():.4f} cents")
i_err = int(np.argmax(np.abs(cents_vs_nominal)))
print(f"  max |cents - walk*depth| over F in [0,1], depth in [0,200]: "
      f"{np.abs(cents_vs_nominal).max() * 1.0:.4f} cents (INTERIOR maximum at "
      f"f = {fs[i_err]:.5f} -> F = {fs[i_err] * 12:.3f} at depth 200, depth = "
      f"{fs[i_err] * 2400:.1f} at F = 1; NOT the F=1/depth=200 corner, where "
      f"the error is only {abs(cents_vs_nominal[-1]):.4f} cents; the argmax is "
      f"grid-sensitive within a ~1e-4-wide f plateau because the float32 "
      f"rounding of P flattens the minimum - see r11_numeric_audit.py)")
print(f"  at f=1/12: 4P = {four_P(1/12):.7f}, 4^f = {4.0 ** (1/12):.7f}, "
      f"cents = {cents(1/12):.3f} (nominal 200)")
print(f"  at f=1:    4P = {four_P(1.0):.7f}, 4^f = 4, cents = {cents(1.0):.2f} "
      f"(rel err {four_P(1.0) / 4 - 1:+.4f})")

print("\n[5] Candidate sources of the claimed cents (none reproduces all six)")
cands = {
    "round-6 transcribed coeff (0.34630,0.24410,0.11983,0.03489)": (
        [0.25, 0.34630, 0.24410, 0.11983, 0.03489]),
}
for name, cc in cands.items():
    def P6(f, cc=cc):
        return cc[0] + cc[1] * f + cc[2] * f * f + cc[3] * f ** 3 + cc[4] * f ** 4
    vals = [1200.0 * math.log2(4.0 * P6(f)) for _, f, _, _ in doc_rows]
    print(f"  {name}:")
    for (label, _, _, dc), v in zip(doc_rows, vals):
        print(f"    {label:<26} -> {v:9.3f}  (doc claimed {dc:7.2f}, diff {dc - v:+.2f})")
print("  formula 2400f*(1+f/10):")
for label, f, _, dc in doc_rows:
    v = 2400.0 * f * (1 + f / 10)
    print(f"    {label:<26} -> {v:9.3f}  (doc claimed {dc:7.2f}, diff {dc - v:+.2f})")
print("  ln(2) typo variants (0.69 / 0.693 / 0.7):")
for ld in (0.69, 0.693, 0.7):
    vals = [1200.0 * math.log(four_P(f)) / ld for _, f, _, _ in doc_rows]
    print(f"    ln2={ld}: " + "  ".join(f"{v:8.2f}" for v in vals))
print("  doc's own 4P column -> cents: "
      + "  ".join(f"{1200.0 * math.log2(d4):8.2f}" for _, _, d4, _ in doc_rows))
print("  doc's claimed cents:            "
      + "  ".join(f"{dc:8.2f}" for _, _, _, dc in doc_rows))

print("\n[6] Noise range (arithmetic bound, default config FUN_180139C00)")
p23 = 1 << 23
sar_lo, sar_hi = -(1 << 23), (1 << 23) - 1
print(f"  sar(state,8) in [{sar_lo}, {sar_hi}]  (int32 arithmetic shift)")
print(f"  scale = 2^-23*(1+2^-23) = {SCALE!r}")
lo_out = f32(f32(sar_lo) * SCALE)      # mulss rounds to float32
hi_out = f32(f32(sar_hi) * SCALE)
print(f"  exact product at sar=-2^23:  -(1+2^-23) = {-(1 + 2.0 ** -23)!r}")
print(f"  float32 out at sar=-2^23:    {lo_out!r}   (below -1 by {-(1 + lo_out):.3e})")
print(f"  exact product at sar=2^23-1: 1-2^-46 = {1 - 2.0 ** -46!r}")
print(f"  float32 out at sar=2^23-1:   {hi_out!r}   (rounds UP to exactly 1.0: "
      f"{hi_out == 1.0})")
print(f"  => noise in [-(1+2^-23), +1.0]; |noise| in [0, 1+2^-23]  "
      f"(exceeds 1 by {2.0 ** -23:.3e})")
print(f"  => after abs/S&H/slew(clamps at target): F in [0, 1+2^-23]")
for depth in (50, 200):
    c_nom = cents(f_of(1.0, depth))
    c_max = cents(f_of(1.0 + 2.0 ** -23, depth))
    print(f"  max cents at depth {depth}: F=1 -> {c_nom:.4f}, F=1+2^-23 -> "
          f"{c_max:.6f} (excess {c_max - c_nom:.2e} cents)")

print("\n[7] Orbit of the decoded recurrence (seeds 13,17 / mask 2^24 / 4 outputs)")
MOD = 1 << 24


def matmul(A, B):
    """2x2 matrix multiply mod 2^24 (tuple-of-tuples)."""
    return (
        (A[0][0] * B[0][0] + A[0][1] * B[1][0]) % MOD,
        (A[0][0] * B[0][1] + A[0][1] * B[1][1]) % MOD,
        (A[1][0] * B[0][0] + A[1][1] * B[1][0]) % MOD,
        (A[1][0] * B[0][1] + A[1][1] * B[1][1]) % MOD,
    )


def as_M(t):
    return ((t[0], t[1]), (t[2], t[3]))


def apply(M, a, b):
    return (M[0][0] * a + M[0][1] * b) % MOD, (M[1][0] * a + M[1][1] * b) % MOD


M = ((17, 12), (24, 17))
# matrix order (power of two: M = I mod 4)
Pw = M
order = None
for k in range(1, 40):
    Pw = as_M(matmul(Pw, Pw))
    if Pw == ((1, 0), (0, 1)):
        order = 1 << k
        break
print(f"  matrix order of M=[[17,12],[24,17]] mod 2^24: {order} (2^{math.log2(order):.0f})")

# full orbit scan in blocks of B consecutive states
B = 1 << 16
a, b = 13, 17
V0a, V0b = [], []
for i in range(B):
    V0a.append(a)
    V0b.append(b)
    a, b = apply(M, a, b)
V0a = np.array(V0a, dtype=np.int64)
V0b = np.array(V0b, dtype=np.int64)
MB = M
for i in range(B - 1):
    MB = as_M(matmul(MB, M))

smin, smax = 1 << 30, -(1 << 30)
hit_lo = hit_hi = False
period_found = None
nblocks = order // B
W = ((1, 0), (0, 1))
for q in range(nblocks):
    A = (W[0][0] * V0a + W[0][1] * V0b) % MOD
    Bv = (W[1][0] * V0a + W[1][1] * V0b) % MOD
    for num, den in ((2, 1), (4, 3), (10, 7), (24, 17)):
        e = (num * A + den * Bv) % MOD
        sar = np.where(e >= (1 << 23), e - (1 << 24), e)
        smin = min(smin, int(sar.min()))
        smax = max(smax, int(sar.max()))
        hit_lo |= bool((e == (1 << 23)).any())
        hit_hi |= bool((e == (1 << 23) - 1).any())
    W = as_M(matmul(W, MB))
    state = apply(W, 13, 17)
    if state == (13, 17) and period_found is None:
        period_found = (q + 1) * B
print(f"  vector period (first return to seeds): {period_found}")
print(f"  emitted sar over the full orbit: min = {smin}, max = {smax}")
print(f"  extremes visited: sar = -2^23 (0x800000): {hit_lo};  sar = 2^23-1: {hit_hi}")
out_min = f32(f32(smin) * SCALE)
out_max = f32(f32(smax) * SCALE)
print(f"  emitted noise (float32) over the orbit: [{out_min!r}, {out_max!r}]")
print(f"  max |noise| over the orbit: {max(abs(out_min), abs(out_max))!r}  "
      f"(exceeds 1: {max(abs(out_min), abs(out_max)) > 1.0})")
Fmax = max(abs(out_min), abs(out_max))
for depth in (50, 200):
    print(f"  max cents at depth {depth} with orbit-max F = {Fmax!r}: "
          f"{cents(f_of(Fmax, depth)):.4f}")

print("\n[8] Verdict summary")
print(f"  - claimed 50.13c at depth 50 -> exact {cents(f_of(1.0, 50)):.3f}c")
print(f"  - claimed 201.68c at depth 200 -> exact {cents(f_of(1.0, 200)):.3f}c")
print(f"  - the walk*depth law holds to <= {np.abs(cents_vs_nominal).max():.3f} cents "
      f"over the shipped range (quartic fit error). The signed residual is "
      f"negative across the bulk of the domain (achieved cents slightly BELOW "
      f"walk*depth, zero at f=0, positive excursions only within half a float32 "
      f"P-ulp of f ~ 0); 'upward' describes the modulation DIRECTION relative to "
      f"the base ratio, not the sign of the approximation error.")
print(f"  - round-9 'F in [0,1)' refined: arithmetic bound F in [0, 1+2^-23];"
        f" orbit-actual max |noise| = {Fmax!r}")
