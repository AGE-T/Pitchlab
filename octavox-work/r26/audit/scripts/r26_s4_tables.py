"""R26 S4+S5+S6: data-table extraction + kernel mathematics.

Extracts: 256x6 interpolation kernel (0x1803B6170), temperament tables
(0x1803BB690), 19-row scale patterns (0x1803BB780), key map (0x1803BB610),
and runs a rigorous numeric investigation of the kernel generator.
"""
import sys, os, json, struct, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, EXPECT_SHA256
import numpy as np

OUT = '/home/z/my-project/octavox-work/r26/audit/evidence'
pe = PE()
assert pe.sha256 == EXPECT_SHA256

# ---------- 256x6 kernel table ----------
KERNEL_VA = 0x1803B6170
fo = pe.va2fo(KERNEL_VA)
raw = pe.data[fo:fo + 256*6*4]
K = np.array(struct.unpack('<1536f', raw), dtype=np.float64).reshape(256, 6)
print('kernel @0x%X rows=%d taps=%d' % (KERNEL_VA, K.shape[0], K.shape[1]))
print('  row sums: min=%.12f max=%.12f  (all==1? %s)' % (K.sum(1).min(), K.sum(1).max(), bool(np.all(K.sum(1)==1.0))))
print('  exact-zero taps: rows where tap==0.0 exactly:', [(r,j) for r in range(256) for j in range(6) if K[r,j]==0.0][:10])
print('  value range [%.9f, %.9f]' % (K.min(), K.max()))
np.save(OUT + '/r26_kernel_table.npy', K)

# Continuous-curve hypothesis: k[r][j] = h(r/256 + j - c) for a single h.
# For 6 taps, natural centers c in {2.0, 2.5, 3.0}; test which gives a single-valued curve.
for c in [2.0, 2.5, 3.0]:
    xs, ys = [], []
    for r in range(256):
        for j in range(6):
            xs.append(r/256.0 + j - c); ys.append(K[r, j])
    xs = np.array(xs); ys = np.array(ys)
    o = np.argsort(xs)
    xs, ys = xs[o], ys[o]
    # single-valued check: for duplicate x (float32 grid), how large is the spread?
    uniq, idx, cnt = np.unique(np.round(xs, 9), return_inverse=True, return_counts=True)
    spreads = []
    for u in range(len(uniq)):
        if cnt[u] > 1:
            v = ys[idx == u]
            spreads.append(v.max() - v.min())
    mx = max(spreads) if spreads else 0.0
    print('  center c=%.1f: %d unique x positions, duplicate-x max spread = %.3e' % (c, len(uniq), mx))

# Choose the best center (should be the one with spread ~ 0 → single continuous h)
best_c, best_spread = None, None
for c in [2.0, 2.5, 3.0]:
    pts = {}
    for r in range(256):
        for j in range(6):
            x = round(r/256.0 + j - c, 9)
            pts.setdefault(x, []).append(K[r, j])
    sp = max((max(v)-min(v)) for v in pts.values() if len(v) > 1) if any(len(v)>1 for v in pts.values()) else float('inf')
    if best_spread is None or (sp != float('inf') and sp < best_spread):
        best_c, best_spread = c, sp
print('  best center =', best_c, 'max dup spread =', best_spread)

# Build h(x) on its grid
pts = {}
for r in range(256):
    for j in range(6):
        x = round(r/256.0 + j - best_c, 9)
        pts.setdefault(x, []).append(K[r, j])
hx = np.array(sorted(pts.keys()))
hy = np.array([np.mean(pts[x]) for x in hx])
print('  h grid: %.4f..%.4f, %d points' % (hx[0], hx[-1], len(hx)))

# Symmetry of h about 0
if hx.min() <= -0.5 and hx.max() >= 0.5:
    # check h(-x) = h(x) where both exist
    neg = -hx[::-1]
    sym_pairs = []
    for x, y in zip(hx, hy):
        j = np.argmin(np.abs(neg - x))
        if abs(neg[j] - x) < 1e-9:
            sym_pairs.append((y, hy[::-1][j]))
    if sym_pairs:
        diffs = np.array([abs(a-b) for a, b in sym_pairs])
        print('  h symmetry h(-x)=h(x): %d pairs, max |diff| = %.3e' % (len(sym_pairs), diffs.max()))

# ---- candidate generator fits on h ----
def fit_quality(fn, label):
    pred = np.array([fn(x) for x in hx])
    err = np.abs(pred - hy)
    rms = float(np.sqrt(np.mean(err**2)))
    print('    %-38s rms=%.3e max=%.3e' % (label, rms, err.max()))
    return rms

