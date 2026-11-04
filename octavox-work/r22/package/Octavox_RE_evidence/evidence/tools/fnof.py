"""Map addresses to containing functions using functions.csv; also list callers from qword/rip scan."""
import csv, sys, bisect, json

CSV = r'C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\functions.csv'
rows = []
with open(CSV) as f:
    r = csv.DictReader(f)
    for d in r:
        rows.append((int(d['start_va'], 16), int(d['end_va'], 16), int(d['size'], 10)))
rows.sort()
starts = [a for (a, b, s) in rows]

def fnof(va):
    i = bisect.bisect_right(starts, va) - 1
    if i >= 0:
        a, b, s = rows[i]
        if a <= va < b:
            return (a, b, s, va - a)
    return None

if __name__ == '__main__':
    if sys.argv[1] == 'fnof':
        for a in sys.argv[2:]:
            va = int(a, 16)
            r = fnof(va)
            if r:
                print('0x%X: in FUN 0x%X..0x%X (offs +0x%X, size %d)' % (va, r[0], r[1], r[3], r[2]))
            else:
                print('0x%X: no containing function' % va)
    elif sys.argv[1] == 'scan':
        # list all functions with size >= threshold, sorted desc
        th = int(sys.argv[2])
        big = sorted([r for r in rows if r[2] >= th], key=lambda x: -x[2])
        for (a, b, s) in big[:150]:
            print('0x%X size %d (0x%X)' % (a, s, s))
