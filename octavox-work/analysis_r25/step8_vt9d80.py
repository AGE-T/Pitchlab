"""Step 8: dump vtable 0x180409d80 (L/R coeff-array sub-object) + disassemble its slots."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()
print("== vtable 0x180409d80 ==")
slots = []
for k in range(12):
    va = 0x180409d80 + 8 * k
    fn = b.u64(va)
    sec = b.sec_of(fn) if fn else None
    slots.append(fn)
    print(f"  slot[{k}] @ {va:#x} -> {fn:#x} ({sec})")

# disassemble each .text slot briefly
import capstone
for k, fn in enumerate(slots):
    if fn and b.sec_of(fn) == ".text":
        print(f"\n== slot[{k}] target {fn:#x} (first 40 insns) ==")
        try:
            insns = b.disasm_until(fn, max_instr=40)
            print(b.fmt(insns))
        except Exception as e:
            print("  err", e)
