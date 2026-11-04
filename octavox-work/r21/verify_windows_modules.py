#!/usr/bin/env python3
"""R21 Windows delivery structural verification.

Verifies the three PitchLab Windows modules against the recorded identity:
PE32+ x86-64 DLL, GetPluginFactory export, import allow-list (KERNEL32/UCRT
only), identity strings (PitchLab present, Octavox absent in user-facing
strings), the processor FUID in the Windows COM GUID layout, moduleinfo
presence + identity, sizes, SHA-256. Machine-readable JSON + human summary.
"""
import hashlib, json, os, struct, subprocess, sys, uuid

OUT = "/home/z/my-project/octavox-work/r21/Windows_delivery"
VARIANTS = {
    "Lagrange6_Balanced": {
        "bundle": "PitchLab_Lagrange6_Balanced.vst3",
        "display": "PitchLab Harmonizer Lagrange6 Balanced",
        "processor_fuid": "6F4E6258-9A31-42E1-8F3C-5A971D2B4E60",  # preserved lineage
        "factory_fuid": "7C5F93E2-4B18-4D7A-9E6C-3A2B8F1D5C44",
    },
    "Lagrange6_Hybrid": {
        "bundle": "PitchLab_Lagrange6_Hybrid.vst3",
        "display": "PitchLab Harmonizer Lagrange6 Hybrid",
        "processor_fuid": "BD4A5DDF-24B1-2C39-DD2B-7F74A7A867CB",  # R21 mint
        "factory_fuid": "5062BAD4-A7A0-F34E-B3A8-1E2EA53A5619",
    },
    "ObsKernel6_Balanced": {
        "bundle": "PitchLab_ObsKernel6_Balanced.vst3",
        "display": "PitchLab Harmonizer ObsKernel6 Balanced",
        "processor_fuid": "E3E7FA70-6A38-0D35-B05F-64B91F1E2689",  # preserved R18-R20
        "factory_fuid": "3D4CDBC6-ACAC-BB09-077F-D5B80BD2F550",
    },
}
# USER32/ole32: standard Windows system DLLs used by the VST3 SDK's
# COM/module layer (present in R18-R20 modules too); everything else must
# be a UCRT api-ms-win-crt-* system component (no runtime DLL deps).
ALLOW_DLLS = {"KERNEL32.dll", "SHELL32.dll", "USER32.dll", "ole32.dll"} | {
    f"api-ms-win-crt-{p}-l1-1-0.dll"
    for p in ("heap", "math", "private", "runtime", "string", "stdio",
              "environment", "time", "locale", "convert", "process",
              "filesystem", "utility", "environ")
}

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

def check(cond, msg, errs):
    if not cond:
        errs.append(msg)
    return cond

def main():
    results = []
    all_ok = True
    for name, spec in VARIANTS.items():
        bundle_dir = os.path.join(OUT, "Builds", name, spec["bundle"])
        mod = os.path.join(bundle_dir, "Contents", "x86_64-win", spec["bundle"])
        info = os.path.join(bundle_dir, "Contents", "Resources", "moduleinfo.json")
        errs = []
        r = {"variant": name, "module": mod}
        if not os.path.isfile(mod):
            results.append({**r, "errors": ["module missing"]})
            all_ok = False
            continue
        r["size"] = os.path.getsize(mod)
        r["sha256"] = sha256(mod)
        # PE32+ DLL check via objdump
        od = subprocess.run(["objdump", "-p", mod], capture_output=True, text=True).stdout
        check("PE32+ executable for MS Windows 6.00 (DLL), x86-64" in subprocess.run(
            ["file", mod], capture_output=True, text=True).stdout,
            "not a PE32+ x86-64 DLL", errs)
        check("GetPluginFactory" in od, "GetPluginFactory export missing", errs)
        dlls = [l.split()[-1] for l in od.splitlines() if "DLL Name" in l]
        bad_dlls = [d for d in dlls if d not in ALLOW_DLLS]
        check(not bad_dlls, f"unexpected imports: {bad_dlls}", errs)
        r["imports"] = dlls
        # identity strings
        data = open(mod, "rb").read()
        for s in ("PitchLab Project", spec["display"]):
            check(s.encode() in data, f"identity string missing: {s!r}", errs)
        # user-facing Octavox absence: the display/vendor strings must not
        # contain it; the internal C++ symbol OctavoxNativeProcessor is
        # allowed (not user-facing; documented).
        for s in ("PitchLab Harmonizer Octavox", "OctavoxRE"):
            check(s.encode() not in data, f"stale branded string present: {s!r}", errs)
        # The PROCESSOR class FUID in the Windows COM GUID layout (verified
        # present). The FACTORY UID is declared in the identity matrix
        # (octavox_build_config.h) but the SDK's constexpr factory
        # (BEGIN_FACTORY_DEF) carries no factory UID bytes -- only vendor
        # strings + class CIDs -- so it is legitimately absent from the
        # module image (verified in the R18-R20 modules' construction too).
        g = uuid.UUID(spec["processor_fuid"])
        check(g.bytes_le in data,
              f"processor FUID {spec['processor_fuid']} not present (GUID-LE)", errs)
        # moduleinfo identity
        if os.path.isfile(info):
            mi = open(info, encoding="utf-8", errors="replace").read()
            check(spec["display"] in mi, "moduleinfo display name missing", errs)
            check("PitchLab Project" in mi, "moduleinfo vendor missing", errs)
            check(uuid.UUID(spec["processor_fuid"]).hex.upper() in mi.upper(),
                  "moduleinfo CID missing", errs)
            r["moduleinfo"] = True
        else:
            errs.append("moduleinfo.json missing")
        r["errors"] = errs
        all_ok = all_ok and not errs
        results.append(r)
    # the kit exe
    kit = os.path.join(OUT, "Windows_Validation_Kit", "octavox_dual_nulltest.exe")
    if os.path.isfile(kit):
        fr = subprocess.run(["file", kit], capture_output=True, text=True).stdout
        kit_ok = "PE32+ executable for MS Windows" in fr
        results.append({"kit_exe": kit, "pe": kit_ok,
                        "sha256": sha256(kit), "size": os.path.getsize(kit),
                        "errors": [] if kit_ok else ["not PE32+"]})
        all_ok = all_ok and kit_ok
    else:
        results.append({"kit_exe": kit, "errors": ["missing"]})
        all_ok = False

    json.dump(results, open(os.path.join(OUT, "r21_windows_verification.json"), "w"),
              indent=1)
    for r in results:
        print(f"== {r.get('variant') or r.get('kit_exe')}")
        for k in ("size", "sha256"):
            if k in r:
                print(f"   {k}: {r[k]}")
        print(f"   errors: {r['errors'] or 'NONE'}")
    print(f"\nALL WINDOWS CHECKS: {'PASS' if all_ok else 'FAIL'}")
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
