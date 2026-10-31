#!/usr/bin/env python3
# ============================================================================
# OCTAVOX R18-WDB: structural verification of the Windows x64 VST3 modules
# ============================================================================
# Verifies BOTH variant bundles under --builds <dir> (expected layout:
#   Lagrange6/OctavoxRE_Lagrange6.vst3/Contents/x86_64-win/OctavoxRE_Lagrange6.vst3
#   ObsKernel6/OctavoxRE_ObsKernel6.vst3/Contents/x86_64-win/OctavoxRE_ObsKernel6.vst3
# ):
#   * PE32+ (magic 0x20B), x86-64 machine (0x8664), DLL characteristic flag
#   * the VST3 module entry point GetPluginFactory is exported
#   * imports restricted to the allow-list (Windows system DLLs + UCRT only;
#     no libstdc++/libc++/libgcc/libwinpthread runtime DLL dependencies)
#   * the identity strings (display name, vendor, subcategory, SDK version)
#   * the PClassInfo CID == the variant's processor FUID in the Windows COM
#     GUID byte layout (Data1/Data2/Data3 little-endian, Data4 raw -- the
#     pinned SDK's COM_COMPATIBLE INLINE_UID branch; the SAME logical UID as
#     the Linux builds' UUID-order layout)
#   * NO cross-variant FUID contamination (each module contains only its own
#     class ID)
#   * the two variant modules are NOT byte-identical (the interpolation
#     selection genuinely differs)
#   * bundle completeness (Contents/x86_64-win module, Contents/Resources/
#     moduleinfo.json, PlugIn.ico, desktop.ini) + sizes + SHA-256
# Output: machine-readable JSON (--out <path>) + a human-readable summary on
# stdout ending in "RESULT: ALL CHECKS PASSED" / "RESULT: <n> FAILED".
# ============================================================================
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

EXPECTED = {
    "Lagrange6": {
        "bundle": "OctavoxRE_Lagrange6",
        "display": "OctavoxRE Lagrange6",
        "cid_hex": "6F4E62589A3142E18F3C5A971D2B4E60",  # lineage FUID
        "cid_com": bytes.fromhex("58624E6F319AE1428F3C5A971D2B4E60"),
        "cid_com_hex": "58624E6F319AE1428F3C5A971D2B4E60",
    },
    "ObsKernel6": {
        "bundle": "OctavoxRE_ObsKernel6",
        "display": "OctavoxRE ObsKernel6",
        "cid_hex": "E3E7FA706A380D35B05F64B91F1E2689",  # minted FUID (R18-WDB)
        "cid_com": bytes.fromhex("70FAE7E3386A350DB05F64B91F1E2689"),
        "cid_com_hex": "70FAE7E3386A350DB05F64B91F1E2689",
    },
}
OTHER_CID = {
    "Lagrange6": bytes.fromhex("70FAE7E3386A350DB05F64B91F1E2689"),
    "ObsKernel6": bytes.fromhex("58624E6F319AE1428F3C5A971D2B4E60"),
}
ALLOWED_IMPORT_PREFIXES = ("KERNEL32.dll", "USER32.dll", "ole32.dll", "api-ms-win-crt-")

ID_VENDOR = b"Octavox RE Project"
ID_SUBCAT = b"Fx|Pitch Shift"
ID_SDKVER = b"VST 3.7.12"


