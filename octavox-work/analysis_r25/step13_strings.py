"""Step 13: find pan/gain param-name strings in .rdata and locate code refs to them."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
rd = [s for s in b.secs if s[0] == ".rdata"][0]
data = b.data

# search for param name strings
names = []
for pat in [b"pan1\x00", b"pan2\x00", b"pan8\x00", b"gain1\x00", b"gain2\x00", b"gain8\x00",
            b"ninputs\x00", b"clipvalue\x00", b"time_constant\x00", b"ramp_time\x00",
            b"left\x00", b"right\x00", b"stereomixer3\x00", b"stereomixer\x00", b"mix\x00",
            b"pans\x00", b"gains\x00"]:
    start = rd[3]
    end = start + rd[4]
    i = start
    while True:
        j = data.find(pat, i, end)
        if j < 0:
            break
        va = b.off2va(j)
        names.append((va, pat))
        print(f"  {va:#x}: {pat!r}  ctx={data[max(0,j-32):j+len(pat)+16]!r}")
        i = j + 1
