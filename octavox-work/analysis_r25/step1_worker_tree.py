"""Step 1: disassemble audio worker tree FUN_180103a10, list all call/jump targets."""
import sys
sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin, IB
import capstone

b = Bin()
print("== sections ==")
for name, va, vsize, raw, rsize in b.secs[:6]:
    print(f"  {name:10s} va={va:#x} vsize={vsize:#x} raw={raw:#x} rsize={rsize:#x}")

START = 0x180103a10
# find end of function: disassemble generously then cut at a clean ret + padding
insns = b.disasm_until(START, max_instr=2000, stop_at_ret=False)

# find function end heuristics: stop at 'ret' followed by int3 padding or next known
end_va = START
calls = []
jmps_ext = []
for idx, i in enumerate(insns):
    if i.mnemonic in ("call", "jmp") and i.operands:
        op = i.operands[0]
        if op.type == capstone.x86.X86_OP_IMM:
            tgt = op.imm
            if i.mnemonic == "call":
                calls.append((i.address, tgt))
            else:
                jmps_ext.append((i.address, tgt))
        else:
            calls.append((i.address, f"IND:{i.op_str}"))
    if i.mnemonic in ("ret", "jmp") :
        pass

print(f"\n== FUN_180103a10: {len(insns)} instructions, last VA {insns[-1].address:#x} ==")
print("\n== direct CALL targets ==")
for a, t in calls:
    if isinstance(t, int):
        sec = b.sec_of(t)
        print(f"  {a:#x}: call {t:#x} ({sec})")
    else:
        print(f"  {a:#x}: call {t}")
print("\n== direct JMP targets (external) ==")
for a, t in jmps_ext:
    sec = b.sec_of(t) if isinstance(t, int) else "?"
    print(f"  {a:#x}: jmp {t if isinstance(t,str) else hex(t)} ({sec})")
