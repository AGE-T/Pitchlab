import json
import sys

want = sys.argv[1].lower()
d = json.load(open("rtti_vtables.json", encoding="utf-8"))
for rec in d:
    vts = rec.get("vftables") or []
    for v in vts:
        if str(v.get("vtable_va", "")).lower().startswith(want):
            print("class:", rec["label"], rec["mangled"])
            print("  col:", v.get("col_va"))
            vt = v["vtable_va"]
            for i, m in enumerate(rec["methods"].get(vt, [])):
                print("   slot %2d (+0x%02X) = %s" % (i, i * 8, m))
