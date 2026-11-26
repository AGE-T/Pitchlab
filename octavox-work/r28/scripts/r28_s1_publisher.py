"""R28 S1: trace the publishers of the event system (U-3 residual).

R27 F7 closed the CONSUMER mechanism (event-router subscriber multicast calling
[vtable+0x10] = m02 with (module, value, aux)); the residual is which call site
fires the events that query m02 with ids 12/13 (correction target/gain).

Step 1: enumerate ALL direct callers of the publisher/dispatcher core
        (FUN_1800C27E0, FUN_1800C2960, FUN_1800C2CE0, thunks 0x1800C5730/40/50).
Step 2: for each caller, dump the payload-construction window before the call.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import (PE, check_hash, get_disassembler, disasm_range, annotate,
                     dump, load_pdata, fn_bounds, scan_calls,
                     PUBLISHER_A, PUBLISHER_B, DISPATCHER,
                     THUNK_SHARED1, THUNK_SHARED2, THUNK_SHARED3, dump_json)

pe = PE(); check_hash(pe)
md = get_disassembler()

targets = {
    'publisher_A_0x1800C27E0': PUBLISHER_A,
    'publisher_B_0x1800C2960': PUBLISHER_B,
    'dispatcher_0x1800C2CE0': DISPATCHER,
    'thunk_0x1800C5730': THUNK_SHARED1,
    'thunk_0x1800C5740': THUNK_SHARED2,
    'thunk_0x1800C5750': THUNK_SHARED3,
}

result = {}
for name, tgt in targets.items():
    hits = scan_calls(pe, md, tgt)
    result[name] = [{'call_va': hex(a), 'fn_lo': hex(lo), 'fn_hi': hex(hi)} for a, lo, hi in hits]
    print('%-26s : %d direct call site(s)' % (name, len(hits)))
    for a, lo, hi in hits:
        print('    call at 0x%X  in fn 0x%X..0x%X (size %d)' % (a, lo, hi, hi - lo))

dump_json('r28_s1_publisher_callsites.json', result)

# Step 2: for each caller of the two publishers, show the payload-construction
# window (30 instructions before the call) so the event id / value can be read.
print()
print('================ caller windows ================')
for name in ('publisher_A_0x1800C27E0', 'publisher_B_0x1800C2960'):
    print('### callers of %s' % name)
    for a, lo, hi in scan_calls(pe, md, targets[name]):
        wlo = max(lo, a - 160)
        print('--- call 0x%X in fn 0x%X ---' % (a, lo))
        insns = list(disasm_range(pe, md, wlo, a + 8))
        for i in insns[-34:]:
            print(annotate(pe, i))
        print()

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
