#!/usr/bin/env python3
"""R26 Step C: decompile EVERY function with the Ghidra decompiler.

RESUMABLE + DEADLINE-BUDGETED design (sandbox limits: no background survival,
10-minute foreground budget per run):
  - completed entries are appended to /home/z/r26-exports/decomp_done.tsv
    (entry\tname\tstatus\tfile) and skipped on later runs;
  - a run stops at DEADLINE_S elapsed (prints PARTIAL n/m or COMPLETE).

Purely static analysis (read-only program DB).
"""
import os
import re
import time

os.environ.setdefault("GHIDRA_INSTALL_DIR", "/home/z/tools/ghidra_12.1.4_PUBLIC")
os.environ.setdefault("JAVA_HOME", "/home/z/tools/jdk-21.0.13.1+2")

import pyghidra  # noqa: E402

BIN = "/home/z/my-project/upload/Ox.vst3"
PROJ_LOC = "/home/z/r26-ghidra"
PROJ_NAME = "ox26"
OUT = "/home/z/r26-exports"
DEC = os.path.join(OUT, "decomp")
DONE = os.path.join(OUT, "decomp_done.tsv")
DEADLINE_S = 540  # stay under the 10-minute tool-call budget


def safe(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.]", "_", name)[:120]


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
    os.makedirs(DEC, exist_ok=True)

    done = {}
    if os.path.exists(DONE):
        with open(DONE) as fh:
            for line in fh:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 4:
                    done[parts[0]] = parts[2]  # entry -> status

    start_bounded()
    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    with pyghidra.open_program(
        BIN, project_location=PROJ_LOC, project_name=PROJ_NAME, analyze=False
    ) as api:
        prog = api.getCurrentProgram()
        fm = prog.getFunctionManager()
        di = DecompInterface()
        di.openProgram(prog)
        mon = ConsoleTaskMonitor()
        total = fm.getFunctionCount()
        i = 0
        ok = 0
        fail = 0
        skipped = 0
        stop = False
        out = open(DONE, "a")
        for f in fm.getFunctions(True):
            i += 1
            e = f.getEntryPoint().toString()
            if e in done:
                skipped += 1
                continue
            if (time.time() - t0) > DEADLINE_S:
                stop = True
                break
            name = f.getName()
            fname = "%s_%s.c" % (safe(name), e)
            status = ""
            try:
                res = di.decompileFunction(f, 120, mon)
                if res.decompileCompleted():
                    c = res.getDecompiledFunction().getC()
                    with open(os.path.join(DEC, fname), "w") as cf:
                        cf.write("// %s @ %s\n" % (name, e))
                        cf.write(c)
                    ok += 1
                    status = "OK"
                else:
                    status = "DECOMP_FAIL: %s" % res.getErrorMessage()
                    fail += 1
            except Exception as ex:  # noqa: BLE001
                status = "EXC: %s" % ex
                fail += 1
            out.write("%s\t%s\t%s\t%s\n" % (e, name, status, fname))
            out.flush()
            if i % 25 == 0:
                print(
                    "[decomp] %d/%d (ok=%d fail=%d skip=%d) %.0fs"
                    % (i, total, ok, fail, skipped, time.time() - t0),
                    flush=True,
                )
        out.close()
        di.dispose()
        remain = total - skipped - ok - fail
        state = "COMPLETE" if (not stop and remain <= 0) else "PARTIAL"
        print(
            "[r26_export_decomp] %s: %d/%d total, this run ok=%d fail=%d "
            "skipped=%d remain=%d elapsed=%.0fs"
            % (state, total, total, ok, fail, skipped, remain, time.time() - t0),
            flush=True,
        )


if __name__ == "__main__":
    main()
