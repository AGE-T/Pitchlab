"""Step 19: find all `call qword ptr [rax+0x60]` sites (engine setModuleParam) in .text."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
data = b.data
text = [s for s in b.secs if s[0] == ".text"][0]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

# find FF /2 with modrm disp8 0x60 on rax: FF 50 60
pat = bytes([0xFF, 0x50, 0x60])
i = text[3]
hits = []
while True:
    j = data.find(pat, i, text[3] + text[4])
    if j < 0:
        break
    va = b.off2va(j)
    hits.append(va)
    i = j + 1

print(f"FF 50 60 (call [rax+0x60]) sites: {len(hits)}")
for h in hits[:60]:
    print(f"  {h:#x}")
