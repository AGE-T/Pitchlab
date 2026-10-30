import json
import sys

d = json.load(open(sys.argv[1], encoding="utf-8"))
print(d["normalized_result"][:int(sys.argv[2]) if len(sys.argv) > 2 else 6000])
