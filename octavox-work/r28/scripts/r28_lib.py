"""R28 static-analysis library — extends r27_lib (same target, same rules).

Target: upload/Ox.vst3  SHA-256 7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2
All R28 analysis is STATIC (the binary is never executed). Hash checked pre+post per script.
"""
import sys, os, struct, json
sys.path.insert(0, '/home/z/my-project/octavox-work/r27/scripts')
from r27_lib import (PE, get_disassembler, disasm_range, rip_target,
                     EXPECT_SHA256, TARGET, check_hash, f32, annotate, dump,
                     load_pdata, fn_bounds, scan_calls)

R28 = '/home/z/my-project/octavox-work/r28'
TEXT_LO = 0x180001000
TEXT_HI = 0x18037D000

# R27-established event-system core (F7.1)
PUBLISHER_A   = 0x1800C27E0   # router-hierarchy publisher, vtable slot +0x10 multicast
PUBLISHER_B   = 0x1800C2960   # twin publisher, vtable slot +0x18 channel
DISPATCHER    = 0x1800C2CE0   # subscriber-array multicast, call [vtable+0x10]
THUNK_SHARED1 = 0x1800C5730
THUNK_SHARED2 = 0x1800C5740
THUNK_SHARED3 = 0x1800C5750

# Engine module (dtonicii9 wrapper / dsp_t)
ENGINE_VTABLE = 0x180409F60   # slot 2 (+0x10) = FUN_1800E1520 (m02, ids 7..13)
M02_FN        = 0x1800E1520   # getParam-style event-in getter

def dump_json(name, obj):
    path = os.path.join(R28, 'evidence', name)
    with open(path, 'w') as f:
        json.dump(obj, f, indent=1)
    return path
