"""R28 S1d: vtable placement scan for the event core + m02 default path.

(a) find all DATA-section qwords equal to the event-core addresses
    (publisher A/B, dispatcher, thunks) -> who hosts them in vtables;
(b) find all qwords == 0x1800E1520 (m02) -> every vtable hosting it;
(c) dump m02's default path 0x1800E1871..end to see what non-id calls do.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     dump, load_pdata, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

addrs = {
    'publisher_A': 0x1800C27E0,
    'publisher_B': 0x1800C2960,
    'dispatcher': 0x1800C2CE0,
    'thunk_5730': 0x1800C5730,
    'thunk_5740': 0x1800C5740,
    'thunk_5750': 0x1800C5750,
    'm02_FUN_1800E1520': 0x1800E1520,
    'm01_slot1': None,  # filled below from the engine vtable
}

# read engine vtable slots 0..8 for context
print('=== engine vtable 0x180409F60 slots ===')
for k in range(10):
    v = pe.read_u64(0x180409F60 + 8 * k)
    print('  slot %d (+0x%X): 0x%X' % (k, 8 * k, v or 0))
addrs['m01_slot1'] = pe.read_u64(0x180409F60 + 8)

ev = {}
for name, va in addrs.items():
    if va is None:
        continue
    hits = []
    for s in pe.sections:
        if s['name'] not in ('.rdata', '.data', '.text', '.pdata', 'rdata', 'data'):
            continue
        base = pe.image_base + s['va']
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        pat = struct.pack('<Q', va)
        off = 0
        while True:
            i = blob.find(pat, off)
            if i < 0:
                break
            hits.append(base + i)
            off = i + 1
    ev[name] = [hex(h) for h in hits]
    print('%-20s appears at: %s' % (name, ', '.join(hex(h) for h in hits) if hits else '(nowhere in scanned sections)'))

dump_json('r28_s1d_vtable_placements.json', ev)

# (c) m02 default path
dump(pe, md, 0x1800E1871, 0x1800E18B0, 'M02 default path 0x1800E1871..')

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
