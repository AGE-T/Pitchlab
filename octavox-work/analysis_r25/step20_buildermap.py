"""Step 20: extract (slot, add_fn, module_id) map from the giant composite builder."""
import sys, struct
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

# find builder function bounds (contains 0x1800f8834)
pd = [s for s in b.secs if s[0] == ".pdata"][0]
n = pd[4] // 12
b0 = b1 = None
for i in range(n):
    off = pd[3] + 12 * i
    s_rva, e_rva = struct.unpack_from("<II", b.data, off)
    if s_rva and IB + s_rva <= 0x1800f8834 < IB + e_rva:
        b0, b1 = IB + s_rva, IB + e_rva
        break
print(f"builder fn: {b0:#x}..{b1:#x} size {b1-b0:#x}")

insns = list(md.disasm(b.read(b0, b1 - b0), b0))
# pattern: lea rcx, [r14+slot]; call add_fn  ; preceded by mov r9d, id
modules = []
cur_id = None
cur_slot = None
for idx, i in enumerate(insns):
    if i.mnemonic == "mov" and i.op_str.startswith("r9d, "):
        try:
            cur_id = int(i.op_str.split(", ")[1], 16)
        except ValueError:
            cur_id = None
    if i.mnemonic == "lea" and i.op_str.startswith("rcx, [r14 + "):
        try:
            cur_slot = int(i.op_str.split("+ ")[1].rstrip("]"), 16)
        except Exception:
            cur_slot = None
    if i.mnemonic == "call" and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
        tgt = i.operands[0].imm
        if cur_slot is not None and cur_id is not None and (i.address - (insns[idx-1].address if idx else 0)) < 0x40:
            modules.append((cur_id, cur_slot, tgt, i.address))
            cur_slot = None
            cur_id = None

print(f"extracted {len(modules)} module constructions")
for mid, slot, fn, at in modules[:400]:
    print(f"  id={mid:#x} slot=r14+{slot:#x} add_fn={fn:#x} (@{at:#x})")
