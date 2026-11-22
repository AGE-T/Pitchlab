"""Step 2a: dump vtable 0x180409E20 and its neighbors; disassemble candidate 0x1800d7f10."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB

b = Bin()

print("== vtable 0x180409E20 dump (16 slots) ==")
for k in range(16):
    va = 0x180409E20 + 8 * k
    fn = b.u64(va)
    sec = b.sec_of(fn) if fn else None
    print(f"  slot[{k}] @ {va:#x} -> {fn:#x} ({sec})")

print("\n== vtable neighbors 0x180409E60 (framework family) ==")
for k in range(12):
    va = 0x180409E60 + 8 * k
    fn = b.u64(va)
    sec = b.sec_of(fn) if fn else None
    print(f"  slot[{k}] @ {va:#x} -> {fn:#x} ({sec})")

# find end of 0x1800d7f10: use .pdata unwind info
print("\n== .pdata lookup for 0x1800d7f10 ==")
found = None
import struct
pdata_off = b.va2off(0x180004cd000 + IB)  # not needed; scan directly
# .pdata entries: 12 bytes: start rva, end rva, unwind rva
sec = [s for s in b.secs if s[0] == ".pdata"][0]
n = sec[4] // 12
base_raw, base_va = sec[3], sec[1]
target_rva = 0x1800d7f10 - IB
for i in range(n):
    off = base_raw + 12 * i
    s_rva, e_rva = struct.unpack_from("<II", b.data, off)
    if s_rva <= target_rva < e_rva:
        print(f"  function {IB+s_rva:#x} .. {IB+e_rva:#x} (size {e_rva-s_rva})")
        found = (IB + s_rva, IB + e_rva)
        break

if found:
    start, end = found
    print(f"\n== disasm 0x1800d7f10 .. {end:#x} ==")
    insns = b.disasm(start, end - start)
    print(b.fmt(insns))
