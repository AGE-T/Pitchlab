# OCTAVOX R33 — THE MINIMAL EDITOR (the wrapper's first GUI surface; a minimalist software-rendered IPlugView)

**Scope note (standing):** clean-room PitchLab research line (octavox-native
16.3.0). The original Ox.vst3 was **never executed** in this project — every
original-behaviour claim cites the published static-audit record (R26–R30).
This round adds an **own-design GUI** to the PitchLab wrapper; it makes no
new RE claims.

## 0. What this round delivers

The PitchLab wrapper had **no editor** (`IPlugView` was deliberately not
implemented through R32 — hosts showed the generic parameter view). This
round delivers the **minimal editor** requested by the user
("tudnál neki egy minimalista ui-t is készíteni?"):

- **`createView("Editor")`** on the processor now serves a fixed-size
  **784x416** `IPlugView` — a dark, warm-neutral "pro-audio minimal" panel
  with a single amber accent (no blue/indigo anywhere, matching the
  project's palette rules);
- **all 38 host parameters reachable from the UI**: global pitch slider,
  voices stepper, harmony-map toggle, feedback + mix sliders, per-voice
  transpose fields / gain bars / delay fields / mute buttons, and a
  clickable bypass LED in the header — every edit lands on the exact
  quantized grid the host surface defines (the wrapper's own conversion
  functions), through the canonical
  `beginEdit → setParamNormalized+performEdit → endEdit` protocol;
  **double-click = documented default**;
- the **R31 vendor telemetry displayed live** in the bottom panel:
  per-voice Hz (derived from id 7's cents-from-C4), raw cents, the id-11
  confidence bar, the id-8 glide ratio, and per-voice activity LEDs driven
  by `consumeDirtyBits()` — the same dirty-bit contract the R31 module
  tests pin. The −65535 no-pitch sentinel **and** the zeroed
  parked/never-polled cells both render honestly as `--`;
- **zero new heavyweight dependencies**: no VSTGUI (the pinned SDK's
  vstgui4 submodule is empty; the build always had it off), no font stack
  (text is an **embedded classic 5x7 bitmap font**, retyped from the
  public-domain terminal glyph set), no X11 extensions — core **Xlib**
  only on Linux, **user32+gdi32** on Windows;
- **bit-reproducible rendering**: one pixel format (`0x00RRGGBB`) that is
  simultaneously the X11 ZPixmap depth-24 LSBFirst layout and the Win32
  top-down 32-bpp BI_RGB DIB layout — both platform layers blit the same
  bytes; a **golden-render test pins the full default-surface frame**
  (FNV-1a `0x0f80c7f43a8358ad`);
- the **published screenshots are the real renderer output**, not mock-ups:
  a new offscreen tool (`octavox_ui_screenshot`) renders the same
  model/renderer the live view uses and writes a self-contained
  stored-deflate PNG (no zlib/libpng dependency).

Package: `public/PitchLab_R33_Minimal_UI_VST3.zip` — **both** the Linux and
the Windows x64 bundles of the same source (the R32 cross pipeline, zig
0.13.0 re-acquired after the sandbox reset) + the PDB + an
install/host-author README + this report + the verification logs + the
two screenshots. SHA-256 sidecar alongside.

## 1. Architecture (three strictly separated layers)

**`MinimalUiModel`** — platform-free logic: layout, cached plain values,
cached telemetry frames, the mouse gesture state machine, the full-frame
draw. Everything is synchronous and single-threaded on "the UI thread"
(whichever thread the platform layer runs it on). This is the layer the
logic tests drive directly, with the real processor and a recording mock
host handler — no window system involved.

**The pixel buffer** — 784x416 `uint32_t` `0x00RRGGBB`, shared verbatim
by both platform layers (see §0).

**The platform layers** (`octavox_minimal_view.cpp`, compiled per OS):

- **Linux**: an X11 child window inside the host's `X11EmbedWindowID`
  parent, running **on the host's UI thread** through the VST 3.6.8+
  Linux hosting contract: `IRunLoop` (queried from the `IPlugFrame`)
  drives an `IEventHandler` on the X11 connection fd + an `ITimerHandler`
  at ~30 Hz. **No private thread, no cross-thread parameter access** —
  a host that does not provide `IRunLoop` cannot attach the view
  (`attached()` returns kResultFalse; documented, and every current Linux
  DAW provides it). Mouse drags use an explicit pointer grab so gestures
  survive leaving the window; a permissive X11 error handler is installed
  while attached (a host tearing the parent down must not kill the host
  process over async BadWindow — the standard GUI-toolkit policy).
- **Windows**: a `WS_CHILD` window created on the attaching thread;
  `WM_TIMER` (~30 Hz) ticks, `WM_PAINT` blits the DIB, mouse messages
  drive the model — all on the host's message thread (the spec-perfect
  Win32 plugin shape). Cross-compiled and machine-code verified (same
  evidence class as the accepted R32 delivery; no PE execution in this
  sandbox).
- **macOS**: not implemented (no hardware); `isPlatformTypeSupported`
  refuses NSView — stated honestly, as with R32's Windows limits.

**Editor lifecycle**: `attached()` validates the platform type, opens the
platform resources, draws the first frame synchronously (the view is
visible before the host's first timer tick); `removed()` unregisters the
run-loop handlers, destroys the window/DIB and closes the display. A
defensive `removed()` in the destructor covers hosts that forget it. The
view holds strong `IPtr` references on the owner processor; the processor
holds no view state (every `createView` call hands out a fresh view).

## 2. The "no VSTGUI" decision (recorded)

The pinned SDK's vstgui4 submodule is EMPTY in this repository, and the
wrapper build has always compiled with `SMTG_ENABLE_VSTGUI_SUPPORT=OFF`
(no gtk/X11 font stack). Re-enabling it would have added a large
third-party GUI framework for a deliberately minimal editor. The
self-contained path — a ~700-line software renderer + embedded 5x7 font —
keeps the module and every test binary linkable with only Xlib/GDI, and
makes every rendered frame **bit-reproducible** (the golden test). The
text is ASCII-only and formatting is C-locale `snprintf`: deterministic
on every platform.

## 3. The edit protocol and value mapping

Every UI gesture goes through the public `EditController` API on the UI
thread: `beginEdit(id)` → per change `setParamNormalized(id, norm)` +
`performEdit(id, norm)` → `endEdit(id)`. The values are computed through
the wrapper's own `plainFromNormalized`/`normalizedFromPlain` — the SAME
step-quantized grid the host surface defines (0.01 st pitch/transpose
steps; exact round-trips). Sliders are absolute (click = jump); transpose
and delay fields are relative vertical drags (0.02 st/px, 0.4 ms/px); the
voices stepper steps ±1 with domain clamps; toggles flip. The protocol
test parses a recording mock handler: exactly one begin before the first
perform, ≥1 perform, exactly one end, end last.

## 4. Verification (all gates green; logs in octavox-work/r33/evidence/)

| Gate | Result |
|---|---|
| native core suite, Release (fresh build_r33_native) | **115/115**; all four reference hashes bit-exact; r15acc determinism `0x35fe0d51a4372ac8` — **identical to the R31/R32 record** (R33 is core-neutral) |
| native core suite, ASan/UBSan + LeakSanitizer | **115/115, zero reports, zero leaks** |
| wrapper tests (the frozen surface), Release + ASan | **21/21** unchanged |
| **ui tests (NEW, `octavox_ui_tests`)**, Release + ASan | **11/11** — font, renderer, hit-map 38/38, drag protocol + quantization, relative drags + clamps, toggles/steppers, double-click defaults, telemetry values (220 Hz → −300.009 cents pin; the sentinel), **golden render**, view lifecycle, **X11 pixel identity** |
| module integration (fresh bundle) | **17/17** (16 → 17: the new `minimal_editor_surface` case drives the SHIPPED module: dlopen → factory → createView → real X11 attach → painted-pixel probe: 214,733 panel px / 272 accent px / 81,091 bg px; teardown verified). Release + ASan/LSan |
| Steinberg validator (Linux bundle) | **47/47** |
| Windows module (machine-code + structural) | ymm=275 vfmadd=26 cpuid=3 xgetbv=1 — **identical to the R32-W record** (the editor TUs are baseline-ISA); GDI32 (new) + USER32 imported, BeginPaint/CreateWindowExW/RegisterClassExW/SetTimer/CreateDIBSection/SetDIBitsToDevice linked; **zero X11 references**; single GetPluginFactory export; both GUIDs found; moduleinfo byte-identical to the twin |

**The pixel-identity proof** (the round's deepest check, under a real Xvfb
X server): the test acts as the Linux host — it implements `IPlugFrame` +
`IRunLoop`, attaches the view into a real parent window, pumps fd events
and timer ticks — then `XGetImage`s the child window and memcmp-compares
the **server-side pixels** with the renderer's buffer: bit-identical,
before and after live audio changes the telemetry. `removed()` then
destroys the child window and unregisters every run-loop handler.

**A harness defect found and fixed along the way:** running the module
suite under LeakSanitizer (new in this round's ASan gate) exposed that the
R18-era `queryInterface → IPtr(p)` adoption sites double-owned every
second instance (the IPtr default add-refs; invisible in the Release-only
runs of R18–R32). All sites now adopt with `addRef=false`; the suite is
leak-clean. Product code was not affected.

**Visual review:** the rendered frames were inspected via a vision model
(critique pass): layout clean, no overlap/clipping, state semantics clear
(active/inactive voices, red mute, lit telemetry LEDs); the one real issue
it caught — never-polled telemetry cells displaying "261.6 Hz / +0.0c"
instead of no-data — was fixed by gating the display on the id-8 ratio
(a tracking voice always has ratio > 0; zeroed cells read `--`).

## 5. Honest limits

- **macOS: not implemented** (no hardware); the view refuses to attach
  there. Windows is cross-compiled (the recorded zig deviation) and
  **machine-code verified only** — no PE execution in this sandbox; the
  identical model/renderer logic is executed end-to-end on Linux.
- Minimal by design: no wheel/keyboard handling, no DPI scaling, no
  resize (canResize = false), no tooltips (the R21 help texts remain
  host-side metadata), fixed 30 Hz telemetry repaint.
- On Linux the view requires the host's `IRunLoop` (the VST 3.6.8+
  contract) — a host without it cannot attach the editor (parameters
  remain fully automatable regardless).
- The golden hash pins THIS layout/font/palette; any deliberate layout
  change requires a conscious re-pin (the test says so).
- Not a DAW compatibility claim (standing scope label): the editor is
  verified against the SDK contract, a real X server, and the validator —
  no commercial DAW was involved.
