import struct

data = open(r'F:\VST\OV_DIS\Ox.vst3', 'rb').read()

def va_of(off): return off + 0x180000000 + 0x1A00

def strings_at(off, maxn=40):
    """Walk forward from off, collecting null-terminated strings."""
    cur = off
    out = []
    for i in range(maxn):
        e = data.find(b'\x00', cur)
        if e < 0: break
        st = data[cur:e]
        out.append((va_of(cur), st))
        cur = e + 1
        if len(st) == 0 and i > 4:
            break
    return out

def block(needle):
    print('=' * 70)
    idx = 0
    while True:
        idx = data.find(needle, idx)
        if idx < 0: break
        print('--- hit at file 0x%X VA 0x%X ---' % (idx, va_of(idx)))
        cur = off0 = idx
        for va, st in strings_at(idx, 45):
            try:
                print('  0x%X: %r' % (va, st.decode('utf-8', 'replace')[:90]))
            except Exception:
                pass
            if b'nentries' in st:
                pass
        print()
        idx += 1

# record blocks: find their first string
block(b'nentries=15')
print()
print('##### c_cmp2 / scal-out comparisons #####')
block(b'in1=scal table-out')
