"""Derive the per-voice module chains of the `mimo` kernel profile from the FULL records.

Input : kernel_modules.json  (untruncated per-module parameter records)
Output: voice_chain_map.txt, written next to this script.

`kernel_modules.txt` truncates each record to its first 24 entries, so a consumer
mentioned later (e.g. `dtonicii9.pcentN`, `stereomixer3.pan7/8`) is invisible there.
This script therefore reads the JSON.  Module *names* are not systematic (voice 1 is
served by `modfilter8`, `moddelay7`, `multiply4`), so chains are resolved structurally,
by following input references, not by the trailing digit of a name.

Read-only: the target binary is never touched.
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "kernel_modules.json"
DST = HERE / "voice_chain_map.txt"

# every output suffix the shipped records actually use
REF = re.compile(
    r"^(?P<base>.+?)-(?:out\d*|output|null|high|low|left|right|eq|maximum|minimum|dbvalue|in[12]|ol|pitch|tonality)$"
)


def load_mimo(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    mods = []
    for m in data["mimo"]:
        mods.append({
            "idx": m["index"],
            "name": m["name"],
            "add": m["add_fn"],
            "slot": m["slot"],
            "recaddr": m["record_va"],
            "rec": "; ".join(m.get("record") or []),
        })
    return mods


def refs(mods, net):
    """Modules whose record references `net` as an input token (any output suffix)."""
    hits = []
    for m in mods:
        for e in m["rec"].split("; "):
            if "=" not in e:
                continue
            v = e.split("=", 1)[1].strip()
            mm = REF.match(v)
            if mm and mm.group("base") == net:
                hits.append(m)
                break
    return hits


def by_value(mods, value, pred=lambda m: True):
    """Modules whose record references the exact token `value` (e.g. `dtonicii9-out1`)."""
    hits = []
    for m in mods:
        for e in m["rec"].split("; "):
            if "=" in e and e.split("=", 1)[1].strip() == value and pred(m):
                hits.append(m)
                break
    return hits


def one(mods, net, pred=lambda m: True):
    hits = [m for m in refs(mods, net) if pred(m)]
    return hits[0] if hits else None


def tag(m):
    return f"[{m['idx']}] {m['name']}" if m else "—"


def field(rec, key):
    m = re.search(r"(?:^|; )" + re.escape(key) + r"=([^;]+)", rec)
    return m.group(1).strip() if m else None


def main():
    mods = load_mimo(SRC)
    out = []
    out.append(f"# Per-voice chains, `mimo` profile ({len(mods)} modules), from FULL records")
    out.append("#")
    out.append("# Derived structurally by derive_voice_chains.py: each row follows")
    out.append("#   dtonicii9-outN -> modfilter -> eqn -> add <- fb multiply <- moddelay")
    out.append("# Note: module *names* are not systematic (voice 1 is served by `modfilter8`,")
    out.append("# `moddelay7`, `multiply4`). Read the chain, not the trailing digit.")
    out.append("")
    hdr = ("voice", "modfilter", "eqn", "add", "moddelay", "fb multiply",
           "fb-gain smoother", "mixer in", "module delayamt", "engine delayamt",
           "engine shift", "engine pcent", "fb switch")
    out.append("| " + " | ".join(hdr) + " |")
    out.append("|" + "---|" * len(hdr))

    engine = next(m for m in mods if m["name"] == "dtonicii9")
    for n in range(1, 9):
        mf = one(mods, "dtonicii9", lambda m: any(
            e.split("=", 1)[1].strip() == f"dtonicii9-out{n}"
            for e in m["rec"].split("; ") if "=" in e))
        eq = (by_value(mods, mf["name"] + "-high") or [None])[0] if mf else None
        ad = (by_value(mods, eq["name"] + "-out", lambda m: m["name"].startswith("add")) or [None])[0] if eq else None
        md = (by_value(mods, ad["name"] + "-out", lambda m: "moddelay" in m["name"]) or [None])[0] if ad else None
        fb = (by_value(mods, md["name"] + "-out", lambda m: m["name"].startswith("multiply")) or [None])[0] if md else None
        sm = None
        if fb:
            in2 = field(fb["rec"], "in2")
            if in2 and REF.match(in2):
                sm = one(mods, REF.match(in2).group("base"))
        module_delayamt = field(md["rec"], "delayamt") if md else "—"
        engine_delayamt = field(engine["rec"], f"delayamt{n}")
        engine_shift = field(engine["rec"], f"shift{n}")
        engine_pcent = field(engine["rec"], f"pcent{n}")
        fbs = (by_value(mods, f"msw{n}-out", lambda m: m["name"].startswith("fback switch")) or [None])[0] if md else None
        out.append("| " + " | ".join([
            str(n), tag(mf), tag(eq), tag(ad), tag(md), tag(fb), tag(sm),
            tag((by_value(mods, ad["name"] + "-out", lambda m: "stereomixer" in m["name"]) or [None])[0]) if ad else "—",
            module_delayamt, engine_delayamt, engine_shift, engine_pcent, tag(fbs),
        ]) + " |")

    out += ["", "## Engine (`dtonicii9`) per-voice inputs (OBS, full record)", ""]
    for n in range(1, 9):
        out.append(
            f"* voice {n}: mod{n}={field(engine['rec'], f'mod{n}')}, "
            f"shift{n}={field(engine['rec'], f'shift{n}')}, "
            f"pcent{n}={field(engine['rec'], f'pcent{n}')}, "
            f"modamt{n}={field(engine['rec'], f'modamt{n}')}, "
            f"delayamt{n}={field(engine['rec'], f'delayamt{n}')}"
        )

    out += ["", "## Shared voice-bus arithmetic (OBS)", ""]
    for name in ("c_minmax2", "c_divide2", "c_multiply6", "c_power1", "c_fltr_a14", "bound1", "modiirq"):
        m = next((x for x in mods if x["name"] == name), None)
        if m:
            out.append(f"* {tag(m)} :: {m['rec']}")

    out += ["", "## Dangling / unread nets found by scanning every FULL record (OBS)", ""]
    for net in ("add8", "c_fltr_a13", "c_fltr_a14", "c_fltr_a15", "c_fltr_a21", "c_fltr_a36",
                "c_minmax", "c_minmax2", "multiply12", "bound", "bound1"):
        hits = refs(mods, net)
        out.append(f"* `{net}-out` -> " + (", ".join(tag(h) for h in hits) if hits else "NO READER"))

    DST.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("\n".join(out))
    print(f"\nwritten: {DST}")


if __name__ == "__main__":
    main()
