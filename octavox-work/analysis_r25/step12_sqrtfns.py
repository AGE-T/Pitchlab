"""Step 12: disassemble the sqrt-containing functions in the 0x18013c8..0x1801420 framework region."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
for start, end, name in [
    (0x18013c85e, 0x18013d1fb, "A: 0x18013c85e (sqrt, near coeff-array ctor)"),
    (0x18013dc00, 0x18013dce2, "B: 0x18013dc00 (sqrt)"),
    (0x18013dd34, 0x18013deab, "C: 0x18013dd34 (sqrt)"),
]:
    print(f"\n{'='*70}\n== {name}: {start:#x}..{end:#x} ==\n{'='*70}")
    insns = b.disasm(start, end - start)
    print(b.fmt(insns))
