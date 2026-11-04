# 09 — Evidence Matrix

Consolidated claim→evidence table for the whole analysis set. Status: **OBS** = directly observed in binary/artifact; **DER** = derived interpretation; **UNK** = unresolved; **CONFLICT** = conflicting evidence.

Evidence artifacts referenced: `dec_*.json` / `dec2_*.json` (Ghidra decompiles, doc 01 §3), `kernel_modules.json` (records), `method01/03/05_annotated.txt` (call annotation), `rtti_vtables*.txt`, `functions.csv`, targeted disassembly (addresses cited). Batch-2 normalized dumps: `dec2_pitch_process_1800E5160.txt`, `dec2_pitch_config_1800E3350.txt`, `dec2_base_ctor_1800E2C00.txt`, `dec2_dsp_prepare_1800E2030.txt`, `dec2_fn_1800E18B0.txt`, `table_dump.txt`. All addresses are VA.

---

## A. Target and identification

| # | Claim | Evidence | Status |
|---|---|---|---|
| A1 | Target is a PE32+ x64 VST3 DLL named `Octavox.vst3`, ImageBase `0x180000000`, 5,307,392 bytes, TIMESTAMP 2023-03-31 | Export table; PE header parse; SHA-256 `7BB260F6…F32F1F2` (doc 01 §1) | OBS |
| A2 | Product is Eventide Octavox | Export module name; Eventide strings; class names `octavox_*` in RTTI | OBS |
| A3 | Built on JUCE 6.1.3 | String `"JUCE v6.1.3"` @ `0x38D730` | OBS |
| A4 | Uses Eventide `tide` + `modpp` module framework | `TideOctW`/`.tide` strings; key=value module record system; module index/slot registration virtuals | OBS/DER |
| A5 | No user symbols; class names from manual MSVC RTTI parsing | `rtti_vtables.json`; `.pdata` function table `functions.csv` | OBS |

## B. Plugin layer and kernels

| # | Claim | Evidence | Status |
|---|---|---|---|
| B1 | `GetPluginFactory` @ `0x180194440`; `InitDll`/`ExitDll` @ `0x180194B80`/`0x180194B70` | Export directory | OBS |
| B2 | `VoxAudioProcessor` ctor `0x1801A7620` installs 5 vtables (`+0, +0x150, +0x158, +0x2600, +0x2810`), embeds AAX chunk parser at `+0x2868`, `std::list` at `[+0x506]` | `dec_factory_user_1801A7620.json` | OBS |
| B3 | Factory singleton slot at `.data 0x1804B6E08`; referenced only from ctor (`0x1801A779E`) and magic-static accessor `0x18030C8D0` (`0x18030C8F7`) | `refto` scan (2 sites) | OBS |
| B4 | Kernel selector `0x18030C6A0`: (1,1)→`octavox_mimo_t` ctor `0x1800F00A0` size `0x20C30`; (1,2)→`octavox_miso_t` ctor `0x180109D40` size `0x20B10`; (2,2)→`octavox_siso_t` ctor `0x180122E00` size `0x20B10`; else `NullAlgorithm_t` (0x1F8) | `dec_kernel_selector_18030C6A0.json`; vtable write at `0x1800F00DC` (`lea rax,[rip+…]`→`0x180409B90` = `.?AUoctavox_mimo_t`) | OBS |
| B5 | `octavox_siso_t` method slots: `[00]` dtor `0x180120C60` (8,604 B), `[01]` prepare `0x18011D5F0` (6,174 B), `[02]` configure `0x18012CD00` (39,333 B), `[03]` reset `0x18011EE10` (3,906 B), `[04]` stub `0x180003B30`, `[05]` process `0x18011FD60` (3,762 B) | vtable parse; `functions.csv` sizes | OBS |
| B6 | `[02]` builds per-module record vectors via `0x1800C3990`/`0x1800C48E0` and calls module config wrappers (e.g., `dtonicii9`→`0x180137DF0`, `eqn`→`0x180137A90`, `moddelay`→`0x180137900`, `c_bound`→`thunk_FUN_1800EC120`, `add`→`0x180136F90`) | `dec_siso_02.json` (298,351 B) | OBS |
| B7 | `[03]` = state reset: `memset` (`0x161700`) over module state buffers + reset wrappers `0x180137D40` (dtonicii9), `0x1801379F0` (eqn ×8), `0x180137560` (delay_xf ×2) | `method03_annotated.txt`; disasm at `0x18011F429`… | OBS |
| B8 | `[05]` begins with parameter-change broadcast: compares `[kernel+0x4D0]` to latch `[kernel+0x4B0]`, then `lock or` bit `entry[8]` into target `entry[0]` for 16-byte entries of vector at `[kernel+0x4B8]…[0x4C0]` | disasm `0x18011FD7C`–`0x18011FDCA` | OBS |
| B9 | `[05]` tail-calls `tonM` (`jmp 0x1801366B0` with `rcx = kernel+0x20A58`) after `pitM` | disasm `0x180120C03` | OBS |
| B10 | Voice blocks: stride `0xB70`; voice 1 starts `modfilter8` @ `0x16980`; layout modfilter(+0) / eqn(+0x2A8) / add(+0x5F0) / moddelay(+0x660) / c_multiply(+0x8A8) / c_bound(+0x968) / c_fltr_a(+0xA30) | slot table in `method05_annotated.txt` | OBS |
| B11 | `dl` flag argument of `[05]`/module processes: **resolved** — `dl ≠ 0` = first block of a run → engine process re-publishes the sample-rate object (id 5); not a declick selector | `movzx edi,dl` @ `0x18011FD6D`; `dec2_pitch_process_1800E5160.txt` | OBS |

