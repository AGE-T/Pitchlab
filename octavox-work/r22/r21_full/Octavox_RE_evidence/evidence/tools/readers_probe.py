import json, re, collections
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.loads((HERE / "kernel_modules.json").read_text(encoding="utf-8"))
mimo = d["mimo"]

REF = re.compile(
    r"^(?P<base>.+?)-(?:out\d*|output|null|high|low|left|right|eq|maximum|minimum|dbvalue|in[12]|ol|pitch|tonality)$"
)

readers = collections.defaultdict(list)
for m in mimo:
    for e in (m.get("record") or []):
        if "=" not in e:
            continue
        k, v = e.split("=", 1)
        mm = REF.match(v.strip())
        if mm:
            readers[mm.group("base")].append((m["name"], k))

NETS = [
    "c_adder8", "c_adder9", "c_adder15", "c_minmax", "c_minmax2", "bound", "bound1",
    "c_fltr_a14", "multiply12", "add8", "c_switch", "softclip", "softclip1", "softclip2",
    "dly1", "sht1", "rdep", "rdm slew 1", "c_multiply 1", "minp", "keys", "scal table",
    "tune", "xfad", "ptrk", "adc", "c_divide3", "c_fltr_a13", "c_fltr_a36", "in",
]
for net in NETS:
    r = readers.get(net, [])
    print("%-14s <- %s" % (net, ", ".join("%s.%s" % (a, b) for a, b in r) if r else "NONE"))
