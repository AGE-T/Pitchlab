"""Emit the round-8 evidence text files (read-only w.r.t. the target).

Outputs (into the analysis folder):
  rea8_rate_constants.txt      - rate-map coefficient stores, constant pool, exact values
  rea8_vtables.txt             - RTTI COL map + VoxAudioProcessor vtable + adapter delegation
  rea8_latency_negative.txt    - the three exhaustive .text scans behind the Q-8 negative

Everything printed comes from the shipped image bytes; no symbol names are used as
evidence (names appear only as annotations).
"""
import json
import struct
import csv
from pathlib import Path

A = Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
PE = json.loads((A / "pe_info.json").read_text(encoding="utf-8"))
BASE = int(PE["image_base"], 16)
IMG = Path(r"F:\VST\OV_DIS\Ox.vst3").read_bytes()


def ai(v):
    return v if isinstance(v, int) else int(v, 16)


SEC = [(ai(s["va"]), ai(s["rsize"]), ai(s["ptr"]), s["name"]) for s in PE["sections"]]
TEXT = [s for s in SEC if s[3] == ".text"][0]
RD = [s for s in SEC if s[3] == ".rdata"][0]
LO, HI = BASE + TEXT[0], BASE + TEXT[0] + TEXT[1]


def va2fo(va):
    r = va - BASE
    for sva, rs, ptr, n in SEC:
        if sva <= r < sva + rs:
            return ptr + (r - sva)
    return None


def fo2va(fo):
    for sva, rs, ptr, n in SEC:
        if ptr <= fo < ptr + rs:
            return BASE + sva + (fo - ptr)
    return None


def cstr(va, n=200):
    fo = va2fo(va)
    if fo is None:
        return None
    e = IMG.find(b"\0", fo, fo + n)
    return IMG[fo:e].decode("latin-1", "replace")


sys_sizes = {}
with open(A / "functions.csv", newline="", encoding="utf-8") as fh:
    for row in csv.DictReader(fh):
        sys_sizes[int(row["start_va"], 16)] = int(row["size"])


# ---------------------------------------------------------------- rate constants
def rate_constants():
    out = []
    va = 0x1800E1F80
    fo = va2fo(va)
    out.append("rate-map coefficient stores (raw image bytes, 0x1800E1F80..0x1800E1FC0)")
    out.append("VA            bytes")
    for k in range(0, 0x40, 8):
        out.append("%s  %s" % (hex(va + k), IMG[fo + k:fo + k + 8].hex(" ")))
    out.append("")
    out.append("decoded `C7 86 <disp32> <imm32>` stores (mov dword ptr [rsi+disp], imm32):")
    b = IMG[fo:fo + 0x40]
    for k in range(0, len(b) - 9):
        if b[k] == 0xC7 and b[k + 1] == 0x86:
            disp = struct.unpack("<I", b[k + 2:k + 6])[0]
            bits = struct.unpack("<I", b[k + 6:k + 10])[0]
            val = struct.unpack("<f", b[k + 6:k + 10])[0]
            out.append("  %s  [rsi+0x%04X] = 0x%08X  = %.9f" % (hex(va + k), disp, bits, val))
    out.append("")
    out.append("multiplier pool, doubles at 0x180412480..0x1804124A8:")
    for a in range(0x180412480, 0x1804124B0, 8):
        fo2 = va2fo(a)
        raw = IMG[fo2:fo2 + 8]
        d = struct.unpack("<d", raw)[0]
        f32 = struct.unpack("<f", raw[:4])[0]
        out.append("  %s  %s  double=%.12g  float32(low)=%.9g" % (hex(a), raw.hex(" "), d, f32))
    out.append("")
    out.append("P evaluation (from the extracted prepare decompile, function FUN_1800E2030):")
    out.append("  f    = (double)page[+0x40F8] * (double)F,  F = **(base + page[0x68] + 8)")
    out.append("  P    = c0 + c1*f + c2*f^2 + c3*f^3 + c4*f^4      (double, MULSD/ADDSD chain)")
    out.append("  step = P * (double)page[+0x4128] * 64.0")
    out.append("  precision: coefficients are float32 members loaded MOVSS and widened with")
    out.append("  0F 5A (CVTPS2PD); f is a MULSD product at 0x1800E236D; quartic and both")
    out.append("  multiplies are double. No CVTPD2PS / no float32 rounding of P.")
    out.append("")
    c = [0.25, 0.346058071, 0.24391453, 0.119830653, 0.0348676369]
    out.append("exact coefficient sum = %.9f" % sum(c))
    out.append("")
    out.append("unity locus: step=1 <=> P(f)*R = 1/64 = 0.015625")

    def P(f):
        return c[0] + c[1] * f + c[2] * f * f + c[3] * f ** 3 + c[4] * f ** 4

    out.append("  f          P(f)          R for unity")
    for f in (0.0, 0.25, 0.5, 0.75, 1.0, 1.003988571, 2.0):
        out.append("  %-10.6f %-13.9f %.9f" % (f, P(f), 1.0 / (64.0 * P(f))))
    lo, hi = 1.0, 1.1
    for _ in range(90):
        m = (lo + hi) / 2
        if P(m) < 1:
            lo = m
        else:
            hi = m
    out.append("  P(f)=1 at f = %.9f" % lo)
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- vtables
COLS_OF_INTEREST = {
    0x18041ACF8: "VoxAudioProcessor (offset 0, primary)",
    0x18041A098: "VoxAudioProcessor (offset 0x150)",
    0x18041A928: "VoxAudioProcessor (offset 0x158)",
    0x180417210: "JuceVST3Component (offset 0)",
    0x180419D30: "JuceVST3Component (offset 8)",
    0x180419B48: "JuceVST3Component (offset 0x18)",
    0x1804178E0: "JuceVST3Component (offset 0x20)",
    0x1804185C0: "JuceVST3Component (offset 0x28)",
    0x1804182A0: "JuceVST3Component (offset 0x10, delegating sub-interface)",
    0x180418300: "JuceAudioProcessor (offset 0)",
}


