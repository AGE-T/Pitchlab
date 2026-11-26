#!/usr/bin/env python3
"""R26 Step A: Ghidra import + auto-analysis of the original Octavox PE32+ DLL.

STRICTLY STATIC: the binary is never executed, never modified (read-only import).
Project: /home/z/r26-ghidra (ox26), program name: Ox.vst3.
Reproducible tool of the R26 static audit (octavox-work/r26/tools_r26/).
"""
import os
import time

os.environ.setdefault("GHIDRA_INSTALL_DIR", "/home/z/tools/ghidra_12.1.4_PUBLIC")
os.environ.setdefault("JAVA_HOME", "/home/z/tools/jdk-21.0.13.1+2")

import pyghidra  # noqa: E402

BIN = "/home/z/my-project/upload/Ox.vst3"
PROJ_LOC = "/home/z/r26-ghidra"
PROJ_NAME = "ox26"


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
    print("[r26_analyze] importing (static, read-only):", BIN, flush=True)
    start_bounded()
    with pyghidra.open_program(
        BIN, project_location=PROJ_LOC, project_name=PROJ_NAME, analyze=True
    ) as api:
        prog = api.getCurrentProgram()
        fm = prog.getFunctionManager()
        print("[r26_analyze] image base:", prog.getImageBase(), flush=True)
        print("[r26_analyze] format:", prog.getExecutableFormat(), flush=True)
        print("[r26_analyze] functions:", fm.getFunctionCount(), flush=True)
        print("[r26_analyze] memory blocks:", flush=True)
        for b in prog.getMemory().getBlocks():
            print(
                "    %-10s %s .. %s  size=%d  init=%s  exec=%s"
                % (
                    b.getName(),
                    b.getStart(),
                    b.getEnd(),
                    b.getSize(),
                    b.isInitialized(),
                    b.isExecutable(),
                ),
                flush=True,
            )
    print("[r26_analyze] DONE in %.1fs" % (time.time() - t0), flush=True)


if __name__ == "__main__":
    main()