## C. Graph and modules

| # | Claim | Evidence | Status |
|---|---|---|---|
| C1 | Kernel module inventories: siso 394, miso 394, mimo 395 modules; only name difference `iswitch9` (mimo) | `kernel_modules.json` | OBS |
| C2 | `iswitch9`/`iswitch11` select `monoSumWetChans` vs `feedback1`/`feedback` on `c_subtract3` (=`ouch−1`) | records [361/362] | OBS |
| C3 | Wiring is name-based (`"module-out"` references) | records | OBS |
| C4 | `dtonicii9` at slot `0xBFB8`, index 275 (`0x113`); wrapper + engine `dsp_t` at `+0x430`; 69-entry record | `dec_dsp_factory_180137E20.json`; records | OBS |
| C5 | Engine record fields and per-voice wiring (`shiftN`, `pcentN`, `modN`, `modamtN`, `delayamtN`, `userscale*`=0, `maxdelay=2400`, `nvoices=8`, `steps=12`, `pitchtracking`, `hysteresis=10`, `correctrate=1`, `mincorrect=0`, `intervalglide=1`, `tuning=0`, `quantization=0`, `minpitch`, `gatelevel=−55`, `xfadetime`, `scale`, `key`, `tune`) | records | OBS |
| C6 | `add` and `multiply4..11` (voice loop) and `add8`, `multiply..multiply3` (wet/dry) have no `[05]` call sites; no `[05]` memory access to their slots | byte-scan + disasm | OBS |
| C7 | `[01]` primes 4-sample runs for smoother/multiply state (e.g., `0x18011DE90`–`0x18011DF27`: 4 sequential smoother updates + 4-sample multiply into stored output pointer) | disasm | OBS |
| C8 | Internal quantum is 4 samples (SSE) | C7 + `0x1800E2030` 4-float tuples | DER |
| C9 | Per-voice echo loop semantics: `add = eqn + 10^(fbk dB/20)·echo`, `moddelay` = `dlyN` ms delay | records + C6 | OBS (wiring) / DER (fold site) |
| C10 | `modiirq` = 2-section IIR filter (q=0.7) in engine feed; cutoff controlled by max shift via `c_minmax2→÷7→×−1→2^x→smooth→bound1(0..1)` | records [178–187] | OBS |
| C11 | AA instrument tables verified; `inst` selects them (0..12 over 16-entry tables) | records [259–262] | OBS |
| C12 | Far feedback: softclip → `delay_xf` (`tap=max(fbkd,50) ms`, `xfadetime=20`) → 1-smp delay + 5 Hz HP + subtraction (DC block) → `feedback`/`feedback1` with `mfbk` | records [339–350] | OBS |
| C13 | Wet/dry: `c_subtract2`(1−`mmix`) / `c_fltr_a*` (tc=10) into 2 dry + 2 wet multiplies summed by `LWetDryMix`/`RWetDryMix` → outputs with `ogan` | records [351–366] | OBS |
| C14 | `add8` output has no consumer in any kernel | records (0 hits for `add8-out`) | OBS+UNK |
| C15 | `rdm` system: noise→abs→S/H→slew (limits from neighbor voice ×2000)→comp→a-c→÷1.5→+0.5→×2000 (cross-coupled 8→1) | records [183]–[245] | OBS |
| C16 | Engine record→parameter transport: 13 binding blocks (`+0x10..+0x27F`, stride `0x30`) `{min,max,def,notifyTarget,notifyBit,valuePtr,cached}`; key map incl. `scale`@`+0x10`, `key`@`+0x40`, `quantization`@`+0x190`, `minpitch`@`+0x1C0` (special accessor registration); per-voice keys (`shift`/`pcent`/`modamt`/`delayamt`/`userscale`) use the same block factory | `dec2_pitch_config_1800E3350.txt`; base-ctor disasm | OBS |
| C17 | Six named engine outputs registered at `+0x2F8` (`pitch`), `+0x318` (`period`), `+0x338` (`freq`), `+0x358` (`amp`), `+0x378` (`tonality`), `+0x398` (`timbre`); each `{f32,pad,vector}` (`0x20` B) with subscriber entries `{qword flagWordPtr, u32 bit}` (16 B) woken via `LOCK; *flag |= 1<<bit` | `dec2_base_ctor_1800E2C00.txt`; instruction-verified disasm (`lea r9` sites / indirect calls) | OBS |

