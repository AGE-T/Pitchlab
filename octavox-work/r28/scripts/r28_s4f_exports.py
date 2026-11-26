"""R28 S4f: walk the VST3 module export table -> GetPluginFactory ->
createInstance -> the component constructor -> its vtable.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, dump, disasm_range,
                     annotate, load_pdata, scan_calls)

pe = PE(); check_hash(pe)
md = get_disassembler()

# --- parse export directory ---
opt = struct.unpack_from('<I', pe.data, 0x3C)[0] + 4 + 20
dd = opt + 96 + 2 * 8   # data directory index 0 = export table
exp_rva, exp_size = struct.unpack_from('<II', pe.data, dd)
def rva2va(r):
    return pe.image_base + r
exp_va = rva2va(exp_rva)
fo = pe.va2fo(exp_va)
nfuncs, nnames = struct.unpack_from('<II', pe.data, fo + 0x14)[0:1][0], struct.unpack_from('<II', pe.data, fo + 0x18)[0]
print('export dir @0x%X: %d functions, %d names' % (exp_va, nfuncs, nnames))
aot = rva2va(struct.unpack_from('<I', pe.data, fo + 0x1C)[0])
aof = rva2va(struct.unpack_from('<I', pe.data, fo + 0x20)[0])
for i in range(nnames):
    name_rva = struct.unpack_from('<I', pe.data, pe.va2fo(aof) + 4 * i)[0]
    name = pe.read_cstr(rva2va(name_rva), 64)
    ordv = struct.unpack_from('<H', pe.data, pe.va2fo(aot) + 2 * i)[0]
    fn_rva = struct.unpack_from('<I', pe.data, pe.va2fo(rva2va(struct.unpack_from('<I', pe.data, fo + 0x1C)[0])) + 0)[0]
    # functions RVA array is at fo+0x1C target
    fns_rva = struct.unpack_from('<I', pe.data, fo + 0x1C)[0]
    frva = struct.unpack_from('<I', pe.data, pe.va2fo(rva2va(fns_rva)) + 4 * (ordv - 1))[0] if ordv >= 1 else 0
    print('  export[%d] %s -> 0x%X' % (i, name, rva2va(frva)))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
