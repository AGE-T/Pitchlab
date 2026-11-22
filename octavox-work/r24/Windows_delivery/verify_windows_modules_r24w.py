#!/usr/bin/env python3
"""R24-Windows delivery structural verification (2 variants).

Verifies the two PitchLab Windows modules against the recorded identity
(the R21/R22 method, R24-W edition):
  * PE32+ x86-64 DLL (machine 0x8664, magic 0x20b),
  * GetPluginFactory export (byte + export-directory),
  * import allow-list (Windows system DLLs only),
  * identity strings (PitchLab present; Eventide absent from user-facing
    strings),
  * the processor FUID bytes in the Windows COM GUID layout (mixed-endian
    first 3 fields),
  * moduleinfo.json presence + CID + display-name identity,
  * DISTINCT processor CIDs between the two delivered variants,
  * desktop.ini + PlugIn.ico presence (canonical bundle layout),
  * sizes, SHA-256,
  * machine-code ISA summary (objdump: ymm/vfmadd/cpuid/xgetbv counts;
    the detailed per-TU proof is in
    evidence/analysis_round24/logs_r24w/r24w_simd_machinecode_verification.txt).

Machine-readable JSON + human summary. The original Eventide Octavox binary
is never involved or executed. NOT a DAW test (see the delivery report for
the honest four-state classification).
"""
import hashlib, json, os, struct, subprocess, sys

R24 = "/home/z/my-project/octavox-work/r24"
OUT = os.path.join(R24, "Windows_delivery")
VARIANTS = {
    "Harmonizer_Balanced": {
        "bundle": "PitchLab_Harmonizer_Balanced.vst3",
        "build_bundle": "PitchLab_Lagrange6_Balanced.vst3",  # the build name
        "build_dir": "build_win_r24w_bal",
        "display": "PitchLab Harmonizer Lagrange6 Balanced",
        "detector": "Decimated YIN (realtime production candidate)",
        "processor_fuid": "6F4E6258-9A31-42E1-8F3C-5A971D2B4E60",  # preserved lineage
    },
    "Harmonizer_FullRate": {
        "bundle": "PitchLab_Harmonizer_FullRate.vst3",
        "build_bundle": "PitchLab_Lagrange6_FullRate.vst3",
        "build_dir": "build_win_r24w_full",
        "display": "PitchLab Harmonizer Lagrange6 FullRate",
        "detector": "FullRate YIN (high-cost detector reference)",
        "processor_fuid": "C13E1BB5-5BFB-0598-F25D-DD4A52B0E817",  # R21 mint, distinct
    },
}
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