## D. Engine interface (dsp_t)

| # | Claim | Evidence | Status |
|---|---|---|---|
| D1 | `dsp_t` ctor `0x1800E0360` parses only `maxdelay=2400` (+0x50) and `nvoices=8` (+0x54); name at +0x30; analysis sizes per SR: 4096 (<50 kHz), 8192 (<100 kHz), 16384 (else) at +0x409C; per-voice work buffers vector at +0x40A0 (mask +0x40B8 = size−1); delay ring +0x41A0 sized `sr·0.001·maxdelay` (115,200 floats @48k = 2.4 s); +index/+mask +0x41B8/+0x41C0 | `dec_dsp_ctor_1800E0360.json` (43,660 B) | OBS |
| D2 | `dsp_t` registers shared objects: ID 3 (float[3] @ `+0x41D8`) and IDs `0x12–0x1E` (per-voice arrays `nvoices×0x2C` @ `+0x4270`/`+0x4298`, float[16] @ `+0x4310`, float[72] @ `+0x4360`, int[12] @ `+0x4490`, scalar shared_t @ `+0x44D0`/`+0x44E8`/`+0x4500`; id 0x13 inits to `−1.0`); registers outputs `out1..out8` (vector `+0x80`, stride `0x18`); self-registers under index 275 (`0x113`); queries owner virtuals `[+8]`, `[+0x18]`, `[+0x30]`, `[+0x60]` | `dec_dsp_ctor_1800E0360.json`; `dec2_*` dumps | OBS |
| D3 | `dsp_t[01]` = setparam: 0→`+0xA484`; 1→`+0xA488` (+`+0xA48C=(int)x+2`); 2→`+0xA4E8` (=gatelevel dB); 4→`+0xA440`; 5 = sample-rate class setup building **decimation lowpass banks** (11/21/41 taps → `+0xA3C8..+0xA41B`; `half−1` @ `+0xA41C` = 5/10/20; count @ `+0xA420` = 6/11/21; factor 2/4/8 @ `+0xA42C`); 6 = stores `0x12` (`+0xA448`), `(int)x·0xF3` (`+0xA450`), `1/x` (`+0xA44C`); 14→`+0xA558`; 15 = zeroes 8 B at `+0xA57C`; 16 = `2^(±x/1200)` (`exp(±x·0.00057762265)`, `+0xA564`/`+0xA568`); 17 = `2^(±1/(12x))` (`exp(±0.057762265/x)`, `+0xA55C`/`+0xA560`); default forwards to owner `[vtable+0x18]` | `dec_dsp_m01_1800E10B0.json` (+ line-verified byte offsets) | OBS |
| D4 | `dsp_t[02]` = getparam: 7 = cents `1731.234·ln((sr·2048/period)/261.6256)` (sentinel −65535); 8 = `sr·2048/period`; 9 = voicing ratio `A/(max(A,B)+8)` (2^23 fixed point; `+0xA4B8`, fallback `+0xA4A8`, `+0xA498`); 10 = `sqrt(power·0.9999999)`; 11 = periodicness `sqrt((p1−p2)/(p1+ε))·(p0·2048/2048)·1.0646746e−05`; 12/13 = gated cents ±`1731.234·ln(v)` (`+0xA58C`/`+0xA588`), threshold `10^(F(+0xA4E8)·0.11513)`; default forwards to `[vtable+0x20]`. `[05]` publishes id 9→`tonality`, id 11→`timbre` (12/13 only when `(int)clamp(quantization)<0`) | `dec_dsp_m02_1800E1520.json`; `dec2_pitch_process_1800E5160.txt` | OBS |
| D5 | Cents/frequency/tonality constants: `1200`, `1731.234` (=1200/ln2), `ln10/20 = 0.115129`, `ln2/1200 = 0.000577623`, `261.6256` (C4), `2048.0` scale, `8388607` (2^23−1) | both decompiles | OBS |
| D6 | Meter/status value conversion: clamp, `×0.23025850929` (ln10/10), `10^x`, min 1.0, push owner `[+0x60]` indices 2/4; −99/−97/−100 dB sentinels; gate state at +0x420 | `0x1800E7960` decompile | OBS |
| D7 | `dsp_t` prepare `0x1800E2030` (per-quantum worker, `[01]`): ring `+0x41A0` (write idx `+0x41B8`, mask `+0x41C0`), 4 samples/quantum, sign-inverted writes into per-voice work buffers (`+0x40A0` slices); **256-phase × 6-tap Lagrange table `0x1803B6170`** (ptr `+0x40F0`, phase mask `+0x4098 = 0x7F8`); read rate = quartic poly (`+0x40D8..+0x40E8`) × per-voice gains (`+0x40F8`/`+0x4128`) × 64; glide multipliers `+0x44E0`/`+0x44F8`; read positions double-buffered `+0x4188`; calls `0x18013BBC0(+0x4518, 1)` | `dec2_dsp_prepare_1800E2030.txt`; `table_dump.txt` | OBS |
| D8 | Engine per-block process = `0x1800E5160` (one logical function through `0x1800E7959`, incl. fallthrough at `0x1800E5990`) called once per block from `[05]` @ `0x1801206EF` with the `dl` flag; `dl≠0` re-publishes the sample-rate object (id 5); publishes the 6 named outputs change-gated + bit-mapped shared products (ids `0x10–0x14`, `0x15/0x16` pages, `0x17` pulse, `0x18`, `0x19`, `0x1A`, `0x1B`, `0x1C/0x1D`, `0x1E`) | call site + `dec2_pitch_process_1800E5160.txt` | OBS |
| D9 | Engine internal algorithm: **shifter = variable-rate Lagrange-interpolated delay-line reads** (resolved at read level); **no OLA/granulation layer found** in `[01]`/`[05]`; remaining unknowns: detector algorithm (leading candidate `+0x4518` sub-object), `xfadetime` consumption site, per-voice page semantics | `dec2_dsp_prepare_1800E2030.txt`, `dec2_pitch_process_1800E5160.txt` | OBS (shifter) / UNK (rest) |

