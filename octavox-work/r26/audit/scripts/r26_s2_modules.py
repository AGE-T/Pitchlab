"""R26 S1 + S2: evidence integrity + fresh module extraction (mimo/miso/siso).

S1: hash the target before/after; document the R25 package absence.
S2: re-derive the full module inventories from the three kernel constructors.
"""
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, disasm_range, rip_target, EXPECT_SHA256, TARGET
import capstone

OUT = '/home/z/my-project/octavox-work/r26/audit/evidence'
os.makedirs(OUT, exist_ok=True)

pe = PE()
print('target      :', TARGET)
print('size        :', len(pe.data))
print('sha256      :', pe.sha256)
print('expected    :', EXPECT_SHA256)
print('hash match  :', pe.sha256 == EXPECT_SHA256)
print('image base  :', hex(pe.image_base))
for s in pe.sections:
    print('  sec %-8s va=0x%X vsize=0x%X raw=0x%X rsize=0x%X' % (s['name'], pe.image_base+s['va'], s['vsize'], s['ro'], s['rsize']))

# --- module extraction (approach of the proven r22 extractor, self-contained) ---
DATA_LO, DATA_HI = 0x1804A5000, 0x1804CD000
RDATA_LO, RDATA_HI = 0x18037D000, 0x1804A5000
KNOWN_CALLS = {0x1800c3990, 0x180017a00, 0x1800171b0, 0x1800c48e0, 0x180017ac0, 0x1800c6260, 0x1800c58b0}

def read_record(recva):
    fo = pe.va2fo(recva)
    if fo is None: return None
    out = []
    for i in range(400):
        v = pe.read_u64(recva + 8*i)
        if not v: break
        s = pe.read_cstr(v, 200)
        out.append(s if s is not None else ('0x%X' % v))
    return out

md = get_disassembler()

def extract(fn_lo, fn_hi):
    insns = disasm_range(pe, md, fn_lo, fn_hi)
    cur_record = cur_record_addr = cur_name = cur_name_addr = None
    cur_index = cur_slot = None
    mods = []
    for insn in insns:
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        if insn.mnemonic == 'lea' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'rdx' and ops[1].type == capstone.x86.X86_OP_MEM:
            tgt = rip_target(insn)
            if tgt is not None and DATA_LO <= tgt < DATA_HI:
                cur_record_addr = tgt
                cur_record = read_record(tgt)
            elif tgt is not None and RDATA_LO <= tgt < RDATA_HI:
                s = pe.read_cstr(tgt, 80)
                if s is not None and s.strip():
                    cur_name = s; cur_name_addr = tgt
        if insn.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'r9d' and ops[1].type == capstone.x86.X86_OP_IMM:
            cur_index = ops[1].imm
        if insn.mnemonic == 'lea' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'rcx' and ops[1].type == capstone.x86.X86_OP_MEM \
           and ops[1].mem.base != capstone.x86.X86_REG_RIP and ops[1].mem.base != 0:
            cur_slot = ops[1].mem.disp
        if insn.mnemonic == 'call' and len(ops) == 1 and ops[0].type == capstone.x86.X86_OP_IMM:
            t = ops[0].imm
            if t not in KNOWN_CALLS:
                if cur_record is not None and cur_name is not None:
                    mods.append({'index': cur_index, 'name': cur_name,
                                 'name_va': '0x%X' % cur_name_addr, 'add_fn': '0x%X' % t,
                                 'slot': ('0x%X' % cur_slot) if cur_slot is not None else None,
                                 'record_va': '0x%X' % cur_record_addr, 'record': cur_record})
                cur_record = cur_record = None; cur_record = None
                cur_name = None; cur_index = None; cur_slot = None
    return mods

kernels = {
    'mimo': (0x1800F00A0, 0x1800FA000),
    'miso': (0x180109D40, 0x180113C3A),
    'siso': (0x180122E00, 0x18012CCFA),
}
out = {}
for k, (lo, hi) in kernels.items():
    mods = extract(lo, hi)
    out[k] = mods
    inds = [m['index'] for m in mods if m['index'] is not None]
    noidx = [m['name'] for m in mods if m['index'] is None]
    print('%s: %d modules, indices %d..%d (%d with index), %d index-less' %
          (k, len(mods), min(inds), max(inds), len(inds), len(noidx)))
    print('  index-less names:', ', '.join(noidx[:24]))

json.dump(out, open(OUT + '/r26_module_extraction.json', 'w'), indent=1)
print('written', OUT + '/r26_module_extraction.json')

# post-extraction hash re-check (S1: analysis must not modify the target)
pe2 = PE()
print('post-analysis sha256 match:', pe2.sha256 == EXPECT_SHA256)
