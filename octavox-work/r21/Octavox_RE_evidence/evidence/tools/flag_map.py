import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "dis_config_str.txt"
lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
last_str = None
for line in lines:
    m = re.search(r';\s*0x[0-9A-F]+ "([^"]+)"', line)
    if m and len(m.group(1)) >= 3:
        last_str = m.group(1)
    m2 = re.search(r"lea\s+r\w+, \[rsi \+ (0x3[0-9a-f]{2})\]", line)
    if m2:
        print(m2.group(1), "<-", last_str, "|", line.split("  ")[0])