## E. Tracking/tonality (what is known)

| # | Claim | Evidence | Status |
|---|---|---|---|
| E1 | Engine receives `pitchtracking` (0/1), `hysteresis=10`, `correctrate=1`, `mincorrect=0`, `intervalglide=1`, `quantization=0`, `tuning=0`, `minpitch`, `tune` (Hz, default 440), `gatelevel=−55` dB | records + D1 | OBS |
| E2 | Sample-rate classes build decimation filters ×2/×4/×8 with windowed lowpasses (DC gain ≈1 verified for 41-tap set: Σ≈0.99; cutoff ≈1.7–1.9 kHz equivalent) | `dec_dsp_m01_1800E10B0.json`; coefficient check | OBS/DER |
| E3 | Pitch output is in cents with C4 = 0; sentinel −65535 when no pitch; a 10^(dB/20) gate on the magnitude | `dec_dsp_m02_1800E1520.json` | OBS |
| E4 | Tonality/timbre mapping verified: `[05]` writes id 9 → `tonality` output slot (`+0x378`), id 11 → `timbre` (`+0x398`), change-gated; gated-cents ids 12/13 only when `(int)clamp(quantization) < 0`; `tonM` consumes `tonality` (label inherited; mechanically id 9) | `dec2_pitch_process_1800E5160.txt`; `dec2_base_ctor_1800E2C00.txt` | OBS (mapping) / [UNK] (meter semantics) |
| E5 | Engine detector type (autocorrelation/FFT/period-based) | not located in `[01]`/`[05]`; candidate the `+0x4518` sub-object (`FUN_18013BBC0`) primed by prepare | UNK |
| E6 | Notify-bit products: bit 1 → gate broadcast `0x1800E7960`; bit 2 → id `0x10` (`mincorrect`); bit 3 → id `0x11` (`correctrate`); bit 4 → ids `0x1C/0x1D` = `2^(±y/1200)`, `y = 100/(sr·0.25·t)`, `t = max(0.01, v·0.001)` (`intervalglide`); bit 5 → id `0x1E` int (`pitchtracking`); bit 0 unconsumed | `dec2_pitch_process_1800E5160.txt` | OBS |
| E7 | Scale machinery dumped: engine tone rows base `0x1803BB780` (code `c` → row `5+c`; 12 chromatic / 13 whole / 14–15 pentatonic / 16 enigmatic / 17 neapolitan / 18 hungarian), key-spelling map `0x1803BB610` (17 ints → pitch classes), ionian pattern `0x1803BB660` + fallback row `0x1803BB7B0`; `scal` literal set (`1..7,10,11,14,15,16,17,18,13`) never emits 12 → domain mismatch with engine rows | `table_dump.txt`; read site `0x1800E6269`; `dec2_pitch_config_1800E3350.txt` | OBS (tables) / CONFLICT (domain) |

