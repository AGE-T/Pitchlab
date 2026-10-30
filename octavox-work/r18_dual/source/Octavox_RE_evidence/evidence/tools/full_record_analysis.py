"""Rigorous graph analysis over the FULL module records (kernel_modules.json).

`kernel_modules.txt` truncates every record to its first 24 entries, so any
reachability claim made from it is unsound: a consumer mentioned in entry 25+ would be
invisible.  This script re-does the analysis on the untruncated `kernel_modules.json`.

Outputs (written next to this script):
  full_records.txt   -- the complete record of every interesting module
  dead_nets.txt      -- every producer output with NO reader, per profile
  voice_chain_map.txt-- per-voice chains, re-derived from full records

Read-only: the target binary is never opened, let alone written.
"""
import json
import re
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "kernel_modules.json"


def load():
    return json.loads(SRC.read_text(encoding="utf-8"))


def entries(mod):
    return mod.get("record") or []


# Every output-suffix actually used by the shipped records (enumerated from the JSON):
#   out / out1..8 / output / null / high / left / right / eq / maximum / minimum /
#   dbvalue / in1 / in2 / ol / pitch / tonality
REF = re.compile(
    r"^(?P<base>.+?)-(?:out\d*|output|null|high|low|left|right|eq|maximum|minimum|dbvalue|in[12]|ol|pitch|tonality)$"
)


def inputs(mod):
    """(producer_net, field) pairs referenced as inputs by this module's full record."""
    out = []
    for e in entries(mod):
        if "=" not in e:
            continue
        k, v = e.split("=", 1)
        m = REF.match(v.strip())
        if m:
            out.append((m.group("base"), k))
    return out


def outputs(mod):
    """Producer net names of this module (its record lists no outputs; the net name is
    the module name -- modules are named uniquely enough for this purpose)."""
    return {mod["name"]}


def analyse(profile, mods):
    readers = defaultdict(list)
    for m in mods:
        for net, field in inputs(m):
            readers[net].append((m["name"], field))
    dead = [m["name"] for m in mods if not readers.get(m["name"])]
    return readers, sorted(set(dead))


def main():
    data = load()
    rec_out = ["# Full (untruncated) module records, extracted from kernel_modules.json", ""]
    dead_out = ["# Nets with no reader, computed from FULL records (kernel_modules.json)", ""]

    for profile, mods in data.items():
        rec_out.append(f"## profile `{profile}` ({len(mods)} modules)")
        rec_out.append("")
        for m in mods:
            rec_out.append(f"[{m['index']}] {m['name']}  add={m['add_fn']} slot={m['slot']} rec={m['record_va']}")
            for e in entries(m):
                rec_out.append("    " + e)
            rec_out.append("")
        readers, dead = analyse(profile, mods)
        dead_out.append(f"## profile `{profile}`")
        dead_out.append("")
        dead_out.append("Modules whose output net is referenced by nobody (full records):")
        for d in dead:
            dead_out.append(f"* {d}")
        dead_out.append("")
        if profile == "mimo":
            for net in ("add8", "c_fltr_a13", "c_fltr_a14", "c_fltr_a15", "c_fltr_a21", "c_fltr_a36"):
                r = readers.get(net, [])
                dead_out.append(f"* readers of `{net}-out`: " +
                                (", ".join(f"{a}.{b}" for a, b in r) if r else "NONE"))
            dead_out.append("")
            dead_out.append("Modules with zero referenced inputs (sources):")
            for m in mods:
                if not inputs(m) and not entries(m):
                    dead_out.append(f"* {m['name']} (empty record)")
            dead_out.append("")

    (HERE / "full_records.txt").write_text("\n".join(rec_out) + "\n", encoding="utf-8")
    (HERE / "dead_nets.txt").write_text("\n".join(dead_out) + "\n", encoding="utf-8")
    print("written:", HERE / "full_records.txt", HERE / "dead_nets.txt")
    print()
    print("\n".join(dead_out))


if __name__ == "__main__":
    main()
