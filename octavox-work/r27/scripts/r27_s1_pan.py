"""R27 S1: pan law — FULL closed-form reconstruction.

Dump the pan function 0x18013DD34..0x18013DEAB, its caller(s), the trig callee
0x180350400, atan2f thunk 0x180180200, the 1/sqrt2 guard helper 0x18013E070,
and the constant pool around 0x1804120A4. Static only; hash pre+post.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r27_lib import (PE, get_disassembler, rip_target, check_hash, dump,
                     annotate, load_pdata, scan_calls)
import capstone

pe = PE(); check_hash(pe)
md = get_disassembler()

# --- 1. the pan function, full ---
dump(pe, md, 0x18013DD34, 0x18013DEAB, 'PAN FN FUN_18013DD34 (full, 375 B)')

# --- 2. guard helper + constant pool ---
dump(pe, md, 0x18013E070, 0x18013E0AA, 'GUARD HELPER FUN_18013E070 (1/sqrt2 magic static)')
b = pe.read(0x1804120A4, 4)
print('const @0x1804120A4 =', repr(struct.unpack('<f', b)[0]))
b = pe.read(0x1804CCD9C, 4)
print('data static @0x1804CCD9C =', repr(struct.unpack('<f', b)[0]))

# --- 3. atan2f thunk 0x180180200 (identify CRT entry) ---
dump(pe, md, 0x180180200, 0x180180240, 'THUNK 0x180180200 (atan2f per R26)')

# --- 4. trig callee 0x180350400 — identify (sinf/cosf/sincosf?) ---
dump(pe, md, 0x180350400, 0x1803504A0, 'TRIG CALLEE 0x180350400 head')
# common CRT float thunks live adjacently; check neighbours for identity
for va, name in [(0x180351690, 'sqrtf? 0x180351690')]:
    dump(pe, md, va, va + 0x40, name)

# --- 5. callers of the pan fn ---
print('=== callers of FUN_18013DD34 ===')
for site, lo, hi in scan_calls(pe, md, 0x18013DD34):
    print('call site 0x%X inside fn 0x%X..0x%X' % (site, lo, hi))

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
