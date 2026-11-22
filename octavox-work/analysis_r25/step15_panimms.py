"""Step 15: scan .text for inline 'pan' string immediates (SSO string construction)."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
data = b.data
text = [s for s in b.secs if s[0] == ".text"][0]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# 'pan\0' = 70 61 6e 00 ; 'pan1' = 70 61 6e 31 ; 'pans' = 70 61 6e 73
pats = [b"pan\x00", b"pan1", b"pan2", b"pan3", b"pan8", b"pans", b"pan~", b"pan%", b"pans\x00"]
for pat in pats:
    i = text[3]
    hits = []
    while True:
        j = data.find(pat, i, text[3] + text[4])
        if j < 0:
            break
        va = b.off2va(j)
        hits.append(va)
        i = j + 1
    if hits:
        print(f"{pat!r}: {[hex(h) for h in hits[:20]]} ({len(hits)} total)")
