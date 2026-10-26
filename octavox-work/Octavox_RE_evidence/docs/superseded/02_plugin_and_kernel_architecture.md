# 02 — Plugin and Kernel Architecture (Canonical)

**Canonical document.** This is the authoritative description of the host-facing architecture and the kernel/module execution model. Other documents reference it and must not contradict it.

All addresses are VA (ImageBase `0x180000000`). See doc 01 for the binary hash and tooling.

---

## 1. Host-facing layer (VST3 → JUCE → tide)

### 1.1 Entry points

| Export | Address | Role |
|---|---|---|
| `GetPluginFactory` | `0x180194440` | VST3 factory entry (standard VST3 module entry) [OBS: export table] |
| `InitDll` / `ExitDll` | `0x180194B80` / `0x180194B70` | VST3 module init/exit [OBS: export table] |

### 1.2 `VoxAudioProcessor` (host adapter, derived from JUCE audio processor + tide base)

Evidence: decompile `dec_factory_user_1801A7620.json` of `FUN_1801A7620`.

- The constructor installs **five vtable pointers** into the object (multiple inheritance): at object offsets `+0x0`, `+0x150`, `+0x158`, `+0x2600`, `+0x2810` (param indices `[0x2a]`, `[0x2b]`, `[0x4c0]`, `[0x502]` in the decompile). [OBS]
- It embeds an **AAX chunk-data parser** object (`AAX_CChunkDataParser::vftable` written at `+0x2868`). [OBS]
- It creates a `std::list` sentinel (`[lVar4]=lVar4; [lVar4+8]=lVar4`) stored at `param_1[0x506]`. [OBS]
- Early initialization calls: `FUN_1803170B0(param_1, 1)` (tide/JUCE base init), `FUN_1801A0C40(param_1)` (processor init/reset helper — **not** a process callback despite living at a "process-like" address), `FUN_18023FD90(param_1+0x41, param_1+0x502)`. [OBS]
- One virtual call on the object at `[[param_1+0x45D]+0x378]` via slot `+0x28` with argument `0` under a critical section. [OBS]
- **Factory registration:** the constructor reads the address `0x1804B6E08` (the global `OctavoxFactory` singleton vtable slot, see §1.3) at `0x1801A779E` and passes it into `FUN_180319510` (tide `ProcessorKernel` registry). [OBS: `refto 0x1804B6E08` = exactly two sites: `0x1801A779E` and `0x18030C8F7`]
  - [DER] `FUN_180319510` registers the plug-in's kernel factory with the tide base so that the correct kernel can be instantiated when the plug-in is prepared with its real channel counts.

### 1.3 `OctavoxFactory` (the kernel factory singleton)

| Item | Value | Evidence |
|---|---|---|
| Singleton vtable slot (`.data`) | `0x1804B6E08` | `refto` scan; decompile of `0x1801A7620` |
| Magic-static accessor | `FUN_18030C8D0` (`__Init_thread_header`-guarded, `atexit` handler `DAT_18037AD10`) | `dec_factory_user_18030C8D0.json` |
| Vtable contents | `[00]` destructor `0x18030C660`; `[01]` `create` = `0x18030C6A0` (the kernel selector) | RTTI/vtable parse + selector decompile |

### 1.4 Kernel selection (`create(in, out)`)

Evidence: decompile `dec_kernel_selector_18030C6A0.json` of `FUN_18030C6A0(param_1, param_2, in, out)`.

| (in, out) | Class (RTTI) | Ctor | Alloc size | VT | Modules |
|---|---|---|---|---|---|
| (1, 1) | `octavox_mimo_t` | `0x1800F00A0` (writes VT `0x180409B90` at `[obj+0]`; see `0x1800F00DC`) | `0x20C30` | `0x180409B90` | 395 |
| (1, 2) | `octavox_miso_t` | `0x180109D40` | `0x20B10` | `0x18040A238` | 394 |
| (2, 2) | `octavox_siso_t` | `0x180122E00` | `0x20B10` | `0x18040A020` | 394 |
| any other | `NullAlgorithm_t` | inline in `0x18030C6A0` | `0x1F8` | (`NullAlgorithm_t::vftable`) | 0 (silent) |

- Allocation uses `FUN_18015E63C` (operator new). [OBS]
- The `NullAlgorithm_t` fallback path also builds two module-record vectors from `PTR_s_ninputs_16…` @ `0x1804AF5B0` and `DAT_1804CC890`. [OBS]
- [OBS] `octavox_siso_t` and `octavox_miso_t` have **identical module inventories** (394 modules each; only the default input-channel count differs, 2 vs 1). `octavox_mimo_t` differs by one module (`iswitch9`) and by the routing of `multiply2.in1`. See doc 03 §2.
- [DER] All three kernels therefore instantiate the **same processing graph** (one pitch-shifting design), with different I/O routing variants for mono/stereo operation.

## 2. Kernel objects

### 2.1 Object size and layout

