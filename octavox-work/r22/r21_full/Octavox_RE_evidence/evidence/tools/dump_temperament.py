"""Dump the static scale/temperament tables cited in OCTAVOX_ROUND6_DSP_FINDINGS.md.

Read-only: opens the target in place, never writes it.

Tables:
  0x1803BB690  5 rows x 12 int32 (cents), row stride 0x30
  0x1803BB60C  12-step -> degree map (17 int8)
  0x1803BB610  second degree map (17 int8)
  0x1803BB780  0/1 interval patterns
"""
import json
import struct
from pathlib import Path

TARGET = Path(r"F:\VST\OV_DIS\Ox.vst3")
PE = Path(__file__).with_name("pe_info.json")


def main() -> None:
    pe = json.loads(PE.read_text(encoding="utf-8"))
    base = int(pe["image_base"], 16)
    data = TARGET.read_bytes()

    def va2fo(va: int) -> int:
        rva = va - base
        for s in pe["sections"]:
            if s["va"] <= rva < s["va"] + s["rsize"]:
                return s["ptr"] + (rva - s["va"])
        raise ValueError(hex(va))

    print("target:", TARGET)
    print("image base:", hex(base))
    print()

    names = ["12-TET", "just", "temperament-3", "temperament-4", "temperament-5"]
    for r in range(5):
        va = 0x1803BB690 + r * 0x30
        fo = va2fo(va)
        vals = struct.unpack("<12i", data[fo : fo + 48])
        print(f"row {r} @ {va:#x}  {names[r]:>14}: {list(vals)}")

    print()
    for va, n in ((0x1803BB60C, 18), (0x1803BB610, 17)):
        fo = va2fo(va)
        vals = struct.unpack(f"<{n}i", data[fo : fo + 4 * n])
        print(f"degree map  @ {va:#x} (int32 x{n}): {list(vals)}")

    print()
    for va in (0x1803BB660, 0x1803BB780):
        fo = va2fo(va)
        vals = struct.unpack("<12i", data[fo : fo + 48])
        print(f"interval pat @ {va:#x} (int32 x12): {list(vals)}")


if __name__ == "__main__":
    main()
