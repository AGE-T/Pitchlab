#!/usr/bin/env python3
"""R23 Gate 1: legacy-vs-v2 instrument delta over the 360-cell matrix."""
import json
import sys
import time
from collections import Counter


def robust_load(path, attempts=10, delay=0.5):
    last = None
    for i in range(attempts):
        try:
            with open(path, 'rb') as f:
                raw = f.read()
            if len(raw) < 1000:
                raise IOError('suspiciously short read: %d bytes' % len(raw))
            return json.loads(raw)
        except Exception as e:  # noqa: BLE001
            last = e
            sys.stderr.write('robust_load attempt %d failed: %s\n' % (i, e))
            time.sleep(delay)
    raise RuntimeError('unparseable after retries: %s' % last)


def main():
    legacy = robust_load('/tmp/legacy360.json')
    v2 = robust_load('/tmp/v2360.json')['cells']
    assert len(legacy) == len(v2) == 360, (len(legacy), len(v2))

    vmap = {'not_detectable': 'not_detectable',
            'below_audible_floor': 'below_output_floor',
            'safely_transposable': 'valid_transposition'}

    mism = []
    for a, b in zip(legacy, v2):
        key = (a['tone_hz'], a['f0_min'], a['shift_st'], a['mode'])
        ka = (b['tone_hz'], b['f0_min'], b['shift_st'], b['mode'])
        assert key == ka, (key, ka)
        va = vmap.get(a['verdict'],
                      'spacing_not_locked' if a['verdict'] == 'detectable_only'
                      else a['verdict'])
        if va != b['verdict']:
            mism.append((key, a['verdict'], b['verdict']))
    print('verdict mismatches legacy vs v2:', len(mism))
    for m in mism[:12]:
        print('   tone=%.1f floor=%.0f st=%.0f mode=%s : %s -> %s' % m)

    diffs = []
    for a, b in zip(legacy, v2):
        la, lb = a['out_hz_measured'], b['out_hz_measured']
        if la > 0 and lb > 0 and abs(la - lb) > 0.05:
            diffs.append((a['tone_hz'], a['mode'], a['shift_st'], la, lb))
    print('output-freq instrument diffs > 0.05 Hz:', len(diffs))
    for d in diffs[:12]:
        print('   tone=%.1f mode=%s st=%.0f legacy=%.2f v2=%.2f' % d)

    edge = [(a['tone_hz'], a['mode']) for a in legacy
            if a['out_hz_measured'] >= 1990.0]
    print('legacy boundary-2000Hz artifacts:', len(edge))

    print('v2 out_state distribution:', Counter(c['out_state'] for c in v2))
    z1 = sum(1 for a, b in zip(legacy, v2)
             if a['out_hz_measured'] == 0 and b['out_hz_measured'] > 0)
    z2 = sum(1 for a, b in zip(legacy, v2)
             if a['out_hz_measured'] > 0 and b['out_hz_measured'] == 0)
    print('legacy-out=0/v2>0:', z1, '| legacy>0/v2=0:', z2)

    bigd = [(a['tone_hz'], a['mode'], a['shift_st'],
             a['out_hz_measured'], b['out_hz_measured'])
            for a, b in zip(legacy, v2)
            if a['out_hz_measured'] > 0 and b['out_hz_measured'] > 0
            and abs(a['out_hz_measured'] - b['out_hz_measured']) > 1.0]
    print('large (>1 Hz) instrument differences:', len(bigd))
    for d in bigd[:10]:
        print('   tone=%.1f mode=%s st=%.0f legacy=%.3f v2=%.3f' % d)

    # legacy-vs-v2 tolerance comparison on the spacing criterion
    tighter = sum(1 for c in v2 if c['spacing_tol_cents'] < 50.0)
    looser = sum(1 for c in v2 if c['spacing_tol_cents'] >= 50.0)
    print('derived spacing tolerance < 50 cents:', tighter, '| >= 50:', looser)
    print('tol range: %.2f .. %.2f cents' % (
        min(c['spacing_tol_cents'] for c in v2),
        max(c['spacing_tol_cents'] for c in v2)))


if __name__ == '__main__':
    main()
