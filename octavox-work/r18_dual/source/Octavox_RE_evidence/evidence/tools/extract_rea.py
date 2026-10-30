import json, os, sys, re

base = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(base, "rea_session_evidence.json")
with open(src, "r", encoding="utf-8") as f:
    doc = json.load(f)

recs = doc.get("records") or doc.get("evidence") or []
if isinstance(recs, dict):
    recs = list(recs.values())

n = 0
for rec in recs:
    nr = rec.get("normalized_result")
    if not isinstance(nr, dict):
        continue
    items = nr.get("items")
    if not isinstance(items, list):
        continue
    op = rec.get("operation", "")
    for it in items:
        if not isinstance(it, dict):
            continue
        pc = it.get("pseudocode")
        if not isinstance(pc, str):
            continue
        addr = it.get("address") or (it.get("procedure") or {}).get("address") or "unknown"
        a = addr.replace("0x", "")
        out = os.path.join(base, "rea3_%s.txt" % a)
        pc = pc.replace("\r\r\n", "\n").replace("\r\n", "\n")
        with open(out, "w", encoding="utf-8") as g:
            g.write(pc)
        n += 1
        print("wrote", out, len(pc.split("\n")), "lines")
print("total", n)
