#!/usr/bin/env python3
"""R26 Step B2: per-function capstone disassembly for every Ghidra-discovered
function of upload/Ox.vst3.

Pure static read of the PE file (no Ghidra needed at runtime). Consumes
/home/z/r26-exports/functions.tsv (from r26_export_base.py) and writes:
  - /home/z/r26-exports/disasm/<SAFE_NAME>_<entry>.asm   (one file per function)
  - /home/z/r26-exports/disasm_index.tsv                 (index)

Each .asm line: "VA: bytes mnemonic operands ; annotations"
Annotations: rip-relative targets, immediate VAs, CALL targets resolved to
function names. Reuses the proven r25lib toolkit from the repository.
"""
import os
import re
import sys

sys.path.insert(0, "/home/z/my-project/octavox-work/analysis_r25")
from r25lib import Bin  # noqa: E402

BIN_PATH = "/home/z/my-project/upload/Ox.vst3"
OUT = "/home/z/r26-exports"
DIS = os.path.join(OUT, "disasm")


def safe(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.]", "_", name)[:120]


def main() -> None:
    os.makedirs(DIS, exist_ok=True)
    b = Bin(BIN_PATH)

    funcs = []
    with open(os.path.join(OUT, "functions.tsv")) as fh:
        next(fh)
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 4:
                e, n, s, t = parts
                funcs.append((e, n, int(s), t))
    entry2name = {e: n for e, n, s, t in funcs}
    print("[disasm] functions to disassemble:", len(funcs), flush=True)

    nwritten = 0
    with open(os.path.join(OUT, "disasm_index.tsv"), "w") as idx:
        idx.write("entry\tname\tfile\tlines\tbytes_covered\n")
        for e, n, sz, thunk in funcs:
            va = int(e, 16)
            insns = b.disasm(va, sz)
            if not insns:
                continue
            # annotate CALL/JMP targets that are known function entries
            lines = b.fmt(insns)
            annotated = []
            for raw in lines.split("\n"):
                m = re.match(r"^([0-9a-f]+): (\S+) (\S+) (.*)", raw)
                if not m:
                    annotated.append(raw)
                    continue
                addr_s, bytes_s, mnem, ops = m.groups()
                extra = ""
                # direct call/jmp absolute target
                for tok in re.findall(r"0x([0-9a-f]{9,16})", ops):
                    tva = int(tok, 16)
                    if ("%x" % tva) in entry2name:
                        extra += f"  ; -> {entry2name['%x' % tva]}"
                annotated.append(raw + extra)
            fname = f"{safe(n)}_{e}.asm"
            with open(os.path.join(DIS, fname), "w") as af:
                af.write("\n".join(annotated))
                af.write("\n")
            idx.write(
                "%s\t%s\t%s\t%d\t%d\n"
                % (e, n, fname, len(annotated), sum(i.size for i in insns))
            )
            nwritten += 1
            if nwritten % 200 == 0:
                print("[disasm] written:", nwritten, flush=True)
    print("[r26_export_disasm] DONE:", nwritten, "files", flush=True)


if __name__ == "__main__":
    main()
