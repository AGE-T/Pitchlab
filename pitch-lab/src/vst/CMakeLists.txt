# ---------------------------------------------------------------------------
# Pitch Lab VST3 product layer (research/pitch-lab-vst3-product-phase-specification.md)
#
# Guarded by PITCHLAB_BUILD_VST3 (default ON; OFF restores the exact v0.1
# target surface). The vendored official Steinberg VST3 SDK + VSTGUI are
# pinned in external/vst3sdk (ORIGIN.toml provenance; Steinberg VST3
# proprietary licence path — the documented product-phase exception).
#
# Layering (spec §2): the v0.1 research pipeline targets are untouched;
# this file adds ONLY product targets:
#   * PitchLab_vst3  — the plug-in module bundle (PitchLab.vst3)
#   * validator      — the official VST3 SDK validator (test infrastructure)
#   * vst test executables + the host-simulation driver (registered in the
#     TOP-LEVEL CMakeLists tests section, following the v0.1 pattern)
# ---------------------------------------------------------------------------

# The SDK's CMake needs >= 3.25 and its own module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../external/vst3sdk/cmake/modules")
include(SMTG_VST3_SDK)

# SDK configuration: NO plugin examples, NO hosting examples (the validator
# itself is built regardless), NO vstgui standalone (the plug-in editor does
# not need the standalone application framework — which would require gtkmm
# on Linux), NO utilities (moduleinfo.json is optional for hosts).
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../external/vst3sdk")
set(SDK_ROOT "${ROOT}")
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")

set(VST_SDK TRUE)  # enables the full sdk/sdk_hosting source sets

include_directories(${ROOT} ${SDK_ROOT})

add_subdirectory(${SDK_ROOT} vstsdk)

# VSTGUI for plug-in views (standalone OFF — see above)
set(VSTGUI_DISABLE_UNITTESTS 1)
set(VSTGUI_STANDALONE_EXAMPLES OFF)
set(VSTGUI_STANDALONE OFF)
set(VSTGUI_TOOLS OFF)
add_subdirectory(${SDK_ROOT}/vstgui4/vstgui vstgui.build)

# vstgui_support: the plug-in-side VSTGUI bindings (the SDK macro's
# definition, minus the standalone-enabling)
add_library(vstgui_support STATIC
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3editor.cpp
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3editor.h
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3groupcontroller.cpp
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3groupcontroller.h
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3padcontroller.cpp
  ${SDK_ROOT}/vstgui4/vstgui/plugin-bindings/vst3padcontroller.h
  ${SDK_ROOT}/public.sdk/source/vst/vstguieditor.cpp
  ${SDK_ROOT}/public.sdk/source/vst/vstguieditor.h
)
target_link_libraries(vstgui_support PUBLIC base vstgui vstgui_uidescription)
target_include_directories(vstgui_support PUBLIC ${SDK_ROOT}/vstgui4)
target_compile_definitions(vstgui_support PUBLIC $<$<CONFIG:Debug>:VSTGUI_LIVE_EDITING>)
smtg_target_setup_vstgui_deprecated_methods(vstgui_support)
smtg_target_setup_universal_binary(vstgui_support)
smtg_target_setup_universal_binary(vstgui)
smtg_target_setup_universal_binary(vstgui_uidescription)

# The vstgui target only propagates X11/freetype/glib/cairo/pango/fontconfig
# include dirs (upstream assumes system xcb dev headers). When the xcb util
# stack lives in a user prefix (scripts/setup-xcb-vstgui-deps.sh), add its
# include dirs explicitly (harmless duplicates on normal systems).
if(LINUX)
  find_package(PkgConfig REQUIRED)
  execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=includedir xcb-util
                  OUTPUT_VARIABLE PLXCB_PREFIX_INCL OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=includedir xkbcommon
                  OUTPUT_VARIABLE PLXKBCOMMON_INCL OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=libdir xcb-util
                  OUTPUT_VARIABLE PLXCB_PREFIX_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=libdir xkbcommon
                  OUTPUT_VARIABLE PLXKBCOMMON_LIB OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(PLXCB_PREFIX_INCL)
    message(STATUS "[PitchLab VST3] xcb-util prefix: ${PLXCB_PREFIX_INCL}")
    target_include_directories(vstgui PRIVATE ${PLXCB_PREFIX_INCL})
    target_include_directories(vstgui_uidescription PRIVATE ${PLXCB_PREFIX_INCL})
    target_include_directories(vstgui_support PRIVATE ${PLXCB_PREFIX_INCL})
    # link dirs must be PUBLIC so targets linking vstgui find the user-prefix
    # static xcb/xkbcommon libs (upstream assumes system locations)
    target_link_directories(vstgui PUBLIC ${PLXCB_PREFIX_LIB} ${PLXKBCOMMON_LIB})
  endif()
  if(PLXKBCOMMON_INCL AND NOT PLXKBCOMMON_INCL STREQUAL "/usr/include")
    message(STATUS "[PitchLab VST3] xkbcommon prefix: ${PLXKBCOMMON_INCL}")
    target_include_directories(vstgui PRIVATE ${PLXKBCOMMON_INCL})
    target_include_directories(vstgui_uidescription PRIVATE ${PLXKBCOMMON_INCL})
    target_include_directories(vstgui_support PRIVATE ${PLXKBCOMMON_INCL})
  endif()
