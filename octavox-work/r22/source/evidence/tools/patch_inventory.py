import os, re, collections
OUT = os.path.dirname(os.path.abspath(__file__))
rows = []
for line in open(os.path.join(OUT, 'strings_all.txt'), encoding='utf-8', errors='replace'):
    p = line.rstrip('\n').split('\t', 2)
    if len(p) == 3:
        rows.append((int(p[0], 16), p[1], p[2]))

out = []
# 1) Eventide modpp DSP module classes
mods = [(o, s) for (o, k, s) in rows if 'modpp' in s]
out.append('===== modpp classes (%d) =====' % len(mods))
pat = re.compile(r'\.\?A([UVA])?(\w+?)@(?:(\w+)_t@)?modules@modpp@@')
seen = set()
for o, s in sorted(mods, key=lambda x: x[1]):
    out.append('%08X %s' % (o, s))
open(os.path.join(OUT, 'modpp_classes.txt'), 'w', encoding='utf-8').write('\n'.join(out))
print('modpp classes:', len(mods))

# 2) all distinct module type names referenced by patch text
body = [re.sub(r'^[0-9A-F]{8}  ', '', l.rstrip('\n')) for l in open(os.path.join(OUT, 'patch_text.txt'), encoding='utf-8', errors='replace')]
names = sorted(set(b.split('=', 1)[1] for b in body if b.startswith('name=')))
open(os.path.join(OUT, 'patch_names.txt'), 'w', encoding='utf-8').write('\n'.join(names))
print('name= values:', len(names))

# 3) patch instance ids from in/out values  (format <instid>-<port>)
ids = collections.Counter()
for b in body:
    m = re.match(r'^(in\d*|out\d*|kids\d*|select|input)=(.*)$', b)
    if m:
        v = m.group(2).strip()
        mm = re.match(r'^([A-Za-z_][\w\.]*)-(.+)$', v)
        if mm:
            ids[mm.group(1)] += 1
open(os.path.join(OUT, 'patch_instances.txt'), 'w', encoding='utf-8').write(
    '\n'.join('%4d  %s' % (v, k) for k, v in sorted(ids.items(), key=lambda x: (-x[1], x[0]))))
print('patch instance ids:', len(ids))

# 4) statement= UI labels
st = sorted(set(b.split('=', 1)[1] for b in body if b.startswith('statement=')))
open(os.path.join(OUT, 'patch_statements.txt'), 'w', encoding='utf-8').write('\n'.join(st))
print('statement= labels:', len(st))
