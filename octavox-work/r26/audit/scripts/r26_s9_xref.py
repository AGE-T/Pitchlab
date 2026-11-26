"""R26 S9: whole-.text cross-reference scan (memory-light, chunked).

Only tracks a fixed WANTED target set; every R26 claim about "who references X"
is machine-verifiable in THIS round from the raw binary.
"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r26_lib import PE, get_disassembler, rip_target, EXPECT_SHA256
import capstone

OUT = '/home/z/my-project/octavox-work/r26/audit/evidence'
pe = PE()
assert pe.sha256 == EXPECT_SHA256
md = get_disassembler()

TEXT_LO, TEXT_HI = 0x180001000, 0x18037D000
WANTED = {
    0x1803B5BF0: 'latency_str_LatencySamples',
    0x180310F20: 'dead_serializer_FUN_180310F20',
    0x1800E1520: 'm02_FUN_1800E1520',
    0x1800E10B0: 'm01_FUN_1800E10B0',
    0x180409F60: 'dtonicii9_vtable',
    0x180409F20: 'dsp_shared_vtable',
    0x1800E0360: 'engine_ctor_FUN_1800E0360',
    0x1800E2030: 'engine_prepare_FUN_1800E2030',
    0x18013BBC0: 'tracker_FUN_18013BBC0',
    0x1800E5160: 'update_FUN_1800E5160',
    0x1803B6170: 'kernel_table_256x6',
    0x180137E20: 'dtonicii9_factory_FUN_180137E20',
    0x1800E0190: 'record_writer_FUN_1800E0190',
    0x1800DFE80: 'state7_FUN_1800DFE80',
    0x1800E2C00: 'param_ctor_FUN_1800E2C00',
    0x18030C6A0: 'profile_selector_FUN_18030C6A0',
    0x1803F05D0: 'voxaudio_vtable',
    0x180137720: 'stereomixer3_composite',
    0x1800D7400: 'mix_sub_ctor',
    0x1800D8230: 'clip_sub_ctor',
    0x1800D4710: 'moddelay_publisher',
    0x180139B40: 'rdm_noise_FUN_180139B40',
    0x1800E7DF0: 'engine_dtor_FUN_1800E7DF0',
    0x1803BB690: 'temperament_table',
    0x1803BB780: 'scale_pattern_table',
    0x1803BB610: 'keymap_table',
    0x1803BB60C: 'userscale_map',
    0x18038EB88: 'juce_param_table_start',
    0x1801A0CFD: 'juce_param_ctor_region',
    0x1800E18B0: 'record_initializer_FUN_1800E18B0',
    0x1800E3350: 'pitch_config_initializer_FUN_1800E3350',
    0x1800E7960: 'gatelevel_applier_FUN_1800E7960',
    0x1800E7B80: 'period_law_FUN_1800E7B80',
    0x1800E8B50: 'per_voice_SVF_FUN_1800E8B50',
    0x18013E160: 'parabolic_refine_FUN_18013E160',
    0x1800E0D5B: 'engine_ctor_store_site',
    0x1800DC430: 'filter_reader_process',
    0x1800DBEF0: 'kernel_binder_FUN_1800DBEF0',
    0x1800DBAD0: 'reader_mode_flag_ctor',
}

found = {v: [] for v in WANTED.values()}
n = 0
CH = 0x40000  # 256 KB chunks
fo = pe.va2fo(TEXT_LO)
base = TEXT_LO
while base < TEXT_HI:
    end = min(base + CH, TEXT_HI)
    chunk = pe.data[pe.va2fo(base):pe.va2fo(end)]
    for insn in md.disasm(chunk, base):
        n += 1
        try:
            ops = insn.operands
        except capstone.CsError:
            continue
        if not ops:
            continue
        if insn.mnemonic in ('call', 'jmp') and ops[0].type == capstone.x86.X86_OP_IMM:
            lbl = WANTED.get(ops[0].imm)
            if lbl:
                found[lbl].append((hex(insn.address), insn.mnemonic))
        else:
            t = rip_target(insn)
            if t is not None:
                lbl = WANTED.get(t)
                if lbl:
                    found[lbl].append((hex(insn.address), 'mem:' + insn.mnemonic))
    base = end
    print('scanned up to 0x%X (%d insns)' % (base, n), flush=True)

print('TOTAL instructions scanned:', n)
res = {lbl: {'refs': refs, 'count': len(refs)} for lbl, refs in found.items()}
for lbl in sorted(res):
    print('%-42s %5d refs' % (lbl, res[lbl]['count']))
    for a, k in res[lbl]['refs'][:10]:
        print('    %s %s' % (a, k))

# stored absolute pointers (vtables/COL) in data sections for wanted targets
print('\nabsolute stored pointers:')
sections = [s for s in pe.sections if s['name'] in ('.data', '.rdata', '_RDATA', '.pdata')]
for va, lbl in WANTED.items():
    pat = struct.pack('<Q', va)
    hits = []
    for s in sections:
        blob = pe.data[s['ro']:s['ro'] + s['rsize']]
        off = 0
        while True:
            i = blob.find(pat, off)
            if i < 0: break
            hits.append(hex(pe.image_base + s['va'] + i))
            off = i + 1
    if hits:
        print('  %-42s -> %s' % (lbl, hits[:8]))

json.dump(res, open(OUT + '/r26_xrefs.json', 'w'), indent=1)
print('written r26_xrefs.json; final hash:', pe.sha256 == EXPECT_SHA256)
