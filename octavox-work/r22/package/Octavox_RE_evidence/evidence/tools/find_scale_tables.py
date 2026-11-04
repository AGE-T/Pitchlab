import struct
from pathlib import Path

DATA = Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()

SCALES = {
    "ionian": [0, 2, 4, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "mixo": [0, 2, 4, 5, 7, 9, 10],
    "aeolian": [0, 2, 3, 5, 7, 8, 10],
    "locrian": [0, 1, 3, 5, 6, 8, 10],
    "harm": [0, 2, 3, 5, 7, 8, 11],
    "mel": [0, 2, 3, 5, 7, 9, 11],
    "chrom": list(range(12)),
    "whole": [0, 2, 4, 6, 8, 10],
    "pentamaj": [0, 2, 4, 7, 9],
    "pentamin": [0, 3, 5, 7, 10],
    "enigma": [0, 1, 4, 6, 8, 10, 11],
    "neap": [0, 1, 3, 5, 7, 8, 11],
    "hung": [0, 2, 3, 6, 7, 8, 11],
}


def find_all(pat, limit=4):
    hits = []
    i = DATA.find(pat)
    while i != -1 and len(hits) < limit:
        hits.append(i)
        i = DATA.find(pat, i + 1)
    return hits


def rep12(deg):
    out = list(deg)
    while len(out) < 12:
        out.append(out[len(out) - len(deg)] + 12)
    return out[:12]


print("== 12 semitone ratios (float32) ==")
ratios = [2.0 ** (n / 12.0) for n in range(12)]
for name, fmt in (("f32", "<12f"), ("f64", "<12d")):
    h = find_all(struct.pack(fmt, *ratios))
    print(name, [hex(x) for x in h])

print("== 12 semitone ints 0..11 ==")
print("i32", [hex(x) for x in find_all(struct.pack("<12i", *range(12)))])
print("bytes", [hex(x) for x in find_all(bytes(range(12)))])

print("== per-scale degree lists ==")
for name, deg in SCALES.items():
    p12 = rep12(deg)
    for tag, pat in (
        ("i32_7", struct.pack("<%di" % len(deg), *deg)),
        ("i32_12", struct.pack("<12i", *p12)),
        ("f32_7", struct.pack("<%df" % len(deg), *[float(x) for x in deg])),
        ("f32_12", struct.pack("<12f", *[float(x) for x in p12])),
        ("b_7", bytes(deg)),
        ("b_12", bytes(p12)),
    ):
        h = find_all(pat)
        if h:
            print(name, tag, [hex(x) for x in h])