- Object size 128–132 KB (`0x20B10` / `0x20C30`), containing all module instances by fixed offsets. [OBS: alloc sizes + slot offsets]
- **Voice blocks:** each of the 8 voices occupies a contiguous `0xB70`-byte block; voice *N* block base = `voice1_base + (N-1)*0xB70`. Voice 1 block begins with `modfilter8` at `+0x16980`. [OBS: slot arithmetic in `method05_annotated.txt`]
- Voice-1 block internal layout (voice-local offsets):

| Module | Slot (voice 1) | Δ from previous |
|---|---|---|
| `modfilter8` | `0x16980` | — |
| `eqn` | `0x16C28` | `0x2A8` |
| `add` | `0x16F70` | `0x348` |
| `moddelay7` | `0x16FE0` | `0x70` |
| `c_multiply8` | `0x17228` | `0x248` |
| `c_bound2` | `0x172E8` | `0xC0` |
| `c_fltr_a23` | `0x173B0` | `0xC8` |
| *(next voice)* | `0x174F0` | `0x140` |

- Global module slots live outside the voice region (e.g., control modules at `0x160`–`0x8600`, meters at `0x1F588`–`0x20A58`, engine `dtonicii9` at `0xBFB8`). [OBS]

### 2.2 Kernel lifecycle methods (vtable slots, `octavox_siso_t` values)

| Slot | Address | Size | Semantics | Key evidence |
|---|---|---|---|---|
| `[00]` | `0x180120C20` → `0x180120C60` | ~8.6 KB | Destructor: 8× voice release `0x1800E9DC0`, engine destroy via `0x1800E7DF0`, module teardown | [OBS] dump |
| `[01]` | `0x18011D5F0` | 6,174 B | **Prepare pass**: per-module buffer allocation. Calls per-module prepare at embedded state sub-objects (e.g., modfilter state at `slot+0x108` → `0x1800E8B50`; eqn state at `slot+0xB8` → `0x1800EA650`; moddelay state at `slot+0xB8` → `0x1800D38B0`), 8 voices each; engine prepare `0x1800E2030` on the `dsp_t` at `kernel+0xC3E8` (call `0x18011DFBB`; worker granularity = 4 samples per voice, batch 2); global modules prepared once | `method01_annotated.txt` |
| `[02]` | `0x18012CD00` (siso; miso `0x180113C40`) | 39,333 B | **Configure pass** (one-time): for each of ~394 modules, build the record vector from the static `.data` record table via `0x1800C3990`, call the module's config wrapper (e.g., `FUN_180137DF0` dtonicii9, `FUN_180137C00` modfilter, `FUN_180137A90` eqn, `FUN_180137900` moddelay, `thunk_FUN_1800EC120` c_bound, `FUN_180136F90` add), destroy vector via `0x1800C48E0` | `dec_siso_02.json` |
| `[03]` | `0x18011EE10` | 3,906 B | **State-clear pass**: `memset` (`0x161700`) of module state buffers + module reset wrappers: `dtonicii9`→`0x180137D40`, `eqn..eqn7`→`0x1801379F0` (×8), `delay_xf1/delay_xf`→`0x180137560` (×2) | `method03_annotated.txt` |
| `[04]` | `0x180003B30` | 1 B (`ret 0`) | Shared stub (interface no-op) | [OBS] |
| `[05]` | `0x18011FD60` | 3,762 B | **Per-block process**. Signature `process(this, dl_flag)`. Begins with the parameter-change broadcast (§2.3), then executes the whole graph in topological order (§doc 04); ends with a tail-call to the `tonM` meter | `method05_annotated.txt` + disasm |

- `[01]` and `[05]` use **disjoint call sets** (prepare vs process) — do not conflate them. [OBS]
- The `dl` flag of `[05]` (`movzx edi, dl` at `0x18011FD6D`) is forwarded to selected module processes (e.g., `modfilter`, `eqn`, `moddelay`, `c_bound`, `modiirq`, `softclip`, `dtonicii9`) via `movzx edx, dil`. [OBS] **Resolved (batch 2)**: `dl ≠ 0` = first block of a run → the engine re-publishes its sample-rate object (id 5); it is not a declick selector (doc 09 B11).
- The `dl` flag of the engine's process (`0x1800E5160`) internally triggers the first-block sub-path (`test dl,dl` → owner publish calls) = the sample-rate **object re-publish (id 5)**; not a DSP re-prepare. [OBS in `0x1800E5160` head; batch 2]

### 2.3 Parameter-change notification (kernel-level broadcast)

Head of `[05]`, `0x18011FD77`–`0x18011FDCA`:

```
add rcx, 0x360 ; call 0x18013AD30          ; c_subtract1 (mono-detect: inch-1)
movss xmm3,[rbx+0x4D0] ; ucomiss vs [rbx+0x4B0]
if changed:
    [rbx+0x4B0] = xmm3                     ; latch new value
    for (entry = [rbx+0x4B8] ; entry != [rbx+0x4C0] ; entry += 0x10)
        *((uint64*)entry[0]) |= 1ull << entry[8]   ; lock or  → target flag word
```

