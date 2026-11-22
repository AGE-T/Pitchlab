"""Step 5: read .rdata constants/strings used by stereomixer3 ctor + process; dump port-group vtables."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import struct

b = Bin()

print("== strings / constants ==")
for va, note in [
    (0x1804082f8, "7-char param name (dword+word+byte pattern)"),
    (0x1804082fc, "+4 word"),
    (0x1804082fe, "+6 byte"),
    (0x180408348, "1 byte const"),
    (0x180408468, "5-char name"),
    (0x180408340, "double default for [+0x54]"),
    (0x180412c90, "xmm9 const in process"),
    (0x18041213c, "min clamp const in process"),
]:
    raw = b.read(va, 16)
    s = b.cstr(va, 32)
    d = struct.unpack("<d", raw[:8])[0] if raw else None
    f = struct.unpack("<f", raw[:4])[0] if raw else None
    print(f"  {va:#x} ({note}): ascii={raw!r} cstr={s!r} f32={f!r} f64={d!r}")

print("\n== param descriptor string 0x1803b92e8 ==")
print("  ", b.cstr(0x1803b92e8, 64))
print("== port name table 0x1803b7970 ==")
for i in range(8):
    p = b.u64(0x1803b7970 + 8 * i)
    print(f"  [{i}] {p:#x} -> {b.cstr(p, 32)!r}")

print("\n== vtable 0x18040a0d8 (port-group, 2/4 ports) ==")
for k in range(8):
    va = 0x18040a0d8 + 8 * k
    fn = b.u64(va)
    sec = b.sec_of(fn) if fn else None
    print(f"  slot[{k}] @ {va:#x} -> {fn:#x} ({sec})")

print("\n== vtable 0x18040a218 (port-group, 3 ports) ==")
for k in range(8):
    va = 0x18040a218 + 8 * k
    fn = b.u64(va)
    sec = b.sec_of(fn) if fn else None
    print(f"  slot[{k}] @ {va:#x} -> {fn:#x} ({sec})")
