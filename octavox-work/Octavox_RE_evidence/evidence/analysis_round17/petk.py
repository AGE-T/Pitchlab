"""R17 local PE toolkit: VA<->file-offset mapping + section info for Ox.vst3.

The target binary is NEVER executed; all operations are read-only byte
parsing (the standing static-analysis rule since round 1).
"""
import json
import struct
import sys

BIN = "/home/z/my-project/octavox-work/original/Ox.vst3"

def _assert(cond, msg):
    if not cond:
        raise RuntimeError(msg)

_img = open(BIN, "rb").read()

# --- PE64 header parse -------------------------------------------------------
e_lfanew = struct.unpack_from("<I", _img, 0x3C)[0]
_assert(e_lfanew < len(_img), "bad e_lfanew")
sig = _img[e_lfanew:e_lfanew + 4]
_assert(sig == b"PE\x00\x00", "not a PE file")
coff = e_lfanew + 4
machine, nsec = struct.unpack_from("<HH", _img, coff)[0:2]
opt_size = struct.unpack_from("<H", _img, coff + 16)[0]
opt = coff + 20
magic = struct.unpack_from("<H", _img, opt)[0]
_assert(magic == 0x20B, "not PE32+ (magic=0x%X)" % magic)
image_base = struct.unpack_from("<Q", _img, opt + 24)[0]
sec_tab = opt + opt_size
SECTIONS = []
for i in range(nsec):
    off = sec_tab + 40 * i
    name = _img[off:off + 8].rstrip(b"\x00").decode("ascii", "replace")
    vsize, va, rsize, ptr = struct.unpack_from("<IIII", _img, off + 8)
    SECTIONS.append(dict(name=name, va=va, vsize=vsize, rsize=rsize, ptr=ptr))



def va2fo(va):
    r = va - image_base
    for s in SECTIONS:
        if s["va"] <= r < s["va"] + max(s["vsize"], s["rsize"]):
            fo = s["ptr"] + (r - s["va"])
            if fo < len(_img):
                return fo
    raise RuntimeError("VA 0x%X outside mapped sections" % va)


def read(va, n):
    fo = va2fo(va)
    return _img[fo:fo + n]


def u32(va):
    return struct.unpack("<I", read(va, 4))[0]


def f32s(va, n):
    return struct.unpack("<%df" % n, read(va, 4 * n))


def f64s(va, n):
    return struct.unpack("<%dd" % n, read(va, 8 * n))


def i32s(va, n):
    return struct.unpack("<%di" % n, read(va, 4 * n))


def find_all(pat, limit=64):
    hits, i = [], _img.find(pat)
    while i != -1 and len(hits) < limit:
        hits.append(i)
        i = _img.find(pat, i + 1)
    return hits


def fo2va(fo):
    for s in SECTIONS:
        if s["ptr"] <= fo < s["ptr"] + s["rsize"]:
            return image_base + s["va"] + (fo - s["ptr"])
    return None


def pdata_functions():
    """The exception-directory function table: [(start_va, end_va), ...]."""
    # data directory index 3 = Exception Table
    dd = opt + 112 + 3 * 8  # data directory index 3 = Exception Table
    rva, size = struct.unpack_from("<II", _img, dd)
    out = []
    if rva and size:
        fo = va2fo(image_base + rva)
        for off in range(fo, fo + size, 12):
            s, e, _u = struct.unpack_from("<III", _img, off)
            if s and e:
                out.append((image_base + s, image_base + e))
    return out


if __name__ == "__main__":
    print("image_base = 0x%X" % image_base)
    for s in SECTIONS:
        print("%-8s va=0x%08X vsize=0x%X rsize=0x%X ptr=0x%X"
              % (s["name"], image_base + s["va"], s["vsize"], s["rsize"], s["ptr"]))
    pf = pdata_functions()
    print(".pdata functions: %d" % len(pf))
