#!/usr/bin/env python3
"""R21 harmony mapper reference-vector generator (independent Python
implementation of the recovered rules).

Generates evidence/analysis_round21/r21_harmony_vectors.json from the
recovered static-RE tables/rules (rounds 17/18; see
docs/R21_HARMONY_MAPPER_SPEC.md). The C++ module (native/src/harmony.cpp)
is verified against these vectors by octavox_r21_harmony --verify: the two
implementations were written from the same spec and must agree to <=1e-12
relative (bit-equality on one platform; libm differences only).

The walk arithmetic reproduced here (addresses in the spec):
  sel = 14 - v            (v = the interval mux value; "+" labels -> sel<0
                            -> the DOWN branch; "-" labels -> UP)
  q   = sel // 7          (C truncating division)
  UP:   start = min(sel % 7, size-1)
        row0  = grid[start % size] + 1200*q + pcent
        row j+1 = int(wrapUp(grid[(start+j) % size]) + pcent) - grid[j] + 1200*q
                  wrapUp(t, b) = t if t >= b else t + 1200
  DOWN: m     = min(7*q - sel, size-1)
        start = (1-q)*size - m
        row0  = (grid[start % size] - 1200) % 1200 + 1200*q + pcent   (C mod)
        row j+1 = int(wrapDown(grid[(start+j) % size]) + pcent) - grid[j] + 1200*q
                  wrapDown(t, b) = t if t <= b else t - 1200
  rows beyond the grid size repeat the row-0 value (table[size+1..7]).
  keyRatio = exp(+-cents[|keymap[key]-9|] * ln2/1200)   (- below A, + above)
  ratio_v  = keyRatio * exp((voiceCents_v + tune) * ln2/1200)
"""
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "r21_harmony_vectors.json")

# ---- OBS tables (verbatim .rdata dumps) -----------------------------------
TEMPERAMENTS = {
    "equal":       [0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100],
    "just_major":  [0, 112, 204, 316, 386, 498, 590, 702, 814, 884, 996, 1088],
    "just_minor":  [0, 71, 182, 316, 386, 498, 590, 702, 814, 884, 996, 1088],
    "pythagorean": [0, 90, 204, 294, 408, 498, 612, 702, 792, 906, 996, 1110],
    "meantone":    [0, 76, 193, 310, 386, 503, 580, 697, 772, 890, 1007, 1083],
}

SCALE_PATTERNS = {
    "ionian":         [1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7],
    "ionian_dup":     [1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7],
    "dorian":         [1, 0, 2, 3, 0, 4, 0, 5, 0, 6, 7, 0],
    "phrygian":       [1, 2, 0, 3, 0, 4, 0, 5, 6, 0, 7, 0],
    "lydian":         [1, 0, 2, 0, 3, 0, 4, 5, 0, 6, 0, 7],
    "mixolydian":     [1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 7, 0],
    "aeolian":        [1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 7, 0],
    "aeolian_dup":    [1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 7, 0],
    "harmonic_minor": [1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 0, 7],
    "melodic_minor":  [1, 0, 2, 3, 0, 4, 0, 5, 0, 6, 0, 7],
    "chromatic":      [1, 99, 2, 99, 3, 4, 99, 5, 99, 6, 99, 7],
    "whole_tone":     [1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0],
    "penta_major":    [2, 0, 3, 0, 4, 0, 0, 6, 0, 7, 0, 0],
    "penta_minor":    [2, 0, 0, 3, 0, 4, 0, 6, 0, 0, 7, 0],
    "enigmatic":      [1, 2, 0, 0, 3, 0, 4, 0, 5, 0, 6, 7],
    "neapolitan":     [1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7],
    "hungarian":      [1, 0, 2, 3, 0, 0, 4, 5, 6, 0, 0, 7],
}

