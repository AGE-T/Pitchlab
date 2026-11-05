import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.loads((HERE / "kernel_modules.json").read_text(encoding="utf-8"))
mods = d["mimo"]

pats = [
    r"^int\d*$", r"^int sub \d+$", r"^c_fltr_a\d+$", r"^live.*$", r"^rdm .*$",
    r"^c_divide\d*$", r"^multiply1[0-2]$", r"^add8$", r"^multiply12$",
    r"^iswitch\d*$", r"^softclip\d*$",
]
print("### index-less modules in `mimo` profile:")
for m in mods:
    if m["index"] is None:
        print("  [None] %s slot=%s add=%s" % (m["name"], m["slot"], m["add_fn"]))
print()

for p in pats:
    rx = re.compile(p)
    hit = [m for m in mods if rx.match(m["name"])]
    print("### %s (%d)" % (p, len(hit)))
    for m in hit:
        print("  [%s] %s  add=%s slot=%s :: %s" % (
            m["index"], m["name"], m["add_fn"], m["slot"], "; ".join(m.get("record") or [])))
    print()
