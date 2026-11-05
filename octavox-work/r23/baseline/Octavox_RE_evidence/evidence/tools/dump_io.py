import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.loads((HERE / "kernel_modules.json").read_text(encoding="utf-8"))
mods = d["mimo"]

WANT = ["iswitch", "iswitch1", "iswitch2", "iswitch3", "iswitch4", "iswitch5", "iswitch6",
        "iswitch7", "iswitch8", "iswitch9", "iswitch10", "iswitch11", "c_or", "c_and",
        "adc", "left in", "right in", "subtract", "subtract1", "subtract2", "feedback",
        "feedback1", "mfbk", "bypa", "ouch", "ogan", "igan", "inch", "highcut", "highcut1",
        "just for the filter", "just for the filter2", "monoSumWetChans", "c_subtract1",
        "c_subtract3", "c_switch", "c_switch7", "c_switch8", "miso", "siso"]

for m in mods:
    if m["name"] in WANT:
        print("[%s] %s  add=%s slot=%s :: %s" % (
            m["index"], m["name"], m["add_fn"], m["slot"], "; ".join(m.get("record") or [])))

print()
print("### all module names, in index order (mimo)")
for m in mods:
    print("%s\t%s" % (m["index"], m["name"]))