## F. Transitions

| # | Claim | Evidence | Status |
|---|---|---|---|
| F1 | `xfadetime` (0..100 ms, default 10) and `intervalglide` (default 1) are engine inputs | records | OBS |
| F2 | `dsp_t` computes per-step glide ratios `2^(±1/(12x))` (setparam 17 → `+0xA55C/+0xA560`) and cents pairs `2^(±x/1200)` (setparam 16 → `+0xA564/+0xA568`) | D3 | OBS |
| F3 | Far-feedback delay uses `xfadetime=20` ms crossfade for tap changes; echo `moddelay` uses `declick=1`; feedback gains smoothed tc=20 ms; wet/dry smoothed tc=10 ms | records | OBS |
| F4 | Internal engine transition machinery: read-rate quartic driver + exponential glide multipliers (`+0x44E0`/`+0x44F8`), intervalglide rate pair, per-voice directory pages (ids `0x15`/`0x16`, pulse `0x17`), voice reset path (id `0x12` = `1/2048`, id `0x14`); `xfadetime` consumption site not found (bit 0 unconsumed) | `dec2_dsp_prepare_1800E2030.txt`; `dec2_pitch_process_1800E5160.txt` | OBS (mechanics) / UNK (xfade site) |

## G. Methodology and limitations

| # | Claim | Evidence | Status |
|---|---|---|---|
| G1 | All conclusions are static-analysis-only; no runtime validation | methodology (doc 01 §6) | OBS |
| G2 | Decompiler artifacts present (`__security_check_cookie` injection, mis-typed floats) | decompile headers | OBS |
| G3 | Function boundaries from `.pdata` may merge adjacent code regions (e.g., `0x180122E00` reported size ≈40 KB) | `functions.csv` vs disasm | OBS |
| G4 | Descriptive names (e.g., “anti-alias EQ”) are analyzer labels unless citing `statement=`/record text | throughout | DER |