ROW_INDEX = {
    "ionian": 0, "ionian_dup": 8, "dorian": 2, "phrygian": 3, "lydian": 4,
    "mixolydian": 5, "aeolian": 6, "aeolian_dup": 9, "harmonic_minor": 10,
    "melodic_minor": 11, "chromatic": 12, "whole_tone": 13,
    "penta_major": 14, "penta_minor": 15, "enigmatic": 16,
    "neapolitan": 17, "hungarian": 18,
}

KEYMAP = [0, 1, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 8, 9, 10, 10, 11]
KEY_NAMES = ["C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb",
             "G", "G#", "Ab", "A", "A#", "Bb", "B"]

INTERVAL_MUX = [0, 1, 3, 3, 4, 5, 7, 7, 8, 10, 10, 11, 12, 14,
                14, 15, 17, 17, 18, 19, 21, 21, 22, 24, 24, 25, 26, 28, 28]
INTERVAL_LABELS = ["-2 oct", "-14th", "-13th", "-12th", "-11th", "-10th",
                   "-9th", "-8th", "-7th", "-6th", "-5th", "-4th", "-3rd",
                   "-2nd", "unison", "+2nd", "+3rd", "+4th", "+5th", "+6th",
                   "+7th", "+8th", "+9th", "+10th", "+11th", "+12th",
                   "+13th", "+14th", "+2 oct"]

SCAL_MUX = [1, 2, 3, 4, 5, 6, 7, 10, 11, 14, 15, 16, 17, 18, 13]

LN2 = math.log(2)


def cdiv(a, b):
    q = a // b
    if q < 0 and q * b != a:
        q += 1
    return q


def cmod(a, b):
    return a - cdiv(a, b) * b


def build_grid(pattern, cents):
    grid = []
    prev = 0
    for slot in range(12):
        p = pattern[slot]
        if p == 0:
            continue
        if p == 99:
            grid.append(cents[slot])
        else:
            run = p - prev
            grid.extend([cents[slot]] * run)
            prev = p
    return grid


def walk(sel, grid, pcent):
    size = len(grid)
    out = [0.0] * 8
    q = cdiv(sel, 7)
    oct_c = 1200.0 * q
    if sel >= 0:
        start = min(cmod(sel, 7), size - 1)
        out[0] = float(grid[cmod(start, size)]) + oct_c + pcent
        for j in range(min(size, 7)):
            target = grid[cmod(start + j, size)]
            base = grid[j]
            wrapped = target if target >= base else target + 1200
            delta = int(wrapped + pcent) - base
            out[j + 1] = float(delta) + oct_c
    else:
        m = min(7 * q - sel, size - 1)
        start = (1 - q) * size - m
        g0 = grid[cmod(start, size)] - 1200
        r0 = cmod(g0, 1200)
        out[0] = float(r0) + oct_c + pcent
        for j in range(min(size, 7)):
            target = grid[cmod(start + j, size)]
            base = grid[j]
            wrapped = target if target <= base else target - 1200
            delta = int(wrapped + pcent) - base
            out[j + 1] = float(delta) + oct_c
    for j in range(size + 1, 8):
        out[j] = out[0]
    return out


def key_ratio(key, cents):
    k = KEYMAP[key]
    if k < 9:
        kc = -cents[9 - k]
    elif k > 9:
        kc = cents[k - 9]
    else:
        kc = 0
    return math.exp(float(kc) * LN2 / 1200)


def map_impl(key, grid, temper_cents, sel, fine, tune, voice):
    rows = walk(sel, grid, fine)
    vc = rows[0] if voice == 0 else rows[voice]
    kr = key_ratio(key, temper_cents)
    return kr * math.exp((vc + tune) * LN2 / 1200)


def map_row(key, scale_name, temper, interval, fine, tune, voice=0):
    cents = TEMPERAMENTS[temper]
    grid = build_grid(SCALE_PATTERNS[scale_name], cents)
    sel = 14 - INTERVAL_MUX[interval]
    return map_impl(key, grid, cents, sel, fine, tune, voice)


