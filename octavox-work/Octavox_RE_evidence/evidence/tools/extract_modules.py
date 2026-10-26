"""Extract the full module list from octavox kernel constructors.

Pattern per module:
  lea rdx,[.data record]  ; key=value string pointer array
  call 0x1800c3990        ; build vector<string>
  lea rdx,["name"]        ; module instance name
  call 0x180017a00        ; std::string ctor
  mov [rsp+0x20], rbx
  mov r9d, INDEX
  lea r8, [name]
  mov rdx, [r14+8]
  lea rcx, [r14+SLOT]
  call HELPER             ; module-type-specific add
"""
import sys
sys.path.insert(0, r'C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis')
import capstone, struct, json
from va_scan import va2fo, data, read_cstr, IMAGE_BASE

DATA_LO, DATA_HI = 0x1804A5000, 0x1804CD000
RDATA_LO, RDATA_HI = 0x18037D000, 0x1804A5000

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
md.skipdata = True

KNOWN_CALLS = {0x1800c3990, 0x180017a00, 0x1800171b0, 0x1800c48e0, 0x180017ac0, 0x1800c6260, 0x1800c58b0}

def read_record(recva):
    fo = va2fo(recva)
    if fo is None:
        return None
    out = []
    for i in range(400):
        v = struct.unpack_from('<Q', data, fo + i * 8)[0]
        if v == 0:
            break
        s = read_cstr(v, 200)
        out.append(s if s is not None else ('0x%X' % v))
    return out

def extract(fn_lo, fn_hi, label):
    fo = va2fo(fn_lo)
    code = data[fo:fo + (fn_hi - fn_lo)]
    cur_record = None
    cur_record_addr = None
    cur_name = None
    cur_name_addr = None
    cur_index = None
    cur_slot = None
    mods = []
    other_calls = []
    for insn in md.disasm(code, fn_lo):
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        # record lea: rdx, [rip + T]
        if insn.mnemonic == 'lea' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'rdx' and ops[1].type == capstone.x86.X86_OP_MEM \
           and ops[1].mem.base == capstone.x86.X86_REG_RIP:
            tgt = insn.address + insn.size + ops[1].mem.disp
            if DATA_LO <= tgt < DATA_HI:
                cur_record_addr = tgt
                cur_record = read_record(tgt)
            elif RDATA_LO <= tgt < RDATA_HI:
                s = read_cstr(tgt, 80)
                if s is not None and s.strip():
                    cur_name = s
                    cur_name_addr = tgt
        # index: mov r9d, imm
        if insn.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'r9d' and ops[1].type == capstone.x86.X86_OP_IMM:
            cur_index = ops[1].imm
        # slot: lea rcx, [r14 + X]
        if insn.mnemonic == 'lea' and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_REG \
           and insn.reg_name(ops[0].reg) == 'rcx' and ops[1].type == capstone.x86.X86_OP_MEM \
           and ops[1].mem.base != capstone.x86.X86_REG_RIP and ops[1].mem.base != 0:
            cur_slot = ops[1].mem.disp
        # call
        if insn.mnemonic == 'call' and len(ops) == 1 and ops[0].type == capstone.x86.X86_OP_IMM:
            t = ops[0].imm
            if t not in KNOWN_CALLS:
                if cur_record is not None and cur_name is not None:
                    mods.append({'index': cur_index, 'name': cur_name, 'name_va': '0x%X' % cur_name_addr,
                                 'add_fn': '0x%X' % t, 'slot': ('0x%X' % cur_slot) if cur_slot is not None else None,
                                 'record_va': '0x%X' % cur_record_addr, 'record': cur_record})
                cur_record = None
                cur_name = None
                cur_index = None
                cur_slot = None
                other_calls.append('0x%X' % t)
                # resync guard: if call not an add-helper, the pattern state is stale; keep anyway
    return mods, other_calls

if __name__ == '__main__':
    kernels = {
        'mimo': (0x1800F00A0, 0x1800FA000),
        'miso': (0x180109D40, 0x180113C3A),
        'siso': (0x180122E00, 0x18012CCFA),
    }
    out = {}
    for k, (lo, hi) in kernels.items():
        mods, others = extract(lo, hi, k)
        out[k] = mods
        print('%s: %d modules extracted' % (k, len(mods)))
        inds = [m['index'] for m in mods if m['index'] is not None]
        print('  indices: %d..%d (%d)' % (min(inds), max(inds), len(inds)) if inds else '  no indices')
    json.dump(out, open(r'C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\kernel_modules.json', 'w', encoding='utf-8'), indent=1)
    with open(r'C:\Users\Tomi\Documents\Default Project\OX_RESEARCH\analysis\kernel_modules.txt', 'w', encoding='utf-8') as f:
        for k, mods in out.items():
            f.write('=== %s (%d modules)\n' % (k, len(mods)))
            for m in mods:
                f.write('  [%s] %-24s add=%s slot=%s rec=%s\n' % (m['index'], m['name'], m['add_fn'], m['slot'], m['record_va']))
                if m['record']:
                    f.write('        rec: %s\n' % '; '.join(m['record'][:24]))
    print('written kernel_modules.txt/json')
