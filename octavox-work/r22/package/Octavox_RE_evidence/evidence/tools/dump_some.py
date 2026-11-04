import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.loads((HERE / "kernel_modules.json").read_text(encoding="utf-8"))

WANT = ("multiply4", "multiply11", "add", "fback switch 1", "m switch 1", "c_switch",
        "dly1", "sht1", "rdm slew 1", "c_multiply 1", "c_fltr_a36", "c_divide26",
        "c_minmax", "softclip", "keys", "scal table", "rdm sh 1", "rdm comp 1", "rdm mult")

for m in d["mimo"]:
    if m["name"] in WANT:
        print("[%s] %s  add=%s slot=%s" % (m["index"], m["name"], m["add_fn"], m["slot"]))
        for e in (m.get("record") or []):
            print("    " + e)
        print()
