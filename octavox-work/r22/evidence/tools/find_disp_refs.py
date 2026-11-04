"""Find every instruction in .text that uses a given RIP-independent displacement.

Usage:  python find_disp_refs.py 0xA4E8 [0x420 ...]

Read-only: reads the target in place.  Scans the raw .text bytes for the 4-byte
little-endian displacement and decodes the preceding bytes far enough to say
whether it is a load, a store, a lea, a cmp/test, or an arithmetic memory operand.

Output columns: va  kind  bytes
"""
import json
import struct
import sys
from pathlib import Path

TARGET = Path(r"F:\VST\OV_DIS\Ox.vst3")
PE = Path(__file__).with_name("pe_info.json")

# opcodes whose ModRM follows directly (after optional prefixes/REX)
SIMPLE = {
    0x8B: "mov-load",
    0x89: "mov-store",
    0x8D: "lea",
    0x0F: "two-byte",
    0x83: "alu-imm8",
    0x81: "alu-imm32",
    0x39: "cmp",
    0x3B: "cmp",
    0x85: "test",
    0x88: "mov8-store",
    0x8A: "mov8-load",
}


def main() -> None:
    pe = json.loads(PE.read_text(encoding="utf-8"))
    base = int(pe["image_base"], 16)
    data = TARGET.read_bytes()

    text = next(s for s in pe["sections"] if s["name"] == ".text")
    tva, tptr, tsize = text["va"], text["ptr"], text["rsize"]
    blob = data[tptr : tptr + tsize]

    for arg in sys.argv[1:]:
        disp = int(arg, 16)
        needle = struct.pack("<I", disp)
        print(f"== displacement {disp:#x} ==")
        n = 0
        start = 0
        while True:
            i = blob.find(needle, start)
            if i < 0:
                break
            start = i + 1
            va = base + tva + i
            # walk back over up to 6 bytes of prefixes/opcode
            j = i - 1
            window = blob[max(0, i - 8) : i + 4]
            n += 1
            print(f"  {va:#012x}  ...{window.hex()}")
        print(f"  total {n}")


if __name__ == "__main__":
    main()
