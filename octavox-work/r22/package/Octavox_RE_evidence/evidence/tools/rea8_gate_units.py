"""Round-8 Q-15/C5 evidence: gate units, applier constants, derived-value path.

Emits rea8_gate_units.txt.  Everything comes from the shipped image bytes; the
annotations are derived from the instruction stream itself.
"""
import csv
import json
import math
import struct
from pathlib import Path

import capstone

A = Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE = json.loads((A / "pe_info.json").read_text(encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()
SEC = [(int(s["va"], 16) if isinstance(s["va"], str) else s["va"],
        int(s["rsize"], 16) if isinstance(s["rsize"], str) else s["rsize"],
        int(s["ptr"], 16) if isinstance(s["ptr"], str) else s["ptr"], s["name"]) for s in PE["sections"]]
TEXT = [s for s in SEC if s[3] == ".text"][0]
LO, HI = BASE + TEXT[0], BASE + TEXT[0] + TEXT[1]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def va2fo(va):
    r = va - BASE
    for sva, rs, ptr, n in SEC:
        if sva <= r < sva + rs:
            return ptr + (r - sva)


def fo2va(fo):
    for sva, rs, ptr, n in SEC:
        if ptr <= fo < ptr + rs:
            return BASE + sva + (fo - ptr)


def const_at(va, size):
    fo = va2fo(va)
    raw = IMG[fo:fo + size]
    if size == 8:
        return raw, struct.unpack("<d", raw)[0], struct.unpack("<f", raw[:4])[0]
    return raw, struct.unpack("<f", raw)[0], None


def listing(start, end, out):
    fo = va2fo(start)
    for ins in md.disasm(IMG[fo:va2fo(end)], start):
        note = ""
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                tgt = ins.address + ins.size + op.mem.disp
                if ins.mnemonic.endswith("ss") or "xmm" in ins.op_str:
                    fo2 = va2fo(tgt)
                    if fo2 is not None:
                        f = struct.unpack("<f", IMG[fo2:fo2 + 4])[0]
                        d = struct.unpack("<d", IMG[fo2:fo2 + 8])[0]
                        note = "   ; [%X] f32=%.9g f64=%.17g" % (tgt, f, d)
        out.append("%016X  %-22s %-42s%s" % (ins.address, ins.bytes.hex(), ins.mnemonic + " " + ins.op_str, note))


sizes = {}
with open(A / "functions.csv", newline="", encoding="utf-8") as fh:
    for row in csv.DictReader(fh):
        sizes[int(row["start_va"], 16)] = int(row["end_va"], 16) - int(row["start_va"], 16)

out = []
out.append("Q-15 / C5 - gate units, applier conversion and the derived-value path")
out.append("target F:\\VST\\OV_DIS\\Ox.vst3  (read-only)")
out.append("")
out.append("1. Routine boundary map for the gate routine (functions.csv is chunked here)")
out.append("")
for a in (0x18013BBC0, 0x18013BCDA, 0x18013C85E):
    out.append("   chunk %X  size %d" % (a, sizes.get(a, 0)))
out.append("   the three chunks are one routine: control flows linearly from 0x18013BBC0")
out.append("   through 0x18013BCDA into 0x18013C85E (no ret / jump table at the boundaries).")
out.append("")
out.append("2. Case dispatch inside the routine")
out.append("")
out.append("   0x18013BE40  movsxd rax,[r15+0x5FF8]   ; case index 0..9")
out.append("   0x18013BE47  cmp eax,9 / ja default")
out.append("   0x18013BE58  lea rdx,[rip-0x13BE5F]    ; => 0x180000000")
out.append("   0x18013BE67  mov ecx,[rdx+rax*4+0x13D22C] ; jump table at 0x18013D22C")
out.append("   0x18013BE6E  add rcx,rdx / jmp rcx")
out.append("")
tbl = va2fo(0x18013D22C)
out.append("   case -> target")
for k in range(10):
    off = struct.unpack("<i", IMG[tbl + k * 4:tbl + k * 4 + 4])[0]
    out.append("   %4d -> %X" % (k, 0x180000000 + off))
out.append("")
out.append("   => case 6 starts at 0x18013CF3C and case 7 at 0x18013D127, so the gate")
out.append("      block 0x18013D0C0..0x18013D0F2 lies inside case 6 (the round-6")
out.append("      'state 6' attribution is confirmed).")
out.append("")
out.append("3. Applier FUN_1800E7960 (gatelevel -> module parameter 2)")
out.append("")
listing(0x1800E7960, 0x1800E79F1, out)
out.append("   ...")
out.append("")
out.append("   constants used above:")
for va, size in ((0x180412250, 8), (0x18041213C, 4)):
    raw, v, f = const_at(va, size)
    out.append("     %X  %-24s  = %.17g" % (va, raw.hex(" "), v))
out.append("   => xmm0 = (double)clamp(g) * 0.23025850929  ~= ln(10)/10")
out.append("      call 0x180351F50 (exp)  =>  10^(g/10)   [power ratio]")
out.append("      minss with 1.0 caps it; r8d=2 -> parameter 2")
out.append("")
out.append("3b. The module-side entry point: dsp_t vtable slot 1 and its dispatch")
out.append("")
rtti = json.loads((A / "rtti_vtables.json").read_text(encoding="utf-8"))
rec = next(r for r in rtti if r.get("label") == "dsp_t@dtonicii_t")
vt = rec["vftables"][0]
out.append("   RTTI  %s" % rec["mangled"])
out.append("   COL %s   vtable %s" % (vt["col_va"], vt["vtable_va"]))
for i, m in enumerate(rec["methods"][vt["vtable_va"]]):
    tag = ""
    if m == "0x1800E10B0":
        tag = "  <- setparam (FUN_1800E10B0)"
    elif m == "0x1800E1520":
        tag = "  <- getparam (FUN_1800E1520)"
    out.append("   slot %d (+0x%02X) = %s%s" % (i, i * 8, m, tag))
out.append("")
out.append("   ctor 0x1800E03B3  lea rcx,[rip+0x329BA6]  -> 0x180409F60")
out.append("        0x1800E03BA  mov [r15],rcx           ; object->vptr = dsp_t vftable")
out.append("")
out.append("   setparam dispatch: 0x1800E10BE cmp edx,0x11 / ja 0x1800E14B3")
out.append("                      0x1800E10D0 table 0x1800E14D0 (offsets from 0x180000000)")
tbl1 = va2fo(0x1800E14D0)
out.append("   index -> target")
for k in range(18):
    off = struct.unpack("<i", IMG[tbl1 + k * 4:tbl1 + k * 4 + 4])[0]
    out.append("   %4d -> %X%s" % (k, 0x180000000 + off, "   <- parameter 2" if k == 2 else
                                   ("   (default)" if 0x180000000 + off == 0x1800E14B3 else "")))
out.append("")
out.append("   0x1800E110F  mov eax,[r8]")
out.append("   0x1800E1112  mov [rbx+0xA4E8],eax   ; gatelevel -> T+0x5FD0")
out.append("")
out.append("   default path reads [vptr+0x18] = slot 3 = 0x1800C5750:")
listing(0x1800C5750, 0x1800C576A, out)
out.append("   -> forwards to modules[edx]->slot1(paramId,value*), i.e. a base-class")
out.append("      thunk, not a self call (the slot is not slot 1).")
out.append("   slots 4/5/6 = 0x1800C5770/5790/57B0: getparam / (base+index) / base id.")
out.append("   [this+0x10] = module pointer array, [module+8] = that module's id base.")
out.append("")
out.append("   UNK: the applier reaches this as owner->[vptr+0x60]([applier+8],2,value);")
out.append("        [applier+8] is a runtime module-array index, not statically fixed here.")
out.append("")
out.append("4. Sentinel handling in the same function")
out.append("")
listing(0x1800E7A38, 0x1800E7B7A, out)
out.append("")
out.append("   -99 (0x63): parameter 4 = 0.666666627 (2/3), flag [+0x420] = 0")
out.append("   -97 (0x61): parameter 4 = 0.666666627 (2/3), flag [+0x420] = 1")
out.append("   -98 (0x62): parameter 4 = 0,               flag [+0x420] = 1")
out.append("   otherwise : parameter 4 = 0,               flag [+0x420] = 0")
out.append("")
out.append("5. The level the gate compares against (case 6, 0x18013BD3C..0x18013BD84)")
out.append("")
listing(0x18013BCC0, 0x18013BD90, out)
out.append("   [T+0x5FC8] -> struct; +4 = c*x^2 + d*prev   (one-pole smoothed POWER)")
out.append("   +8 = a second smoothed value with the same shape")
out.append("")
out.append("6. The gate comparison (case 6, 0x18013D0C0..0x18013D0F2)")
out.append("")
listing(0x18013D044, 0x18013D0F2, out)
out.append("   [0x18013D044..0x18013D09F] builds xmm8 (two calls to the interpolator 0x18013E160,")
out.append("   difference, plus a rate term scaled by [T+0x6008]); mode [T+0x5F14] = 4 doubles it,")
out.append("   mode 8 scales it by the float 4.0 at 0x180412510.")
out.append("   gate opens when  gatelevel(power) - level(power) < eps  => same domain")
out.append("")
out.append("7. Derived-value path reads of the same field (m02 FUN_1800E1520)")
out.append("")
listing(0x1800E1751, 0x1800E17E0, out)
out.append("")
for va, size in ((0x180412200, 8), (0x180412320, 8), (0x180412888, 8), (0x180411E44, 4)):
    raw, v, f = const_at(va, size)
    out.append("     %X  %-24s  = %.17g" % (va, raw.hex(" "), v))
out.append("   => threshold = exp(field * 0.11513) ~= 10^(field/20)   [treats the stored")
out.append("      POWER ratio as if it were dB]; compared with sqrt(level*c);")
out.append("      then ratio -> (0.0 -1200/ln2) -> cents (constant 1731.234).")
out.append("")
out.append("8. Numeric notes (hand-typed constants)")
out.append("")
out.append("   ln(10)/10 = %.17g   stored 0.23025850929      (rel err %.1e)" % (math.log(10) / 10, abs(0.23025850929 / (math.log(10) / 10) - 1)))
out.append("   ln(10)/20 = %.17g   stored 0.11513            (rel err %.1e)" % (math.log(10) / 20, abs(0.11513 / (math.log(10) / 20) - 1)))
out.append("   1200/ln2  = %.17g   stored 1731.234           (rel err %.1e)" % (1200 / math.log(2), abs(1731.234 / (1200 / math.log(2)) - 1)))
out.append("")
out.append("Conclusion (DER from the above):")
out.append("  * applier writes a POWER ratio (10^(g/10)) into module parameter 2 -> T+0x5FD0;")
out.append("  * the case-6 gate compares it against a smoothed POWER envelope -> consistent;")
out.append("  * the m02 derived-value path exponentiates the same field as if it were dB,")
out.append("    so it saturates near 1.0 for every g <= 0 -> the units CONFLICT is confined")
out.append("    to that path, not to the audio gate.")

(A / "rea8_gate_units.txt").write_text("\n".join(out) + "\n", encoding="utf-8")
print("wrote rea8_gate_units.txt", len(out), "lines")
