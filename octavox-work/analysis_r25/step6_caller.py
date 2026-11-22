"""Step 6: caller context of 0x1800d7f10 in worker tree FUN_180103a10 (region 0x180104b60..0x180104d70)."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
print("== FUN_180103a10 region 0x180104a00..0x180104d70 (voice 8 tail + mix + taps) ==")
insns = b.disasm(0x180104a00, 0x180104d70 - 0x180104a00)
print(b.fmt(insns))
