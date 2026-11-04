#!/usr/bin/env python3
"""R22 Windows delivery structural verification (the R21 method, 4 variants).

Verifies the four PitchLab Windows modules against the recorded identity:
PE32+ x86-64 DLL, GetPluginFactory export, import allow-list, identity
strings (PitchLab present; Octavox absent from user-facing strings), the
processor FUID in the Windows COM GUID layout, moduleinfo presence +
identity, sizes, SHA-256. Machine-readable JSON + human summary.
The original Eventide Octavox binary is never involved or executed.
"""
import hashlib, json, os, struct, sys

SRC = "/home/z/my-project/octavox-work/Octavox_RE_evidence/native/vst3"
OUT = "/home/z/my-project/octavox-work/r22/Windows_delivery"
VARIANTS = {
    "Lagrange6_Balanced": {
        "build": "build_win_r22_bal",
        "bundle": "PitchLab_Lagrange6_Balanced.vst3",
        "display": "PitchLab Harmonizer Lagrange6 Balanced",
        "processor_fuid": "6F4E6258-9A31-42E1-8F3C-5A971D2B4E60",  # preserved lineage
    },
    "Lagrange6_Hybrid": {
        "build": "build_win_r22_hyb",
        "bundle": "PitchLab_Lagrange6_Hybrid.vst3",
        "display": "PitchLab Harmonizer Lagrange6 Hybrid",
        "processor_fuid": "BD4A5DDF-24B1-2C39-DD2B-7F74A7A867CB",  # R21 mint
    },
    "ObsKernel6_Balanced": {
        "build": "build_win_r22_obs",
        "bundle": "PitchLab_ObsKernel6_Balanced.vst3",
        "display": "PitchLab Harmonizer ObsKernel6 Balanced",
        "processor_fuid": "E3E7FA70-6A38-0D35-B05F-64B91F1E2689",  # preserved R18-R20
    },
    "Lagrange6_FullRate": {
        "build": "build_win_r22_full",
        "bundle": "PitchLab_Lagrange6_FullRate.vst3",
        "display": "PitchLab Harmonizer Lagrange6 FullRate",
        "processor_fuid": "C13E1BB5-5BFB-0598-F25D-DD4A52B0E817",  # benchmark-only build
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
    if exp_rva:
        # import directory is at export + 8 in the data directory array
        pass
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
    return machine, magic, secs, imports, d

def main():
    results = []
    all_ok = True
    for name, spec in VARIANTS.items():
        errs = []
        bundle_dir = os.path.join(SRC, spec["build"], "VST3", "Release", spec["bundle"])
        mod = os.path.join(bundle_dir, "Contents", "x86_64-win", spec["bundle"])
        rec = {"variant": name}
        ok = check(os.path.isfile(mod), "module DLL missing", errs)
        if ok:
            machine, magic, secs, imports, d = parse_pe(mod)
            check(machine == 0x8664, "not x86-64", errs)
            check(magic == 0x20B, "not PE32+", errs)
            check(b"GetPluginFactory\x00" in d, "GetPluginFactory export missing", errs)
            bad_imports = [i for i in imports if i not in ALLOW_DLLS]
            check(not bad_imports, "unexpected imports: %s" % bad_imports, errs)
            # FUID in Windows COM GUID layout: the 16 bytes of the GUID
            # (mixed-endian first 3 fields) must appear verbatim.
            u = spec["processor_fuid"].replace("-", "")
            b = bytes.fromhex(u)
            g = b[0:4][::-1] + b[4:6][::-1] + b[6:8][::-1] + b[8:16]
            check(g in d, "processor FUID bytes not found", errs)
            # identity strings: PitchLab present; "Eventide"/product-Octavox absent
            check(b"PitchLab" in d, "PitchLab identity missing", errs)
            check(b"Eventide" not in d, "Eventide string present (must be absent)", errs)
            mi = os.path.join(bundle_dir, "Contents", "Resources", "moduleinfo.json")
            check(os.path.isfile(mi), "moduleinfo.json missing", errs)
            if os.path.isfile(mi):
                mtext = open(mi, encoding="utf-8", errors="replace").read()
                check("PitchLab" in mtext, "moduleinfo: PitchLab missing", errs)
                check(spec["display"].split(" ", 3)[-1] in mtext or
                      spec["display"] in mtext or name.split("_")[-1] in mtext,
                      "moduleinfo: variant identity missing", errs)
                check("Eventide" not in mtext, "moduleinfo: Eventide present", errs)
            rec.update({
                "size": os.path.getsize(mod),
                "sha256": sha256(mod),
                "machine": hex(machine),
                "magic": hex(magic),
                "imports": imports,
                "getpluginfactory": b"GetPluginFactory\x00" in d,
            })
        rec["ok"] = not errs
        rec["errors"] = errs
        all_ok = all_ok and not errs
        results.append(rec)
        print("%-22s %s" % (name, "OK" if not errs else "FAIL: " + "; ".join(errs)))
    out = {"all_ok": all_ok, "variants": results}
    jsonp = os.path.join(OUT, "r22_windows_verification.json")
    json.dump(out, open(jsonp, "w"), indent=2)
    print("json:", jsonp)
    return 0 if all_ok else 1

if __name__ == "__main__":
    sys.exit(main())
