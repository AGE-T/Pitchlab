"""R28 S1c: find ALL clones of the event-dispatch pattern in .text.

The dispatcher core (FUN_1800C2CE0) ends with:
    mov r8, qword ptr [rsi + 8]      4C 8B 46 08
    mov rdx, qword ptr [rsi]         48 8B 16
    call qword ptr [rax + 0x10]      FF 50 10
The twin (FUN_1800C2D40) ends with:
    mov r9d, dword ptr [rsi + 0x10]  44 8B 4E 10
    mov r8, qword ptr [rsi + 8]      4C 8B 46 08
    mov rdx, qword ptr [rsi]         48 8B 16
    call qword ptr [rax + 0x18]      FF 50 18

Scan the whole image for these byte sequences (any section, any location) and
map hits to enclosing .pdata functions. This finds per-profile clones of the
event multicast machinery.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from r28_lib import PE, check_hash, load_pdata, dump_json

pe = PE(); check_hash(pe)
data = pe.data

PAT_A = bytes.fromhex('4C8B4608' + '488B16' + 'FF5010')          # slot +0x10, 2-field
PAT_B = bytes.fromhex('448B4E10' + '4C8B4608' + '488B16' + 'FF5018')  # slot +0x18, 3-field

def find_all(pat):
    out, start = [], 0
    while True:
        i = data.find(pat, start)
        if i < 0:
            return out
        out.append(i)
        start = i + 1

def fo2va(fo):
    return pe.fo2va(fo)

fns = load_pdata()
def fn_of(va):
    for lo, hi in fns:
        if lo <= va < hi:
            return lo, hi
    return None

for name, pat in (('slot+0x10 2-field dispatch', PAT_A),
                  ('slot+0x18 3-field dispatch', PAT_B)):
    fos = find_all(pat)
    print('=== %s: %d hit(s) ===' % (name, len(fos)))
    for fo in fos:
        va = fo2va(fo)
        if va is None:
            print('  file-offset 0x%X (not mapped)' % fo)
            continue
        fb = fn_of(va)
        loc = 'fn 0x%X..0x%X' % fb if fb else 'NO .pdata scope'
        print('  pattern at 0x%X  in %s' % (va, loc))
    print()

# save raw hit lists as evidence
ev = {}
for name, pat in (('patA_slot10', PAT_A), ('patB_slot18', PAT_B)):
    ev[name] = [hex(fo2va(fo)) if fo2va(fo) else None for fo in find_all(pat)]
dump_json('r28_s1c_dispatch_clones.json', ev)

pe2 = PE(); check_hash(pe2)
print('post-pass hash OK')