def pe_info(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        return None, "no MZ signature"
    e_lfanew = struct.unpack("<I", data[0x3C:0x40])[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        return None, "no PE signature"
    machine, nsec, tsd, psz, nsy, szopt, chars = struct.unpack(
        "<HHIIIHH", data[e_lfanew + 4:e_lfanew + 24])
    magic = struct.unpack("<H", data[e_lfanew + 24:e_lfanew + 26])[0]
    return {
        "machine_hex": "0x%04X" % machine,
        "machine_is_amd64": machine == 0x8664,
        "pe32plus_magic": magic == 0x20B,
        "dll_flag": bool(chars & 0x2000),
        "characteristics_hex": "0x%04X" % chars,
        "time_date_stamp": tsd,
        "sections": nsec,
        "size_bytes": len(data),
    }, None


def objdump(path, args):
    return subprocess.run(["objdump"] + args + [path],
                          capture_output=True, text=True).stdout


def verify_variant(builds_dir, variant):
    spec = EXPECTED[variant]
    bundle = os.path.join(builds_dir, variant, spec["bundle"] + ".vst3")
    module = os.path.join(bundle, "Contents", "x86_64-win", spec["bundle"] + ".vst3")
    res = {"variant": variant, "checks": {}, "failures": []}

    def check(name, ok, detail=""):
        res["checks"][name] = {"pass": bool(ok), "detail": str(detail)}
        if not ok:
            res["failures"].append(name)

    # -- bundle completeness --------------------------------------------------
    for rel in ("Contents/x86_64-win/" + spec["bundle"] + ".vst3",
                "Contents/Resources/moduleinfo.json",
                "PlugIn.ico", "desktop.ini"):
        check("bundle_has:" + rel, os.path.isfile(os.path.join(bundle, rel)))

    # -- PE structure ---------------------------------------------------------
    info, err = pe_info(module)
    if err:
        check("pe_structure", False, err)
        return res
    check("pe_machine_amd64", info["machine_is_amd64"], info["machine_hex"])
    check("pe_pe32plus_magic", info["pe32plus_magic"], "0x20B")
    check("pe_dll_characteristic", info["dll_flag"], info["characteristics_hex"])
    res["pe"] = {k: v for k, v in info.items()}

    # -- file(1) corroborates --------------------------------------------------
    fdesc = subprocess.run(["file", module], capture_output=True, text=True).stdout
    check("file_pe32plus_dll_x86_64",
          "PE32+ executable for MS Windows" in fdesc and "DLL" in fdesc and "x86-64" in fdesc,
          fdesc.strip())

    # -- exports ----------------------------------------------------------------
    exports = objdump(module, ["-p"])
    check("export_GetPluginFactory", "GetPluginFactory" in exports)

    # -- imports allow-list ------------------------------------------------------
    import_dlls = sorted({line.split("DLL Name:")[1].strip()
                           for line in exports.splitlines() if "DLL Name:" in line})
    res["imports"] = import_dlls
    bad = [d for d in import_dlls if not d.startswith(ALLOWED_IMPORT_PREFIXES)]
    check("imports_allow_list", not bad, "unexpected: %s" % bad if bad else "; ".join(import_dlls))

    # -- identity strings ---------------------------------------------------------
    with open(module, "rb") as f:
        data = f.read()
    check("identity_display_name", spec["display"].encode() in data, spec["display"])
    check("identity_vendor", ID_VENDOR in data, ID_VENDOR.decode())
    check("identity_subcategory", ID_SUBCAT in data, ID_SUBCAT.decode())
    check("identity_sdk_version", ID_SDKVER in data, ID_SDKVER.decode())

    # -- PClassInfo CID (COM GUID layout) -------------------------------------------
    cat = b"Audio Module Class\x00"
    i = data.find(cat)
    cid_found = None
    if i >= 20:
        cid_found = data[i - 20:i - 4]
    check("pclass_cid_matches_fuid_com_layout",
          cid_found == spec["cid_com"],
          "found %s want %s" % (cid_found.hex().upper() if cid_found else None,
                                spec["cid_com_hex"]))
    check("no_cross_variant_fuid", OTHER_CID[variant] not in data,
          "the other variant's class ID must be absent")

    # -- moduleinfo.json identity ------------------------------------------------
    mi_path = os.path.join(bundle, "Contents", "Resources", "moduleinfo.json")
    try:
        with open(mi_path, "r") as f:
            mi = f.read()
        check("moduleinfo_cid", spec["cid_hex"] in mi)
        check("moduleinfo_name", '"%s"' % spec["bundle"] in mi)
        check("moduleinfo_display", spec["display"] in mi)
    except OSError as e:
        check("moduleinfo_readable", False, str(e))

    # -- size + sha256 --------------------------------------------------------------
    res["module_path"] = module
    res["module_size_bytes"] = info["size_bytes"]
    res["module_sha256"] = hashlib.sha256(data).hexdigest()
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--builds", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    results = [verify_variant(args.builds, v) for v in ("Lagrange6", "ObsKernel6")]

    # cross-variant: the two modules must differ
    m0 = results[0]["module_path"] if "module_path" in results[0] else None
    m1 = results[1]["module_path"] if "module_path" in results[1] else None
    differ = False
    if m0 and m1:
        with open(m0, "rb") as f:
            d0 = f.read()
        with open(m1, "rb") as f:
            d1 = f.read()
        differ = d0 != d1
    cross = {"variants_differ": differ}
    if not differ:
        for r in results:
            r["failures"].append("variants_differ")

    total_fail = sum(len(r["failures"]) for r in results)
    out = {"tool": "octavox-r18-wdb-windows-verification",
           "builds_dir": args.builds,
           "variants": results,
           "cross": cross,
           "total_failures": total_fail}
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2)

    for r in results:
        print("variant %-10s module sha256=%s size=%d bytes  %s" % (
            r["variant"], r.get("module_sha256", "?"), r.get("module_size_bytes", 0),
            "OK" if not r["failures"] else "FAILED: " + ",".join(r["failures"])))
    print("cross-variant: modules differ: %s" % cross["variants_differ"])
    if total_fail == 0:
        print("RESULT: ALL CHECKS PASSED")
    else:
        print("RESULT: %d FAILED" % total_fail)
    return 0 if total_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