endif()

# ---------------------------------------------------------------------------
# The plug-in
# ---------------------------------------------------------------------------

set(pitchlab_vst3_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/entry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/parameters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/processor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/realtime_adapter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/realtime_status.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/editor.cpp
  # the single-component base class implementation (the SDK ships it as a
  # sample-side source; the "again_simple" pattern)
  ${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
  # module initialisation machinery (required by the module entry)
  ${SDK_ROOT}/public.sdk/source/main/moduleinit.cpp
)

# The SDK's own source emits a GCC -Wextra copy-ctor warning (upstream code;
# not product code): keep -Werror for Pitch Lab sources only.
set_source_files_properties(${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-error")

find_package(Threads REQUIRED)

smtg_add_vst3plugin(PitchLab_vst3
  PACKAGE_NAME "PitchLab"
  SOURCES_LIST ${pitchlab_vst3_sources}
)
smtg_target_add_library_main(PitchLab_vst3)

target_include_directories(PitchLab_vst3 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../
)
target_link_libraries(PitchLab_vst3
  PRIVATE
    pitchlab_core
    sdk
    sdk_common
    base
    pluginterfaces
    vstgui
    vstgui_uidescription
    vstgui_support
    Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Product-layer warning/FP policy: the v0.1 determinism rule (-ffp-contract=off,
  # implementation spec §7.5) applies to the product layer too. -Werror is NOT
  # applied to this target: the vendored VST3 SDK / VSTGUI HEADERS (upstream
  # code, compiled inside our TUs) are not warning-clean under GCC -Wextra —
  # our own product sources are kept warning-clean and visible.
  target_compile_options(PitchLab_vst3 PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The REAL VSTGUI editor is compiled into the plug-in module (the product
# requirement: a real UI, no stub in the shipped binary). ui/editor.cpp is
# the ONE editor source: with this definition it builds the full editor;
# the test / host-simulation targets compile the same file WITHOUT it and
# get the honest headless nullptr stub (no windowing in the test harness).
target_compile_definitions(PitchLab_vst3 PRIVATE PITCHLAB_WITH_VSTGUI)


# ---------------------------------------------------------------------------
# Product-layer tests (implementation quality — NOT a project verification
# gate; the v0.1 test surface is untouched). Headless: the editor compiles
# to its honest nullptr stub without PITCHLAB_WITH_VSTGUI.
# ---------------------------------------------------------------------------

function(pitchlab_add_vst_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    pitchlab_core pitchlab_doctest sdk sdk_common base pluginterfaces
    Threads::Threads)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_options(${name} PRIVATE -ffp-contract=off)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ${name} COMMAND ${name}
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

# sources shared by the tests + the host-simulation driver (no VSTGUI:
# headless editor stub)
set(pitchlab_vst_headless_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/entry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/parameters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/processor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/realtime_adapter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/realtime_status.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/editor.cpp
  ${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
  ${SDK_ROOT}/public.sdk/source/main/moduleinit.cpp
)
# moduleHandle definition (moduleinit.cpp references it; the plugin gets it
# from linuxmain/dllmain via smtg_target_add_library_main, the test
# executables link it directly)
if(SMTG_LINUX)
  list(APPEND pitchlab_vst_headless_sources ${SDK_ROOT}/public.sdk/source/main/linuxmain.cpp)
elseif(SMTG_WIN)
  list(APPEND pitchlab_vst_headless_sources ${SDK_ROOT}/public.sdk/source/main/dllmain.cpp)
endif()

function(pitchlab_add_vst_suite name)
  pitchlab_add_vst_test(${name} ${CMAKE_SOURCE_DIR}/tests/${name}.cpp
    ${pitchlab_vst_headless_sources})
  # SDK sources inside test TUs: exempt from -Wpedantic strictness the same
  # way as the plugin target
  set_source_files_properties(
    ${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${SDK_ROOT}/public.sdk/source/main/moduleinit.cpp
    ${SDK_ROOT}/public.sdk/source/main/linuxmain.cpp
    ${SDK_ROOT}/public.sdk/source/main/dllmain.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
endfunction()

pitchlab_add_vst_suite(vst_parameters_test)
pitchlab_add_vst_suite(vst_adapter_test)
pitchlab_add_vst_suite(vst_processor_test)
pitchlab_add_vst_suite(vst_audio_path_test)

# T-VEP* (Task 29): the engine-owned parameter model — descriptor integrity,
# registry->parameter mapping, the generated table's frozen-surface equality
# (state compatibility), role split, discrete-choice display, binding
# resolution, engine-switching persistence.
pitchlab_add_vst_suite(vst_engine_params_test)

# T-RD* (Task 29): realtime fault diagnostics — the categorized counters'
# invariants + the 96 kHz granular / PV-phaselocked measurement drives.
pitchlab_add_vst_suite(vst_realtime_diagnostics_test)

# T-S* (Task 31): the retiring-chain seam coverage suite — the extreme
# same-engine latency jump (granular grain 0.1 -> 0.5, ΔΛ 19200) fault-free
# with the re-coverage served from the retained wet history + the
# replay bit-identity + the block/rate/pacing/LFO matrices + the
# cross-engine switch control.
pitchlab_add_vst_suite(vst_seam_test)

# T-RT* (Task 32): the realtime-capability status suite — the classifier's
# levels/bases/thresholds/fault-escalation/benchmark-prior rules as pure
# unit fixtures + the label texts (incl. the required warning wording) +
# integration drives proving the adapter's measurement accumulates, follows
# chain switches, and classifies real engines correctly.
pitchlab_add_vst_suite(vst_realtime_status_test)

# Task-33 continuation: the realtime host-path probe (the owner's manual
# VST3 host observations — Fixed crackle, Pitch+Formant dry/wet alternation,
# the underrun counts — reproduced through the REAL adapter path).
pitchlab_add_vst_suite(timepitch_host_probe)

# Task-34 continuation: the host-continuity probe (the second-round owner
# observations — the remaining RECT crackles, the intermittent Pitch-Synced
# wet, the pitch-slide "wet switching on/off" — the static material matrix,
# the RECT focus, the sweep/step host models, the wet-continuity telemetry).
pitchlab_add_vst_suite(timepitch_continuity_probe)

# The host-simulation driver: renders corpus material through the REAL VST3
# process path (used for the retained audio examples; tools/vst_host_sim.cpp)
add_executable(vst_host_sim ${CMAKE_SOURCE_DIR}/tools/vst_host_sim.cpp
  ${pitchlab_vst_headless_sources})
target_link_libraries(vst_host_sim PRIVATE pitchlab_core sdk sdk_common base
  pluginterfaces Threads::Threads)
target_compile_features(vst_host_sim PRIVATE cxx_std_20)
target_compile_options(vst_host_sim PRIVATE -ffp-contract=off)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(vst_host_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The UI screenshot host (Linux/X11 only — the Xvfb reference environment):
# loads the BUILT bundle the DAW way (VST3::Hosting::Module), opens the real
# VSTGUI editor through the plug-frame run loop, drives audio, captures the
# window (produces the retained UI evidence, spec §10).
if(LINUX AND PITCHLAB_BUILD_VST3)
  find_package(X11 REQUIRED)
  # module_linux.cpp: the platform module loader (the SDK's hosting library
  # leaves the platform-specific loader to the host application — the same
  # pattern the SDK's own validator uses)
  add_executable(vst_ui_screenshot ${CMAKE_SOURCE_DIR}/tools/vst_ui_screenshot.cpp
    ${CMAKE_SOURCE_DIR}/src/vst/parameters.cpp
    ${CMAKE_SOURCE_DIR}/src/vst/realtime_status.cpp
    ${SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp)
  target_link_libraries(vst_ui_screenshot PRIVATE
    pitchlab_core sdk sdk_common sdk_hosting base pluginterfaces X11::X11
    Threads::Threads ${CMAKE_DL_LIBS})
  target_include_directories(vst_ui_screenshot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../)
  target_compile_features(vst_ui_screenshot PRIVATE cxx_std_20)
  target_compile_options(vst_ui_screenshot PRIVATE -ffp-contract=off)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(vst_ui_screenshot PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_source_files_properties(${SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp
    PROPERTIES COMPILE_OPTIONS "-w")
endif()
