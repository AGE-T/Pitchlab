"""Step 14: find module-TYPE table: scan .rdata/.data for qword pointers to name strings;
the type table maps names->add_fn. Identify entry for 'pan'."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
data = b.data

# scan .rdata and .data for qword == ptr to 'pan' name candidates
for name_va in (0x180408470, 0x1804078fe, 0x180409820):
    print(f"== qword refs to {name_va:#x} (in .rdata/.data) ==")
    for secname in (".rdata", ".data", "_RDATA"):
        sec = [s for s in b.secs if s[0] == secname]
        if not sec:
            continue
        sec = sec[0]
        raw, size = sec[3], sec[4]
        target = struct.pack("<Q", name_va)
        i = raw
        while True:
            j = data.find(target, i, raw + size)
            if j < 0:
                break
            va = b.off2va(j)
            print(f"  ref at {va:#x} ({secname})")
            i = j + 1