- `[rbx+0x4B8]…[0x4C0]` is a vector of `{qword target; dword bit_index}` entries (16 bytes each). [OBS]
- Broadcast loop instructions at `0x18011FDB0`–`0x18011FDCA`: `mov ecx,[r8+8]; mov edx,1; mov rax,[r8]; shl rdx,cl; lock or [rax],rdx; add r8,0x10`. [OBS]
- [DER] This is the **control→audio notification bus**: when a parameter changes, registered modules get a bit set in a state word; module processes (e.g., the engine) test those bits during the block and perform the corresponding re-read/recalc.
- Confirmed consumer pattern: `dtonicii9` module object tests bits at `[obj+0x3B8]` (2 separate tests in `0x1800E5160` head); the object holds six `−1`-initialized qwords at `+0x3B8..+0x3E0` (candidate: per-output subscriber wake words; doc 05 §4). [OBS]/[UNK]

## 3. Module framework (modpp)

### 3.1 Records

- Each module instance is described by a **null-terminated array of `key=value` C strings** (a "record") in `.data` (`0x1804A5000`–`0x1804CD000`). [OBS: `kernel_modules.json` extraction; record tables consumed in `[02]` via `0x1800C3990`/`0x1800C48E0`]
- Typical keys: `in`, `in1..inN`, `select`, `statement` (UI label), `min`, `max`, `default`, `res`, `nchoices`, `choices1..N`, `nentries`, `maxdelay`, `time_constant`, `processoff`, etc. [OBS]
- The `statement=` values carry printf-style UI labels used by both the plug-in UI and (via the same records) the parameter surface (see doc 08). [OBS]

### 3.2 Module instantiation pattern

- A module type has a **config/factory wrapper** (called from kernel `[02]`) that (re)sets the instance from its record. Examples (from `dec_siso_02.json` and call annotation): `dtonicii9`→`FUN_180137DF0`/factory `FUN_180137E20`, `modfilter`→`0x180137C00`/`0x180137C30`, `eqn`→`0x180137A90`/`0x180137AC0`, `moddelay`→`0x180137900`/`0x180137930`, `add`→`0x180137F40`-family, `multiply`→`0x1801390D0`-family. [OBS]
- Some module instances embed a **state sub-object** at a fixed offset (found by prepare-pass offsets): modfilter state `+0x108`, eqn state `+0xB8`, moddelay state `+0xB8`, `dtonicii9` engine object `dsp_t` `+0x430`. [OBS: `method01_annotated.txt`]
- The `dtonicii9` instance is the composite pitch engine: module wrapper at kernel `+0xBFB8` (index `0x113`=275), `dsp_t` engine at `+0xC3E8`. [OBS: `dec_dsp_factory_180137E20.json`]

### 3.3 Execution model in `[05]`

- The process pass calls module processes **in topological (signal-flow) order**, devirtualized with explicit `lea rcx,[rbx+slot]` + `call target`. [OBS: `method05_annotated.txt`, 284 call sites]
- **Not every module appears as a call site.** Some modules are **inlined** into the kernel method: e.g., at `0x18010442A`–`0x180104468` (mimo kernel method) an inline block reads fields of `c_fltr_a23` (`+0x17448`, `+0x17460`, `+0x17478`) and writes its output through the stored pointer at `+0x17440`. [OBS]
- Modules `add` and `multiply4` (voice-1 loop members) have **no process call sites** (byte-scan for `lea rcx,[rbx+0x16F70]`: only `[01]` prepare at `0x18011DFF2` and the mimo prepare at `0x180104412`; `lea rcx,[rbx+0xB5C0]`: zero hits). [OBS] Their contribution is computed inside adjacent module processing (e.g., the delay module's process or inline code). [DER]
- Therefore: **use the call list as evidence of execution order, and the record tables as evidence of wiring** — together they define the graph.

## 4. Timed/threading model

- The engine (`dtonicii9`) exposes atomic state words tested during `[05]` (e.g., bits at `[obj+0x3B8]`), consistent with a control thread publishing changes and the audio thread consuming them. [OBS]
- `dsp_t` registers typed **shared objects** with the owner (IDs 18–30) — the published state used by the UI/parameters (doc 05 §5). [OBS]
- Status broadcasts use virtual call `[rax+0x60]` with `(index, value)` shape; values are converted dB→linear via `10^(x/20)`-style math (`ln(10)/20` constant `0x180412250` = `0.23025850929`). [OBS: `0x1800E7960`]
- No locking inside the per-sample path was observed; flag-based publication is used instead. [DER]

## 5. What the kernels are *not*

- `[02]` is a **configure** pass, not an audio process pass; `[01]`, `[03]`, `[05]` are distinct roles (prepare / clear / process). [OBS]
- Kernel objects contain no audio buffers *of the host*; audio enters through the kernel's I/O modules (`left in`/`right in`, `left out`/`right out`) inside `[05]`. [OBS: `method05_annotated.txt` steps 163–256]
- Pitch shifting is **not** performed by the graph's delay modules: the per-voice `moddelay` instances are wired as *static echoes* (`mod=adc-null`, `modamt=0`, `glidespeed=0`). All pitch shifting is inside `dtonicii9` (doc 05). [OBS: records; see doc 04 §3]
