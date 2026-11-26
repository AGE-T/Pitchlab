#!/usr/bin/env python3
"""Quick probe: why did the instruction iterator produce zero references?"""
import os

os.environ.setdefault("GHIDRA_INSTALL_DIR", "/home/z/tools/ghidra_12.1.4_PUBLIC")
os.environ.setdefault("JAVA_HOME", "/home/z/tools/jdk-21.0.13.1+2")

import pyghidra  # noqa: E402

BIN = "/home/z/my-project/upload/Ox.vst3"
PROJ_LOC = "/home/z/r26-ghidra"
PROJ_NAME = "ox26"


def start_bounded() -> None:
    from pyghidra.launcher import HeadlessPyGhidraLauncher

    launcher = HeadlessPyGhidraLauncher()
    launcher.add_vmargs("-Xmx900m")
    launcher.add_vmargs("-XX:+UseSerialGC")
    launcher.start()


def main() -> None:
    start_bounded()
    with pyghidra.open_program(
        BIN, project_location=PROJ_LOC, project_name=PROJ_NAME, analyze=False
    ) as api:
        prog = api.getCurrentProgram()
        listing = prog.getListing()
        try:
            print("numInstructions:", listing.getNumInstructions())
        except Exception as ex:  # noqa: BLE001
            print("numInstructions ERR:", ex)
        it = listing.getInstructions(True)
        n = 0
        first = None
        while it.hasNext() and n < 4:
            cu = it.next()
            if first is None:
                first = cu
            n += 1
        print("iterated:", n, "type(first):", type(first))
        if first is not None:
            print("dir has getReferencesFrom:", "getReferencesFrom" in dir(first))
            try:
                refs = first.getReferencesFrom()
                print("refs from first:", len(refs))
            except Exception as ex:  # noqa: BLE001
                print("getReferencesFrom ERR:", repr(ex))
            try:
                print("mnemonic:", first.getMnemonicString())
            except Exception as ex:  # noqa: BLE001
                print("mnemonic ERR:", repr(ex))
        # reference manager total
        rm = prog.getReferenceManager()
        try:
            print("refcount:", rm.getReferenceCount())
        except Exception as ex:  # noqa: BLE001
            print("refcount ERR:", ex)


if __name__ == "__main__":
    main()
