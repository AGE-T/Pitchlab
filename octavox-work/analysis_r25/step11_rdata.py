"""Step 11: dump .rdata double regions around the pi/4 constants; disassemble sqrt functions in 0x18013xxxx region."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()

print("== .rdata doubles around 0x180414500..0x180415380 ==")
for va in range(0x180414500, 0x180415390, 8):
    d = b.f64(va)
    f = b.f32(va)
    if d is not None and (0.0001 < abs(d) < 100 or f in (0.5,)):
        print(f"  {va:#x}: f64={d!r:24} f32={f!r}")