def parse_pe(mod):
    d = open(mod, "rb").read()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    machine = struct.unpack_from("<H", d, e + 4)[0]
    nsec = struct.unpack_from("<H", d, e + 6)[0]
    optsz = struct.unpack_from("<H", d, e + 20)[0]
    magic = struct.unpack_from("<H", d, e + 24)[0]
    exp_rva = struct.unpack_from("<I", d, e + 24 + 112)[0]
    secs = []
    so = e + 24 + optsz
    for i in range(nsec):
        o = so + 40 * i
        name = d[o:o+8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", d, o + 8)
        secs.append((name, va, vsize, raw, rsize))

    def rva2off(rva):
        for name, va, vsize, raw, rsize in secs:
            if va <= rva < va + vsize:
                return raw + (rva - va)
        return None

    imports = []
    imp_rva = struct.unpack_from("<I", d, e + 24 + 120)[0]
    off = rva2off(imp_rva) if imp_rva else None
    if off:
        while True:
            ent = struct.unpack_from("<IIHHI", d, off)
            if ent[4] == 0:
                break
            name_off = rva2off(ent[4])
            if name_off is None:
                break
            end = d.index(b"\0", name_off)
            imports.append(d[name_off:end].decode("ascii", "replace"))
            off += 20
    exports = []
    if exp_rva:
        eo = rva2off(exp_rva)
        if eo:
            nnames = struct.unpack_from("<I", d, eo + 24)[0]
            names_rva = struct.unpack_from("<I", d, eo + 32)[0]
            no = rva2off(names_rva)
            if no:
                for i in range(nnames):
                    nrva = struct.unpack_from("<I", d, no + 4 * i)[0]
                    fo = rva2off(nrva)
                    if fo:
                        end = d.index(b"\0", fo)
                        exports.append(d[fo:end].decode("ascii", "replace"))
    return machine, magic, secs, imports, exports, d


def isa_counts(mod):
    """objdump -d instruction counters (pe-x86-64)."""
    try:
        txt = subprocess.run(["objdump", "-d", mod],
                             capture_output=True, text=True, timeout=300).stdout
    except Exception as ex:
        return {"error": str(ex)}
    import re
    return {
        "ymm_refs": len(re.findall(r"ymm\d", txt)),
        "vfmadd": len(re.findall(r"vfmadd", txt)),
        "cpuid": len(re.findall(r"\bcpuid\b", txt)),
        "xgetbv": len(re.findall(r"\bxgetbv\b", txt)),
    }


def main():
    results = []
    all_ok = True
    cids = {}
    for name, spec in VARIANTS.items():
        errs = []
        bundle_dir = os.path.join(OUT, "plugins", spec["bundle"])
        mod = os.path.join(bundle_dir, "Contents", "x86_64-win",
                           spec["bundle"])
        rec = {"variant": name, "detector": spec["detector"],
               "display": spec["display"],
               "build_name": spec["build_bundle"],
               "build_dir": spec["build_dir"]}
        ok = check(os.path.isfile(mod), "module DLL missing", errs)
        if ok:
            machine, magic, secs, imports, exports, d = parse_pe(mod)
            check(machine == 0x8664, "not x86-64", errs)
            check(magic == 0x20B, "not PE32+", errs)
            check("GetPluginFactory" in exports,
                  "GetPluginFactory export missing (export dir)", errs)
            check(b"GetPluginFactory\x00" in d,
                  "GetPluginFactory string missing", errs)
            bad_imports = [i for i in imports if i not in ALLOW_DLLS]
            check(not bad_imports,
                  "unexpected imports: %s" % bad_imports, errs)
            # FUID in Windows COM GUID layout: the 16 GUID bytes (mixed
            # endian first 3 fields) must appear verbatim in the image.
            u = spec["processor_fuid"].replace("-", "")
            b = bytes.fromhex(u)
            g = b[0:4][::-1] + b[4:6][::-1] + b[6:8][::-1] + b[8:16]
            check(g in d, "processor FUID bytes not found", errs)
            # the OTHER variant's FUID must NOT be present
            for other_name, other in VARIANTS.items():
                if other_name == name:
                    continue
                ou = other["processor_fuid"].replace("-", "")
                ob = bytes.fromhex(ou)
                og = ob[0:4][::-1] + ob[4:6][::-1] + ob[6:8][::-1] + ob[8:16]
                check(og not in d,
                      "OTHER variant's FUID present (%s)" % other_name, errs)
            # identity strings
            check(b"PitchLab" in d, "PitchLab identity missing", errs)
            check(b"Eventide" not in d,
                  "Eventide string present (must be absent)", errs)
            # canonical bundle layout
            check(os.path.isfile(os.path.join(bundle_dir, "PlugIn.ico")),
                  "PlugIn.ico missing", errs)
            check(os.path.isfile(os.path.join(bundle_dir, "desktop.ini")),
                  "desktop.ini missing", errs)
            # moduleinfo identity
            mi = os.path.join(bundle_dir, "Contents", "Resources",
                              "moduleinfo.json")
            check(os.path.isfile(mi), "moduleinfo.json missing", errs)
            if os.path.isfile(mi):
                mtext = open(mi, encoding="utf-8", errors="replace").read()
                check(spec["processor_fuid"].replace("-", "") in mtext,
                      "moduleinfo CID mismatch", errs)
                check(spec["display"] in mtext,
                      "moduleinfo display name missing", errs)
                check("PitchLab_Harmonizer" in mtext,
                      "moduleinfo module name (delivery rename) missing", errs)
                check("Eventide" not in mtext,
                      "moduleinfo: Eventide present", errs)
            cids[name] = spec["processor_fuid"]
            rec.update({
                "size": os.path.getsize(mod),
                "sha256": sha256(mod),
                "machine": hex(machine),
                "magic": hex(magic),
                "imports": imports,
                "exports": exports,
                "isa_counts": isa_counts(mod),
            })
        rec["ok"] = not errs
        rec["errors"] = errs
        all_ok = all_ok and not errs
        results.append(rec)
        print("%-22s %s" % (name, "OK" if not errs else "FAIL: " + "; ".join(errs)))
    # the two delivered variants must have DISTINCT class ids
    if len(cids) == 2:
        distinct = cids["Harmonizer_Balanced"] != cids["Harmonizer_FullRate"]
        print("distinct processor CIDs: %s" % ("OK" if distinct else "FAIL"))
        all_ok = all_ok and distinct
    out = {"all_ok": all_ok, "variants": results}
    jsonp = os.path.join(OUT, "r24w_windows_verification.json")
    json.dump(out, open(jsonp, "w"), indent=2)
    print("json:", jsonp)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
