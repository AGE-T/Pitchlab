import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.loads((HERE / "kernel_modules.json").read_text(encoding="utf-8"))
mods = d["mimo"]

pats = [r"^c_power\d*$", r"^c_subtract\d*$", r"^int table.*$", r"^int sub.*$",
        r"^mmix$", r"^m mix.*$", r"^c_mix.*$"]

for p in pats:
    rx = re.compile(p)
    hit = [m for m in mods if rx.match(m["name"])]
    print("### %s (%d)" % (p, len(hit)))
    for m in hit:
        print("  [%s] %s  add=%s slot=%s :: %s" % (
            m["index"], m["name"], m["add_fn"], m["slot"], "; ".join(m.get("record") or [])))
    print()

print("### tail section: modules with slot >= 0x1C3C0 (stereomixer3 and after)")
for m in mods:
    try:
        if int(m["slot"], 16) >= 0x1C3C0:
            print("  [%s] %s  add=%s slot=%s :: %s" % (
                m["index"], m["name"], m["add_fn"], m["slot"], "; ".join(m.get("record") or [])))
    except (TypeError, ValueError):
        pass
