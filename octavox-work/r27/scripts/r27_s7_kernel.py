"""R27 S7: kernel 256x6 — further generator characterisation.

Tests beyond R26's eliminations (Farrow, LS-FD, windowed-sinc, Gaussian):
1. Row-level min-phase test (root locations via z-transform of each row)
2. Equiripple analysis (Chebyshev error structure of the fractional-delay response)
3. Row-shift structure: is row r+1 related to row r by a fixed impulse response?
4. Total-phase/group-delay profile at DC.
"""
import sys, os, json
import numpy as np

K = np.load('/home/z/my-project/octavox-work/r26/audit/evidence/r26_kernel_table.npy')
print('kernel shape', K.shape, 'dtype', K.dtype)
n_rows, n_taps = K.shape

out = {}

# --- 1. min-phase test per row (roots of H(z) inside/outside unit circle) ---
# H(z) = sum h[k] z^{-k}; roots of polynomial in z^{-1} == roots of h reversed poly
inside_frac = []
for r in range(n_rows):
    h = K[r].astype(np.float64)
    # polynomial coefficients (h[0] + h[1] z + ... ) -> roots via numpy
    roots = np.roots(h[::-1])  # roots of sum h[k] z^{n-1-k}
    if len(roots):
        mag = np.abs(roots)
        inside_frac.append(float(np.mean(mag < 1.0)))
    else:
        inside_frac.append(1.0)
out['minphase_inside_fraction'] = {
    'mean': float(np.mean(inside_frac)),
    'min': float(np.min(inside_frac)),
    'max': float(np.max(inside_frac)),
    'n_rows_fully_inside': int(sum(1 for f in inside_frac if f == 1.0)),
}

# --- 2. row-difference structure: K[r+1] - K[r] vs a fixed kernel? ---
D = np.diff(K, axis=0)
out['row_diff_energy'] = {
    'mean_abs': float(np.mean(np.abs(D))),
    'max_abs': float(np.max(np.abs(D))),
    'first_row_diff': D[0].tolist(),
    'is_constant': bool(np.allclose(D, D[0])),
}

# --- 3. equiripple: per-row frequency response error vs ideal FD ---
# ideal: H_fd(w) = e^{-j w d}, d = fractional delay of the row
# row r => fraction f = r/256 (per R26 read path). taps=6 => d_int = 2.5 offset?
# R26: phase = floor(frac*256)&255 over rows; treat row r as fraction r/256 of one sample.
# The kernel likely covers [f-2.5 .. f+2.5] taps; test with d0 = r/256:
freqs = np.linspace(0, np.pi, 513)
worst_ripple = []
for r in [0, 32, 64, 96, 128, 160, 192, 224, 255]:
    h = K[r].astype(np.float64)
    H = np.fft.fft(np.concatenate([h, np.zeros(512 - 6)]))
    Hw = H[:257]
    w = np.linspace(0, np.pi, 257)
    # magnitude error vs unit
    mag = np.abs(Hw)
    worst_ripple.append(float(np.max(np.abs(mag - 1.0))))
out['mag_error_vs_unit_rows'] = dict(zip([0,32,64,96,128,160,192,224,255], worst_ripple))

# --- 4. group delay at DC per row (sum k*h[k] / sum h[k]) ---
gd = []
for r in range(n_rows):
    h = K[r].astype(np.float64)
    gd.append(float((np.arange(6) * h).sum() / h.sum()))
out['group_delay_dc'] = {
    'row0': gd[0], 'row64': gd[64], 'row128': gd[128], 'row192': gd[192], 'row255': gd[255],
    'monotone': bool(np.all(np.diff(gd) > 0)),
    'gd_slope_per_row': float(np.mean(np.diff(gd))),
}

# --- 5. palindrome re-check + distinct values (reconfirm R26) ---
flat = K.reshape(-1)
out['palindrome'] = bool(np.allclose(flat, flat[::-1]))
out['distinct_values'] = int(len(np.unique(K)))

# --- 6. equiripple structure: alternation of the fractional-delay error ---
# For row 128 (half-sample): compare |H| to the ideal half-delay sinc band
r = 128
h = K[r].astype(np.float64)
H = np.abs(np.fft.fft(np.concatenate([h, np.zeros(506)])))[:257]
w = np.linspace(0, 1, 257)  # normalized to Nyquist
err = H - np.abs(np.sinc(0.5 * w) )  # ideal half-delay magnitude ~ |sinc| shape for 6 taps is NOT this; informational
out['row128_mag_profile'] = {
    'dc': float(H[0]), 'nyquist': float(H[-1]),
    'w_at_3dB': float(w[np.searchsorted(-H, -H[0]/np.sqrt(2))] if H[0] > 0 else 0),
}

print(json.dumps(out, indent=1))
with open('/home/z/my-project/octavox-work/r27/evidence/r27_s7_kernel.json', 'w') as f:
    json.dump(out, f, indent=1)
print('saved r27_s7_kernel.json')