print('  candidate generators (rms/max abs error vs h):')
results = {}
# windowed sinc family
sinc = lambda x: 1.0 if abs(x) < 1e-12 else math.sin(math.pi*x)/(math.pi*x)
windows = {
    'rect (sinc)':      lambda t: 1.0,
    'hann':             lambda t: 0.5*(1+math.cos(math.pi*t)) if abs(t)<=1 else 0.0,
    'hamming':          lambda t: 0.54+0.46*math.cos(math.pi*t) if abs(t)<=1 else 0.0,
    'blackman':         lambda t: 0.42+0.5*math.cos(math.pi*t)+0.08*math.cos(2*math.pi*t) if abs(t)<=1 else 0.0,
    'blackman-harris4': lambda t: 0.35875+0.48829*math.cos(math.pi*t)+0.14128*math.cos(2*math.pi*t)+0.01168*math.cos(3*math.pi*t) if abs(t)<=1 else 0.0,
    'lanczos(a=3)':     lambda t: sinc(t/3.0) if abs(t)<3 else 0.0,
    'lanczos(a=2)':     lambda t: sinc(t/2.0) if abs(t)<2 else 0.0,
}
for wl, wf in windows.items():
    for a in [2.0, 2.5, 3.0, 3.5]:
        f = lambda x, a=a, wf=wf: sinc(a*x)*wf(x/a) if a*abs(x) < 1e-9 or True else 0
        # normalise numerically on the grid used (sum over the row = 1 handled by construction below)
        pred = np.array([sinc(a*x)*wf(x/a) for x in hx])
        # the row-sum-1 normalisation: compare shapes after best scalar + offset fit
        A = np.vstack([pred, np.ones_like(pred)]).T
        coef, *_ = np.linalg.lstsq(A, hy, rcond=None)
        fitp = A @ coef
        rms = float(np.sqrt(np.mean((fitp-hy)**2)))
        results['sinc|%s|a=%.1f' % (wl, a)] = rms
best = sorted(results.items(), key=lambda kv: kv[1])[:8]
for k, v in best:
    print('    windowed-sinc %s  rms=%.3e' % (k, v))
# gaussian
for sigma in [0.3,0.4,0.5,0.6,0.7,0.8]:
    pred = np.array([math.exp(-x*x/(2*sigma*sigma)) for x in hx])
    A = np.vstack([pred, np.ones_like(pred)]).T
    coef, *_ = np.linalg.lstsq(A, hy, rcond=None)
    results['gaussian|s=%.2f' % sigma] = float(np.sqrt(np.mean((A@coef-hy)**2)))
# B-spline order 3 (cubic) centred
def bs3(x):
    x = abs(x)
    if x < 1: return (3.0*x**3 - 6.0*x**2 + 4.0)/6.0
    if x < 2: return ((2.0-x)**3)/6.0
    return 0.0
pred = np.array([bs3(x) for x in hx])
A = np.vstack([pred, np.ones_like(pred)]).T
coef, *_ = np.linalg.lstsq(A, hy, rcond=None)
results['bspline3'] = float(np.sqrt(np.mean((A@coef-hy)**2)))
print('  top fits:', sorted(results.items(), key=lambda kv: kv[1])[:6])

# ---- Fourier analysis of h (as a reconstruction kernel) ----
# dense interpolation of h on [-2.5, 3.5]
hd = np.interp(np.arange(-25, 36)/10.0, hx, hy)
# frequency response via evaluating the row applied to a complex exponential:
# y(theta) = sum_j h(x_j) e^{-i theta x_j} for row r with x_j = r/256 + j - c
# A perfect fractional-delay-lowpass has |H| = 1 up to cut and 0 beyond; phase = -c*theta
resp_mag, resp_ph = [], []
for r in [0, 64, 128, 192, 255]:
    for th in np.linspace(0, math.pi, 33):
        xj = np.array([r/256.0 + j - best_c for j in range(6)])
        hj = K[r, :]
        H = np.sum(hj * np.exp(-1j*th*(xj + 0)) )  # magnitude only meaningful
        resp_mag.append((r, th, abs(H)))
print('  |H(theta)| at theta=pi: ' + ', '.join('r%d=%.4f' % (r, [m for rr,t,m in resp_mag if rr==r and abs(t-math.pi)<1e-9][0]) for r in [0,64,128,192,255]))

# ---------- temperament tables ----------
TAMP_VA = 0x1803BB690
fo = pe.va2fo(TAMP_VA)
rows = []
for i in range(5):
    cents = struct.unpack_from('<12i', pe.data, fo + i*0x30)
    rows.append(list(cents))
print('temperament @0x%X:' % TAMP_VA)
for i, r in enumerate(rows):
    print('   row%d: %s' % (i, r))
# choices string
ch = pe.read_cstr(0x1803BBF60, 400)
print('temperament choices str:', repr(ch))

SCALE_VA = 0x1803BB780
fo = pe.va2fo(SCALE_VA)
srows = []
for i in range(19):
    vals = struct.unpack_from('<12i', pe.data, fo + i*0x30)
    srows.append(list(vals))
print('scale-pattern @0x%X: %d rows' % (SCALE_VA, len(srows)))
for i, r in enumerate(srows):
    print('   row%2d: %s' % (i, r))

for va, lbl in [(0x1803BB610, 'keymap'), (0x1803BB660, 'ionian ref'), (0x1803BB60C, 'userscale')]:
    fo = pe.va2fo(va)
    vals = struct.unpack_from('<17i', pe.data, fo)
    print('%s @0x%X: %s' % (lbl, va, list(vals)))

json.dump({'kernel_va': hex(KERNEL_VA), 'rows': K.tolist(),
           'temperament': rows, 'temperament_choices': ch,
           'scale_patterns': srows,
           'keymap': list(struct.unpack_from('<17i', pe.data, pe.va2fo(0x1803BB610))),
           'ionian_ref': list(struct.unpack_from('<17i', pe.data, pe.va2fo(0x1803BB660))),
           'userscale': list(struct.unpack_from('<17i', pe.data, pe.va2fo(0x1803BB60C)))},
          open(OUT + '/r26_tables.json', 'w'), indent=1)
print('written', OUT + '/r26_tables.json')
print('final hash check:', pe.sha256 == EXPECT_SHA256)
