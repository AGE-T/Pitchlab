"""Regenerate F:\\VST\\OV_DIS\\Evdence\\EVIDENCE_HASHES.md.

Working set = the deliverable folder + the analysis working directory + the target.
The target is hashed for integrity only and is never written.  Deterministic: the
only non-deterministic part is the generation timestamp.
"""
import hashlib
import datetime
from pathlib import Path

DELIV = Path(r"F:\VST\OV_DIS\Evdence")
ANALYSIS = Path(r"C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis")
TARGET = Path(r"F:\VST\OV_DIS\Ox.vst3")
OUT = DELIV / "EVIDENCE_HASHES.md"

EXCLUDE_NAMES = {"EVIDENCE_HASHES.md"}
EXCLUDE_DIRS = {"__pycache__", ".git"}


def sha256(p: Path, chunk=1 << 20) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def collect(root: Path):
    out = []
    for p in sorted(root.rglob("*"), key=lambda x: str(x).lower()):
        if not p.is_file():
            continue
        if p.name in EXCLUDE_NAMES:
            continue
        if any(part in EXCLUDE_DIRS for part in p.parts):
            continue
        if p.suffix.lower() == ".zip":
            continue
        out.append(p)
    return out


def main():
    files = collect(DELIV) + collect(ANALYSIS) + [TARGET]
    # de-dup, keep deterministic order by path
    seen, uniq = set(), []
    for p in files:
        k = str(p).lower()
        if k in seen:
            continue
        seen.add(k)
        uniq.append(p)
    uniq.sort(key=lambda x: str(x).lower())

    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        "# OCTAVOX evidence hashes (SHA-256)",
        "",
        f"Generated {ts}. The target file is listed for integrity only and was never modified.",
        "",
        "| artifact | sha256 | bytes |",
        "|---|---|---|",
    ]
    for p in uniq:
        lines.append(f"| `{p}` | `{sha256(p)}` | {p.stat().st_size} |")
    lines += [
        "",
        "Notes:",
        "* Paths are absolute; this manifest describes the working set in `F:\\VST\\OV_DIS\\Evdence\\` and `OX_RESEARCH\\analysis\\`.",
        "* `Evdence\\OCTAVOX_*.md` are the 8 canonical documents (plus `PACKAGE_README.md`); `poc\\` is our own implementation (mandate \u00a79); `Evdence\\01_*.md` ... `10_*.md` are superseded first-round documents kept for history and not authoritative.",
        "* **Round-6 additions**: `Evdence\\OCTAVOX_ROUND6_DSP_FINDINGS.md`, `OCTAVOX_DSP_DEMO_USAGE.md`, `OCTAVOX_ROUND6_MEASUREMENTS.md`, `poc\\octavox_offline.py`, the example WAVs, `example_*.json`, `offline_measurements.json` and `OFFLINE_COMPARISON.md`. The round-6 static evidence is `analysis\\rea6*.txt`, `analysis\\rea6_batch*.json`, `analysis\\dis_proc.txt`, `analysis\\dis_config_str.txt`, `analysis\\flag_map.py`, `analysis\\dump_temperament.py` → `analysis\\temperament_tables.txt` and `analysis\\find_refs.py`.",
        "* **Round-4 full-record evidence**: `analysis\\kernel_modules.json` (untruncated records), `analysis\\full_record_analysis.py` \u2192 `full_records.txt` + `dead_nets.txt`, `analysis\\derive_voice_chains.py` \u2192 `voice_chain_map.txt`, `analysis\\readers_probe.py`, the `dump_*.py` probes, `analysis\\module_names_mimo.txt`.",
        "* `poc\\poc_measurements.json`, `poc\\POC_RESULTS.md` and `poc\\poc_run.log` are generated outputs whose hashes change on every run; the cost rows also vary with machine load. `poc\\octavox_poc.py` and `poc\\README.md` are source and stable. The same applies to `poc\\offline_measurements.json`, `poc\\OFFLINE_COMPARISON.md` and `poc\\example_*.json`.",
        "* **Round-7 additions**: the round-7 supplement is `Evdence\\OCTAVOX_ROUND6_DSP_FINDINGS.md` §10; new static evidence is `analysis\\rea7_gate_and_flag.txt`, `analysis\\rea7_18013BBC0_state6.txt` and `analysis\\find_disp_refs.py` (exhaustive raw-`.text` displacement scan). No PoC source changed in round 7.",
        "* The packaged zips (`F:\\VST\\OV_DIS\\Octavox_RE_evidence_round7.zip` and the earlier round zips) are excluded here to avoid a self-reference; each zip carries its own `checksums.txt`.",
        "* The target `Ox.vst3` hash is stable and matches the first-round record; it was opened read-only for analysis and never written.",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT}  ({len(uniq)} rows)")


if __name__ == "__main__":
    main()
