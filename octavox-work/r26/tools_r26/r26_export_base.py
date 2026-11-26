#!/usr/bin/env python3
"""R26 Step B: base exports from the analyzed Ghidra project (ox26 / Ox.vst3).

Writes to /home/z/r26-exports/ (OUTSIDE the repository - raw bulk evidence):
  - meta.txt          program metadata (base, format, language, function count)
  - segments.tsv      memory blocks
  - functions.tsv     entry, name, size(bytes), thunk flag
  - symbols.tsv       all defined symbols (incl. demangled/imports/labels)
  - strings.tsv       defined strings with referencing addresses
  - xrefs.tsv         every code reference: from, to, type
  - callgraph.tsv     caller->callee edges (CALL/THUNK into function entries)

Purely static (read-only on the analyzed project database).
"""
import os
import time

os.environ.setdefault("GHIDRA_INSTALL_DIR", "/home/z/tools/ghidra_12.1.4_PUBLIC")
os.environ.setdefault("JAVA_HOME", "/home/z/tools/jdk-21.0.13.1+2")

import pyghidra  # noqa: E402

BIN = "/home/z/my-project/upload/Ox.vst3"
PROJ_LOC = "/home/z/r26-ghidra"
PROJ_NAME = "ox26"
OUT = "/home/z/r26-exports"


def start_bounded() -> None:
    """Start the JVM with a bounded heap (4 GB host shared with the dev server)."""
    from pyghidra.launcher import HeadlessPyGhidraLauncher

    launcher = HeadlessPyGhidraLauncher()
    launcher.add_vmargs("-Xmx900m")
    launcher.add_vmargs("-XX:+UseSerialGC")
    launcher.add_vmargs("-Dcpu.core.limit=2")
    launcher.start()


def main() -> None:
    t0 = time.time()
    os.makedirs(OUT, exist_ok=True)
    start_bounded()
    with pyghidra.open_program(
        BIN, project_location=PROJ_LOC, project_name=PROJ_NAME, analyze=False
    ) as api:
        prog = api.getCurrentProgram()
        fm = prog.getFunctionManager()
        listing = prog.getListing()
        rm = prog.getReferenceManager()
        st = prog.getSymbolTable()

        # ---- segments.tsv ----
        with open(os.path.join(OUT, "segments.tsv"), "w") as fh:
            fh.write("name\tstart\tend\tsize\ttype\tinitialized\n")
            for b in prog.getMemory().getBlocks():
                fh.write(
                    "%s\t%s\t%s\t%d\t%s\t%s\n"
                    % (
                        b.getName(),
                        b.getStart(),
                        b.getEnd(),
                        b.getSize(),
                        b.getType(),
                        b.isInitialized(),
                    )
                )
        print("[base] segments.tsv done", flush=True)

        # ---- functions.tsv ----
        entries = set()
        with open(os.path.join(OUT, "functions.tsv"), "w") as fh:
            fh.write("entry\tname\tsize\tthunk\n")
            for f in fm.getFunctions(True):
                e = f.getEntryPoint().toString()
                entries.add(e)
                fh.write(
                    "%s\t%s\t%d\t%s\n"
                    % (e, f.getName(), f.getBody().getNumAddresses(), f.isThunk())
                )
        print("[base] functions.tsv done:", len(entries), flush=True)

        # ---- symbols.tsv ----
        with open(os.path.join(OUT, "symbols.tsv"), "w") as fh:
            fh.write("address\tname\ttype\n")
            for s in st.getAllSymbols(True):
                fh.write(
                    "%s\t%s\t%s\n" % (s.getAddress(), s.getName(), s.getSymbolType())
                )
        print("[base] symbols.tsv done", flush=True)

        # ---- strings.tsv ----
        nstr = 0
        with open(os.path.join(OUT, "strings.tsv"), "w") as fh:
            fh.write("address\tlength\trefs_from\ttext\n")
            it = listing.getDefinedData(True)
            while it.hasNext():
                d = it.next()
                v = d.getValue()
                if isinstance(v, str) and len(v) >= 4:
                    refs = []
                    for r in rm.getReferencesTo(d.getAddress()):
                        refs.append(r.getFromAddress().toString())
                    txt = (
                        v.replace("\\", "\\\\")
                        .replace("\t", " ")
                        .replace("\n", "\\n")
                        .replace("\r", "\\r")
                    )
                    fh.write(
                        "%s\t%d\t%s\t%s\n"
                        % (d.getAddress(), d.getLength(), ",".join(refs), txt)
                    )
                    nstr += 1
        print("[base] strings.tsv done:", nstr, flush=True)

        # ---- xrefs.tsv + callgraph.tsv (via instruction references) ----
        nx = 0
        ncg = 0
        with open(os.path.join(OUT, "xrefs.tsv"), "w") as fx, open(
            os.path.join(OUT, "callgraph.tsv"), "w"
        ) as fc:
            fx.write("from\tto\ttype\n")
            fc.write("caller\tcallee\n")
            it = listing.getInstructions(True)
            while it.hasNext():
                ins = it.next()
                refs = ins.getReferencesFrom()
                for r in refs:
                    t = r.getReferenceType().toString()
                    fx.write("%s\t%s\t%s\n" % (r.getFromAddress(), r.getToAddress(), t))
                    nx += 1
                    if (
                        t in ("CALL", "THUNK")
                        and r.getToAddress().toString() in entries
                    ):
                        fc.write("%s\t%s\n" % (r.getFromAddress(), r.getToAddress()))
                        ncg += 1
        print("[base] xrefs done:", nx, "callgraph edges:", ncg, flush=True)

        # ---- meta.txt ----
        with open(os.path.join(OUT, "meta.txt"), "w") as fh:
            fh.write("program: %s\n" % prog.getName())
            fh.write("image_base: %s\n" % prog.getImageBase())
            fh.write("format: %s\n" % prog.getExecutableFormat())
            fh.write("language: %s\n" % prog.getLanguageID())
            fh.write(
                "compiler_spec: %s\n" % prog.getCompilerSpec().getCompilerSpecID()
            )
            fh.write("functions: %d\n" % fm.getFunctionCount())
            fh.write("created: %s\n" % time.ctime())
    print("[r26_export_base] DONE in %.1fs" % (time.time() - t0), flush=True)


if __name__ == "__main__":
    main()