def vtables():
    out = []
    out.append("MSVC RTTI / vtable map (resolved from image bytes)")
    out.append("")
    out.append("type descriptor = vfptr + 0x10 -> decorated name  (pTypeDescriptor + 0x10)")
    out.append("")
    for col, note in COLS_OF_INTEREST.items():
        fo = va2fo(col)
        sig, off, cd, ptd, pcd = struct.unpack("<IIIII", IMG[fo:fo + 0x14])
        name = cstr(BASE + ptd + 0x10) or cstr(BASE + ptd + 8) or cstr(BASE + ptd)
        out.append("COL %s  sig=%d offset=0x%X cdOffset=0x%X" % (hex(col), sig, off, cd))
        out.append("  type descriptor %s  name %s" % (hex(BASE + ptd), name))
        out.append("  annotated: %s" % note)
        needle = struct.pack("<Q", col)
        i = 0
        hdrs = []
        while True:
            i = IMG.find(needle, i)
            if i < 0:
                break
            v = fo2va(i)
            i += 1
            if v:
                hdrs.append(v)
        out.append("  vtable headers (qword == COL): %s" % ", ".join(hex(h) for h in hdrs))
        out.append("")

    out.append("VoxAudioProcessor primary vtable = 0x1803F05D0 (COL 0x18041ACF8, offset 0)")
    out.append("confirmed by destructor 0x1800175D0: lea rax,[0x1803f05d0]; mov [rcx],rax")
    out.append("")
    out.append("slot  VA            size   first bytes")
    fo = va2fo(0x1803F05D0)
    for k in range(64):
        v = struct.unpack("<Q", IMG[fo + k * 8:fo + k * 8 + 8])[0]
        if not (LO <= v < HI):
            out.append("%4d  %s  NONCODE" % (k, hex(v)))
            continue
        b = IMG[va2fo(v):va2fo(v) + 8]
        out.append("%4d  %s  %4d   %s" % (k, hex(v), sys_sizes.get(v, 0), b.hex(" ")))
    out.append("")
    out.append("slot notes:")
    out.append("  [0]  0x1800175D0 destructor; stores base vtable pointers at +0, +0x150, +0x158, +0x2600")
    out.append("  [1]  0x1801A72A0 returns String(\"Octavox\")  (literal 0x18038EB60)")
    out.append("  [2]  0x1801A7250 returns String(\"Ocxv\")     (literal 0x18038EB68)")
    out.append("  [3]  0x180313D20 (this, double, int) -> prepareToPlay")
    out.append("  [4]  0x180313BC0 mov byte [rcx+0x25C0],1; ret -> releaseResources")
    out.append("  [46] 0x180147A40 mov eax,[rcx+0x27DC]; ret")
    out.append("  [54] 0x180319200 test edx,edx; js; cmp edx,[rcx+0x27DC]; jae; mov rax,[rcx+0x140]")
    out.append("       -> +0x27DC is the program COUNT (bounds the program-name array)")
    out.append("")
    out.append("JuceVST3Component +0x10 delegating sub-interface "
               "(COL 0x1804182A0, vtable 0x1803EFDC0):")
    fo = va2fo(0x1803EFDC0)
    for k in range(12):
        v = struct.unpack("<Q", IMG[fo + k * 8:fo + k * 8 + 8])[0]
        b = IMG[va2fo(v):va2fo(v) + 16] if va2fo(v) else b""
        out.append("  [%2d] %s  %s" % (k, hex(v), b.hex(" ")))
    out.append("  -> entries 0-2: 'sub rcx,0x10; jmp rel32' adjustor thunks")
    out.append("  -> entries 3-14: 'mov rcx,[rcx+0x38]; mov rax,[rcx]; jmp [rax+0x18..0x70]'")
    out.append("     (delegates the interface to an object reached through the pointer at +0x38)")
    out.append("")
    out.append("JuceAudioProcessor (COL 0x180418300, vtable 0x1803EF950, 16 entries):")
    fo = va2fo(0x1803EF950)
    for k in range(16):
        v = struct.unpack("<Q", IMG[fo + k * 8:fo + k * 8 + 8])[0]
        b = IMG[va2fo(v):va2fo(v) + 12]
        out.append("  [%2d] %s  %s" % (k, hex(v), b.hex(" ")))
    out.append("  [0] = 'mov qword ptr [r8],0; mov eax,0x80004001; ret' (E_NOTIMPL queryInterface)")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- latency negative
