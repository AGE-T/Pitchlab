#!/usr/bin/env python3
"""R24-v2: class-ID audit across the nine delivered VST3 bundles.

Reads every bundle's moduleinfo.json, extracts the processor class CID +
class Name, and gates:
  (1) nine bundles found, expected names;
  (2) the CIDs are pairwise DISTINCT (Correction B);
  (3) every CID matches the pinned matrix from octavox_build_config.h
      (R24-v2), including the preserved Lagrange6 Balanced lineage FUID.

Usage: class_id_audit.py <plugins-dir>        (exit 0 iff all gates pass)
"""
import json
import re
import sys
from pathlib import Path

PINNED = [
    ("PitchLab_Lagrange6_Balanced", "6F4E62589A3142E18F3C5A971D2B4E60",
     "PitchLab Harmonizer Lagrange6 Balanced"),
    ("PitchLab_Lagrange6_Hybrid", "BD4A5DDF24B12C39DD2B7F74A7A867CB",
     "PitchLab Harmonizer Lagrange6 Hybrid"),
    ("PitchLab_Lagrange6_FullRate", "C13E1BB55BFB0598F25DDD4A52B0E817",
     "PitchLab Harmonizer Lagrange6 FullRate"),
    ("PitchLab_Lagrange6Q", "F0E1B5DF3809FA49A4DBE929C38CDF08",
     "PitchLab Harmonizer Lagrange6Q"),
    ("PitchLab_Lagrange6Q_Hybrid", "E37DC7BCA7F66D0D726A9477963B2A5D",
     "PitchLab Harmonizer Lagrange6Q Hybrid"),
    ("PitchLab_Lagrange6Q_FullRate", "2F2E7034591E46F30794E1E13DD51B96",
     "PitchLab Harmonizer Lagrange6Q FullRate"),
    ("PitchLab_ObsKernel6_Balanced", "E3E7FA706A380D35B05F64B91F1E2689",
     "PitchLab Harmonizer ObsKernel6 Balanced"),
    ("PitchLab_ObsKernel6_Hybrid", "A9BE5A71D192F1BA5A32D5FA9315FFC0",
     "PitchLab Harmonizer ObsKernel6 Hybrid"),
    ("PitchLab_ObsKernel6_FullRate", "64E9EEBAF112DDEDABBE2CD9242695D3",
     "PitchLab Harmonizer ObsKernel6 FullRate"),
]


def main() -> int:
    root = Path(sys.argv[1])
    bundles = sorted(root.glob("*.vst3")) + sorted(
        p for sub in ("recommended", "additional_variants") for p in
        (root / sub).glob("*.vst3"))
    if len(bundles) != 9:
        print(f"FAIL: expected 9 bundles under {root}, found {len(bundles)}")
        return 1

    found = {}
    ok = True
    for b in bundles:
        mi = b / "Contents" / "Resources" / "moduleinfo.json"
        if not mi.is_file():
            print(f"FAIL: {b.name}: missing moduleinfo.json")
            ok = False
            continue
        text = mi.read_text()
        # SDK emits JSON5-ish (trailing commas); strip them for parsing.
        text = re.sub(r",\s*([\]}])", r"\1", text)
        data = json.loads(text)
        classes = data.get("Classes", [])
        if len(classes) != 1:
            print(f"FAIL: {b.name}: {len(classes)} classes (expected 1)")
            ok = False
            continue
        cid = classes[0]["CID"].upper()
        name = classes[0]["Name"]
        vendor = classes[0].get("Vendor", "")
        subcats = classes[0].get("Sub Categories", [])
        found[b.stem] = cid
        print(f"  {b.name}: CID={cid} name={name!r} vendor={vendor!r} "
              f"subcats={subcats}")
        if vendor != "PitchLab Project":
            print(f"    FAIL: unexpected vendor {vendor!r}")
            ok = False

    # (2) pairwise distinct
    cids = list(found.values())
    if len(set(cids)) != len(cids):
        seen = {}
        for n, c in found.items():
            seen.setdefault(c, []).append(n)
        for c, names in seen.items():
            if len(names) > 1:
                print(f"FAIL: CID COLLISION {c}: {names}")
        ok = False
    else:
        print(f"pairwise-distinct CIDs across {len(cids)} bundles: OK")

    # (3) pinned matrix agreement
    for bundle, cid, display in PINNED:
        got = found.get(bundle)
        if got is None:
            print(f"FAIL: expected bundle {bundle} not found")
            ok = False
        elif got != cid:
            print(f"FAIL: {bundle}: CID {got} != pinned {cid}")
            ok = False
    if ok:
        print("pinned-matrix agreement (incl. preserved Lagrange6 Balanced "
              "lineage FUID 6F4E6258...): OK")
    print("RESULT: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