def map_choice(key, choice_idx, temper, interval, fine, tune, voice=0):
    cents = TEMPERAMENTS[temper]
    mux_out = SCAL_MUX[choice_idx]
    row_name = [n for n, r in ROW_INDEX.items() if r == mux_out][0]
    grid = build_grid(SCALE_PATTERNS[row_name], cents)
    if mux_out in (14, 15):
        sel = 14 - interval  # the raw-index switch (un-bumped)
    else:
        sel = 14 - INTERVAL_MUX[interval]
    return map_impl(key, grid, cents, sel, fine, tune, voice)


vectors = []
vid = [0]


def add(key, scale, temper, interval, fine, tune, voice, expected,
        comment, api="row", scale_choice=None):
    vid[0] += 1
    vectors.append({
        "id": "v%03d" % vid[0],
        "api": api,
        "key": key,
        "key_name": KEY_NAMES[key],
        "scale": scale,
        "scale_row": ROW_INDEX[scale],
        "scale_choice": scale_choice,
        "temperament": temper,
        "interval": interval,
        "interval_label": INTERVAL_LABELS[interval],
        "voice": voice,
        "fine_cents": fine,
        "global_tune_cents": tune,
        "expected_ratio": expected,
        "comment": comment,
    })


def main():
    rng = random.Random(20261102)

    # 1. all 17 unique scale rows x representative intervals x equal x A
    rep = [14, 1, 12, 6, 8, 26, 2, 20, 5, 27, 28, 13, 0]
    seen = set()
    for scale in SCALE_PATTERNS:
        if scale in seen:
            continue
        seen.add(scale)
        for iv in rep:
            r = map_row(13, scale, "equal", iv, 0.0, 0.0, 0)
            add(13, scale, "equal", iv, 0.0, 0.0, 0, r,
                "row %s, %s, voice 1" % (scale, INTERVAL_LABELS[iv]))

    # 2. all 29 intervals: ionian row / chromatic row / whole-tone row
    for iv in range(29):
        r = map_row(13, "ionian", "equal", iv, 0.0, 0.0, 0)
        add(13, "ionian", "equal", iv, 0.0, 0.0, 0, r,
            "ionian voice 1 %s" % INTERVAL_LABELS[iv])
    for iv in range(29):
        r = map_row(13, "chromatic", "equal", iv, 0.0, 0.0, 0)
        add(13, "chromatic", "equal", iv, 0.0, 0.0, 0, r,
            "chromatic ROW voice 1 %s (mux walk, 12 grid)" % INTERVAL_LABELS[iv])
    for iv in range(29):
        r = map_row(13, "whole_tone", "equal", iv, 0.0, 0.0, 0)
        add(13, "whole_tone", "equal", iv, 0.0, 0.0, 0, r,
            "whole-tone ROW voice 1 %s (mux walk, 6 grid)" % INTERVAL_LABELS[iv])
    # all 29 x the "chromatic"/"whole tone" CHOICES (raw-index switch)
    for iv in range(29):
        r = map_choice(13, 9, "equal", iv, 0.0, 0.0, 0)
        add(13, "penta_major", "equal", iv, 0.0, 0.0, 0, r,
            "chromatic CHOICE (raw-index, penta-maj grid) %s"
            % INTERVAL_LABELS[iv], api="choice", scale_choice=9)
    for iv in range(29):
        r = map_choice(13, 10, "equal", iv, 0.0, 0.0, 0)
        add(13, "penta_minor", "equal", iv, 0.0, 0.0, 0, r,
            "whole-tone CHOICE (raw-index, penta-min grid) %s"
            % INTERVAL_LABELS[iv], api="choice", scale_choice=10)

    # 3. all 5 temperaments x representative set (voices 1 + 3)
    for temper in TEMPERAMENTS:
        for iv in [14, 12, 6, 18, 21]:
            for voice in (0, 2):
                r = map_row(13, "ionian", temper, iv, 0.0, 0.0, voice)
                add(13, "ionian", temper, iv, 0.0, 0.0, voice, r,
                    "%s ionian voice %d %s" % (temper, voice + 1,
                                               INTERVAL_LABELS[iv]))

    # 4. key transposition: C, E, Gb, A x unison/-5th x equal/just
    for key in (0, 5, 9, 16):
        for iv in (14, 18):
            for temper in ("equal", "just_major"):
                r = map_row(key, "ionian", temper, iv, 0.0, 0.0, 0)
                add(key, "ionian", temper, iv, 0.0, 0.0, 0, r,
                    "key %s %s %s voice 1" % (KEY_NAMES[key], temper,
                                              INTERVAL_LABELS[iv]))

    # 5. diatonic snapping cases (the higher voices' base degrees)
    for scale in ("penta_major", "penta_minor", "enigmatic", "hungarian",
                  "harmonic_minor", "whole_tone", "chromatic"):
        for iv in (15, 16, 17, 6):
            for voice in (1, 2, 3):
                r = map_row(13, scale, "equal", iv, 0.0, 0.0, voice)
                add(13, scale, "equal", iv, 0.0, 0.0, voice, r,
                    "snapping %s voice %d %s" % (scale, voice + 1,
                                                 INTERVAL_LABELS[iv]))

    # 6. fine cents + global tune incl. boundaries
    for fine in (-50.0, -1.0, 0.5, 25.0, 50.0):
        r = map_row(13, "ionian", "equal", 14, fine, 0.0, 0)
        add(13, "ionian", "equal", 14, fine, 0.0, 0, r, "fine cents %.2f" % fine)
    for tune in (-100.0, -12.0, 7.0, 100.0):
        r = map_row(13, "ionian", "equal", 14, 0.0, tune, 0)
        add(13, "ionian", "equal", 14, 0.0, tune, 0, r, "global tune %.2f" % tune)
    r = map_row(13, "ionian", "equal", 16, 13.7, -6.3, 4)
    add(13, "ionian", "equal", 16, 13.7, -6.3, 4, r, "fine+tune combo voice 5")

    # 7. boundary selections: intervals 0 and 28 at voices 1 and 8
    for iv in (0, 28):
        for voice in (0, 7):
            r = map_row(13, "ionian", "equal", iv, 0.0, 0.0, voice)
            add(13, "ionian", "equal", iv, 0.0, 0.0, voice, r,
                "boundary interval %s voice %d" % (INTERVAL_LABELS[iv], voice + 1))
    r = map_row(13, "hungarian", "equal", 14, 0.0, 0.0, 0)
    add(13, "hungarian", "equal", 14, 0.0, 0.0, 0, r,
        "hungarian ROW direct (mux-unreachable; documented irregularity)")

    # 8. musical anchors (closed forms verified in the generator):
    # ionian grid (equal) = [0,200,400,500,700,900,1100]; voice-1 row0:
    #  '-3rd'  (s=-2, v=12, sel=+2): start=2 -> 400c -> 2^(1/3)   = M3
    #  '-5th'  (s=-4, v=10, sel=+4): start=4 -> 700c -> 2^(7/12)  = P5
    #  '-8th'  (s=-7, v=7,  sel=+7): q=1,start=0 -> 1200c -> 2.0  = P8
    #  '+3rd'  (s=+2, v=17, sel=-3): start=4 -> (700-1200)%1200 = -500c
    #  'unison'(s=0,  v=14, sel=0)  : 0c -> 1.0
    anchors = [
        (14, 1.0, "unison = 1.0"),
        (10, 2.0 ** (7.0 / 12.0), "'-5th' (s=-4) walks UP to 700c = P5 = 2^(7/12)"),
        (7, 2.0, "'-8th' (s=-7) walks UP to 1200c = P8 = 2.0"),
        (12, 2.0 ** (1.0 / 3.0), "'-3rd' (s=-2) walks UP to 400c = M3 = 2^(1/3)"),
        (16, 2.0 ** (-5.0 / 12.0), "'+3rd' (s=+2, bumped) walks DOWN to -500c = -P4"),
    ]
    for iv, want, comment in anchors:
        r = map_row(13, "ionian", "equal", iv, 0.0, 0.0, 0)
        assert abs(r - want) < 1e-12, "anchor mismatch %s: %r vs %r" % (
            INTERVAL_LABELS[iv], r, want)
        add(13, "ionian", "equal", iv, 0.0, 0.0, 0, r,
            "ANCHOR %s: %s" % (INTERVAL_LABELS[iv], comment))
    r = map_row(13, "ionian", "meantone", 10, 0.0, 0.0, 0)
    want = 2.0 ** (697.0 / 1200.0)
    assert abs(r - want) < 1e-12
    add(13, "ionian", "meantone", 10, 0.0, 0.0, 0, r,
        "ANCHOR meantone '-5th' = 2^(697/1200)")
    r = map_row(13, "ionian", "just_major", 12, 0.0, 0.0, 0)
    want = 2.0 ** (386.0 / 1200.0)
    assert abs(r - want) < 1e-12
    add(13, "ionian", "just_major", 12, 0.0, 0.0, 0, r,
        "ANCHOR just '-3rd' = 2^(386/1200) (just M3 ~= 5/4)")
    r = map_row(0, "ionian", "equal", 14, 0.0, 0.0, 0)
    want = 2.0 ** (-900.0 / 1200.0)
    assert abs(r - want) < 1e-12
    add(0, "ionian", "equal", 14, 0.0, 0.0, 0, r,
        "ANCHOR key C: keyRatio = 2^(-900/1200)")

    # 9. determinism duplicates (bitwise identical re-computation)
    dups = []
    for _ in range(6):
        key = rng.randrange(17)
        scale = rng.choice(sorted(SCALE_PATTERNS.keys()))
        temper = rng.choice(sorted(TEMPERAMENTS.keys()))
        iv = rng.randrange(29)
        fine = round(rng.uniform(-50, 50), 3)
        tune = round(rng.uniform(-100, 100), 3)
        voice = rng.randrange(8)
        r = map_row(key, scale, temper, iv, fine, tune, voice)
        dups.append((key, scale, temper, iv, fine, tune, voice, r))
    for (key, scale, temper, iv, fine, tune, voice, r) in dups:
        r2 = map_row(key, scale, temper, iv, fine, tune, voice)
        assert r == r2, "generator determinism broken"
        add(key, scale, temper, iv, fine, tune, voice, r,
            "determinism duplicate (%s)" % temper)

    out = {
        "spec_version": "r21-harmony-mapper-v1",
        "formula": "ratio = exp(cents_total * ln(2) / 1200)",
        "walk_law": "sel = 14 - mux[interval]; q = sel/7 (C trunc); "
                    "up: start=min(sel%7,size-1); "
                    "down: start=(1-q)*size-min(7q-sel,size-1); "
                    "rows>size repeat row0",
        "key_law": "keyRatio = exp(+-temperament[|keymap[key]-9|]*ln2/1200)",
        "temperaments": TEMPERAMENTS,
        "scales": SCALE_PATTERNS,
        "keys": KEY_NAMES,
        "keymap": KEYMAP,
        "interval_mux": INTERVAL_MUX,
        "scal_mux": SCAL_MUX,
        "vectors": vectors,
    }
    with open(OUT, "w") as f:
        json.dump(out, f, indent=1)
    print("wrote %s with %d vectors" % (OUT, len(vectors)))
    cats = {}
    for v in vectors:
        c = v["comment"].split(" (")[0].split(":")[0][:20]
        cats[c] = cats.get(c, 0) + 1
    print("categories (rough): %d groups" % len(cats))


if __name__ == "__main__":
    main()
