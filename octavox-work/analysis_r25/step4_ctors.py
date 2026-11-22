"""Step 4: full disasm of stereomixer3 sub-ctors 0x1800d7400 (2110B) and 0x1800d8230 (328B) + wrapper 0x180137720."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()

for start, end, name in [
    (0x180137720, 0x180137783, "stereomixer3 wrapper ctor"),
    (0x1800d8230, 0x1800d8378, "subctor A (module id 0x152)"),
    (0x1800d7400, 0x1800d7c3e, "subctor B (vtable 0x180409E20)"),
]:
    print(f"\n{'='*70}\n== {name}: {start:#x}..{end:#x} ==\n{'='*70}")
    insns = b.disasm(start, end - start)
    print(b.fmt(insns))