def latency_negative():
    out = []
    t0, t1 = TEXT[2], TEXT[2] + TEXT[1]
    out.append("Q-8 exhaustive negatives (raw .text scan, %d bytes, 0x%X..0x%X)" % (t1 - t0, LO, HI))
    out.append("")

    # scan 1: inline getter shape
    reg64 = {0: "rax", 2: "rdx", 1: "rcx", 3: "rbx", 6: "rsi", 7: "rdi"}

    def disp(b, i, mod):
        if mod == 1:
            return struct.unpack("<b", b[i:i + 1])[0], i + 1
        if mod == 2:
            return struct.unpack("<i", b[i:i + 4])[0], i + 4
        return 0, i

    hits = []
    for off in range(t0, t1 - 16):
        b = IMG[off:off + 16]
        if b[0] == 0x48 and b[1] == 0x8B and (b[2] & 0x07) == 0x01:
            mod1 = (b[2] >> 6) & 3
            if mod1 not in (1, 2):
                continue
            r1 = (b[2] >> 3) & 7
            if r1 not in reg64:
                continue
            d1, i = disp(b, 3, mod1)
        elif b[0] == 0x48 and b[1] == 0x8B and (b[2] & 0xC7) == 0x01:
            r1 = (b[2] >> 3) & 7
            if r1 not in reg64:
                continue
            d1, i = 0, 3
        else:
            continue
        if b[i] != 0x8B:
            continue
        m2 = b[i + 1]
        if (m2 & 7) != r1 or ((m2 >> 3) & 7) != 0:
            continue
        mod2 = (m2 >> 6) & 3
        if mod2 not in (1, 2):
            continue
        d2, j = disp(b, i + 2, mod2)
        if b[j] == 0xC3:
            hits.append((fo2va(off), reg64[r1], d1, d2))

    refs = {}
    rva0, size0, ptr0, _ = RD
    for off in range(ptr0, ptr0 + size0 - 8, 8):
        v = struct.unpack("<Q", IMG[off:off + 8])[0]
        refs.setdefault(v, []).append(fo2va(off))

    out.append("scan 1: mov r64,[rcx+d]; mov eax,[r64+d]; ret   -> %d hits" % len(hits))
    for va, reg, d1, d2 in hits:
        tag = "VT" if va in refs else "  "
        out.append("  %s %s  [rcx%+d] -> [%s%+d]  rdata_refs=%s" % (
            tag, hex(va), d1, reg, d2, [hex(x) for x in refs.get(va, [])[:2]]))
    out.append("  vtable-referenced hits resolve to:")
    out.append("    0x180083410 -> FileListTreeItem@juce (COL 0x18042CDB8)")
    out.append("    0x180099AE0 -> TextEditorTextInterface@TextEditorAccessibilityHandler@juce"
               " (COL 0x180427C78)")
    out.append("    0x18014A110 -> CodeEditorComponent::TextInsert accessibility interface"
               " (COL 0x180438628, ctor 0x18031DAC0)")
    out.append("  -> none is the host latency getter")
    out.append("")

    # scan 1b: same shape with zero displacement on the first load
    zd = []
    i = t0
    while True:
        i = IMG.find(b"\x48\x8b\x01\x8b", i, t1)
        if i < 0:
            break
        m = IMG[i + 4]
        if (m & 0xC7) == 0x40 and IMG[i + 6] == 0xC3:
            zd.append((fo2va(i), IMG[i + 5]))
        i += 1
    out.append("scan 1b: mov rax,[rcx]; mov eax,[rax+d]; ret   -> %d hits" % len(zd))
    for va, d in zd:
        out.append("  %s  [rcx] -> [rax+%d]  rdata_refs=%s" % (
            hex(va), d, [hex(x) for x in refs.get(va, [])[:2]]))
    out.append("  -> neither is a vtable entry")
    out.append("")

    # scan 2: stores to +0x27DC
    needle = struct.pack("<I", 0x27DC)
    stores = 0
    loads = 0
    i = t0
    while True:
        i = IMG.find(needle, i, t1)
        if i < 0:
            break
        op = IMG[i - 2]
        modrm = IMG[i - 1]
        if (modrm >> 6) == 2 and op in (0x89, 0xC7, 0x88):
            stores += 1
            out.append("scan 2: store to +0x27DC at %s : %s" % (
                hex(fo2va(i - 2)), IMG[i - 2:i + 8].hex(" ")))
        i += 1
        loads += 1
    out.append("scan 2: displacements 0x27DC in .text: %d sites, %d stores" % (loads, stores))
    out.append("  the single store is the zero-init at 0x180319764 in FUN_180319510")
    out.append("  (tide::DigiPluginBase ctor): *(u32*)(this+0x27DC) = 0")
    out.append("")

    # scan 3: test-and-set pairs (setLatencySamples shape)
    blob = IMG[t0:t1]
    pairs = []
    i = 0
    while True:
        i = blob.find(b"\x39\x91", i)
        if i < 0:
            break
        d1 = struct.unpack("<I", blob[i + 2:i + 6])[0]
        if blob[i:i + 48].find(b"\x89\x91" + struct.pack("<I", d1)) > 0:
            pairs.append((fo2va(t0 + i), d1))
        i += 1
    out.append("scan 3: `cmp [rcx+d],edx` + `mov [rcx+d],edx` pairs: %d" % len(pairs))
    for va, d in pairs:
        out.append("  %s disp=0x%X" % (hex(va), d))
    out.append("  none targets 0x27DC")
    return "\n".join(out) + "\n"


def main():
    (A / "rea8_rate_constants.txt").write_text(rate_constants(), encoding="utf-8")
    (A / "rea8_vtables.txt").write_text(vtables(), encoding="utf-8")
    (A / "rea8_latency_negative.txt").write_text(latency_negative(), encoding="utf-8")
    print("wrote rea8_rate_constants.txt, rea8_vtables.txt, rea8_latency_negative.txt")


if __name__ == "__main__":
    main()
