# ---------------------------------------------------------------------------
# OCTAVOX native core -- VST3 wrapper build (R16-P4b)
# ============================================================================
# Builds (Release by default):
#   octavoxnative               the VST3 plug-in module, bundled by the SDK's
#                               own packaging machinery as OctavoxNative.vst3
#                               (Contents/x86_64-linux/OctavoxNative.so +
#                               Contents/Resources/moduleinfo.json)
#   octavox_vst3_wrapper_tests  the in-process wrapper test suite
#   validator                   the VST3 SDK conformance validator (built by
#                               the SDK unconditionally; run it against the
#                               module -- see VST3_WRAPPER.md)
#
# Requirements: the VST3 SDK v3.7.12_build_20 at VST3_SDK_ROOT (default:
# ../../vst3sdk -- OUTSIDE this evidence tree; the SDK is NOT redistributed
# with the evidence package; see VST3_WRAPPER.md for the pinned-SHA
# acquisition record). The DSP core sources are compiled from ../ (the core
# tree itself stays untouched).
#
# The SDK's own examples and VSTGUI are disabled (no gtk/X11 dependency, no
# GUI -- this is a minimal processor+controller wrapper).
#
# Integration note: the smtg_* packaging macros must be visible in THIS
# scope, so after add_subdirectory(SDK) (which defines the sdk/sdk_hosting/
# moduleinfotool targets) we re-include SMTG_VST3_SDK here and repeat the
# SDK root's platform setup -- the documented external-consumer pattern.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.19)
project(octavox_vst3 LANGUAGES CXX VERSION 1.0.0)

set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../vst3sdk" CACHE PATH
    "VST3 SDK root (v3.7.12_build_20; see VST3_WRAPPER.md)")

if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${VST3_SDK_ROOT}. Set -DVST3_SDK_ROOT=<path> "
        "(see native/vst3/VST3_WRAPPER.md for the pinned-SHA acquisition).")
endif()

# ---------------------------------------------------------------------------
# R18-WDB (Windows dual build): the wrapper's compile-time default
# interpolation -- the VARIANT selection. Exactly one of
#   OCTAVOX_VST3_INTERP_LAGRANGE6 / _LAGRANGE6Q / _OBSKERNEL6
# is defined for every wrapper consumer (module, wrapper reference tests,
# compiled-module integration tests; consumed ONLY via the central helper
# octavox_build_config.h). The GENERIC DSP CORE DEFAULT stays
# Interpolation::Lagrange6 (native/include/octavox/dsp.hpp is UNCHANGED).
#
# The variant also selects the bundle/package name and the VST3 identities
# (display name + per-variant processor class FUID / factory UID -- see
# octavox_build_config.h). The 38-parameter host surface is identical in
# every variant (no host parameter is added for the interpolation).
# ---------------------------------------------------------------------------
set(OCTAVOX_VST3_INTERPOLATION "LAGRANGE6" CACHE STRING
    "VST3 wrapper default interpolation kernel: LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6")
string(TOUPPER "${OCTAVOX_VST3_INTERPOLATION}" OCTAVOX_VST3_INTERPOLATION)
if(NOT OCTAVOX_VST3_INTERPOLATION MATCHES "^(LAGRANGE6|LAGRANGE6Q|OBSKERNEL6)$")
    message(FATAL_ERROR "OCTAVOX_VST3_INTERPOLATION must be one of "
        "LAGRANGE6 | LAGRANGE6Q | OBSKERNEL6 (got: '${OCTAVOX_VST3_INTERPOLATION}')")
endif()
set(OCTAVOX_VST3_VARIANT_DEF "OCTAVOX_VST3_INTERP_${OCTAVOX_VST3_INTERPOLATION}=1")
if(OCTAVOX_VST3_INTERPOLATION STREQUAL "OBSKERNEL6")
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_ObsKernel6")
elseif(OCTAVOX_VST3_INTERPOLATION STREQUAL "LAGRANGE6Q")
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_Lagrange6Q")
else()
    set(OCTAVOX_VST3_BUNDLE_NAME "OctavoxRE_Lagrange6")
endif()
message(STATUS "OCTAVOX VST3 build variant: ${OCTAVOX_VST3_INTERPOLATION} "
               "(bundle ${OCTAVOX_VST3_BUNDLE_NAME}.vst3; define ${OCTAVOX_VST3_VARIANT_DEF})")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# The DSP core (compiled from the parent tree; the core sources are FROZEN
# and untouched by the wrapper -- one static lib shared by module + tests).
# ---------------------------------------------------------------------------
add_library(octavox_core_vst3 STATIC ../src/dsp.cpp)
target_include_directories(octavox_core_vst3 PUBLIC ../include ../src)
# The core is linked into a SHARED VST3 module: it needs PIC (its
# thread-local VoiceStats scratch emits TPOFF32 relocations otherwise).
set_target_properties(octavox_core_vst3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# The VST3 SDK: targets (sdk, sdk_hosting, moduleinfotool, validator, ...)
# ---------------------------------------------------------------------------
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${VST3_SDK_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/vst3sdk"
                 EXCLUDE_FROM_ALL)

# Bring the smtg_* macros into THIS scope + the SDK root's platform setup.
list(APPEND CMAKE_MODULE_PATH "${VST3_SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
set(ROOT "${VST3_SDK_ROOT}")
set(SDK_ROOT "${VST3_SDK_ROOT}")
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
include_directories(${ROOT} ${SDK_ROOT})

# ---------------------------------------------------------------------------
# R18-WDB MinGW (llvm-mingw + libc++) compatibility shims. The pinned SDK
# tree itself stays BYTE-UNTOUCHED (the pinned-SHA verification still
# passes); these only adjust compilation from OUR build scope:
#   1) public.sdk/source/common/threadchecker_win32.cpp uses std::terminate
#      without including <exception> (MSVC/libstdc++ provide it transitively;
#      libc++ does not) -> force-include <exception>.
#   2) public.sdk/source/vst/utility/alignedalloc.h guards its Windows path
#      with _MSC_VER, so MinGW takes the POSIX std::aligned_alloc branch,
#      which libc++ does not provide -> force-include
#      mingw_compat_aligned_alloc.h (see that file's header comment for the
#      full dormancy analysis of the alloc/free pairing caveat).
# Recorded in WINDOWS_DUAL_BUILD_REPORT.md.
# ---------------------------------------------------------------------------
if(MINGW)
    # a SINGLE -include flag per target (CMake deduplicates repeated
    # '-include' tokens, which would orphan a second file argument)
    set(OCTAVOX_MINGW_SHIM "${CMAKE_CURRENT_SOURCE_DIR}/mingw_compat_shim.h")
    foreach(octavox_sdk_target sdk sdk_hosting sdk_common)
        if(TARGET ${octavox_sdk_target})
            target_compile_options(${octavox_sdk_target} PRIVATE
                -include "${OCTAVOX_MINGW_SHIM}")
        endif()
    endforeach()
    if(TARGET moduleinfotool)
        target_compile_options(moduleinfotool PRIVATE
            -include "${OCTAVOX_MINGW_SHIM}")
    endif()
    #   3) the SDK validator's Windows entry point is wmain (main.cpp's
    #      SMTG_OS_WINDOWS branch); MSVC links it automatically, MinGW needs
    #      the -municode driver flag to select the wmainCRTStartup entry.
    #      Applied to the target from OUR scope (SDK tree untouched).
    if(TARGET validator)
        target_link_options(validator PRIVATE -municode)
    endif()
endif()

# ---------------------------------------------------------------------------
# The plug-in module (bundle name per VARIANT: OctavoxRE_<Variant>.vst3)
# ---------------------------------------------------------------------------
smtg_add_vst3plugin(octavoxnative
    PACKAGE_NAME "${OCTAVOX_VST3_BUNDLE_NAME}"
    SOURCES_LIST
        octavox_native_processor.cpp
        octavox_native_processor.h
        octavox_params.cpp
        octavox_params.h
        octavox_build_config.h
        octavox_ids.h
        factory.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_link_libraries(octavoxnative PRIVATE octavox_core_vst3 sdk)
target_include_directories(octavoxnative PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(octavoxnative PRIVATE ${OCTAVOX_VST3_VARIANT_DEF})
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavoxnative PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The in-process wrapper test suite (drives the actual VST3 classes through
# their COM interfaces; host-side objects = the SDK hosting utilities; the
# allocation counter from the core's test harness gates zero allocations).
# ---------------------------------------------------------------------------
add_executable(octavox_vst3_wrapper_tests
    octavox_vst3_tests.cpp
    octavox_params.cpp
    octavox_native_processor.cpp
    factory.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
    ../tests/alloc_counter.cpp
)
target_link_libraries(octavox_vst3_wrapper_tests PRIVATE octavox_core_vst3
                                                         sdk sdk_hosting)
target_include_directories(octavox_vst3_wrapper_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(octavox_vst3_wrapper_tests PRIVATE ${OCTAVOX_VST3_VARIANT_DEF})
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_vst3_wrapper_tests PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# R18-P6: the ACTUAL-COMPILED-MODULE integration harness.  Loads the built
# OctavoxRE_<Variant>.vst3 bundle through the pinned SDK's VST3::Hosting::Module
# loader (a real dlopen + GetPluginFactory) and drives the exported factory
# instances through the full host interface contract; outputs are compared
# bit-exactly with the hand-driven core reference.  This is an
# SDK-module-load + interface-contract test -- NOT a DAW compatibility
# claim (see the file header).
# ---------------------------------------------------------------------------
add_executable(octavox_module_integration_tests
    octavox_module_integration_tests.cpp
    octavox_params.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
)
target_compile_definitions(octavox_module_integration_tests PRIVATE ${OCTAVOX_VST3_VARIANT_DEF})
# the SDK keeps Module::create in the platform file
if(WIN32)
    target_sources(octavox_module_integration_tests PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp)
elseif(APPLE)
    target_sources(octavox_module_integration_tests PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm)
else()
    target_sources(octavox_module_integration_tests PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp)
endif()
# (the Windows/macOS harness paths are AUTHORED, not executed -- the
#  R18 execution evidence is Linux-only, as stated in VST3_WRAPPER.md §7.)
target_link_libraries(octavox_module_integration_tests PRIVATE
    octavox_core_vst3 sdk sdk_hosting)
target_include_directories(octavox_module_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_module_integration_tests PRIVATE -Wall -Wextra)
endif()
add_dependencies(octavox_module_integration_tests octavoxnative)

# ---------------------------------------------------------------------------
# R19-P3b: the DETERMINISTIC DUAL-MODULE VST3 NULL TEST tool.  Loads TWO
# separately-built bundles (OctavoxRE_Lagrange6.vst3 + OctavoxRE_ObsKernel6.vst3)
# SIMULTANEOUSLY in one process through the pinned SDK's VST3::Hosting::Module
# loader (two real dlopens of distinct .so files with identical exported symbol
# names) and drives one processor instance from each factory in lockstep with
# identical stimulus; unity must be bit-identical (parked integer-tap path),
# non-unity MUST differ (different interpolation kernels engaged; a
# bit-identical non-unity result is a build-configuration defect -> exit 1).
#
# This tool is deliberately NOT variant-compiled (no
# ${OCTAVOX_VST3_VARIANT_DEF}, no octavox_core_vst3 link, no wrapper headers):
# both expected processor class FUIDs are hardcoded inside it, so ONE binary
# verifies BOTH module builds.  It only needs the SDK hosting stack + the
# harness wav_io (32-bit float A/B/diff output).
# ---------------------------------------------------------------------------
add_executable(octavox_dual_nulltest
    octavox_dual_nulltest.cpp
    ../tests/wav_io.cpp
    ${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp
)
# the SDK keeps Module::create in the platform file
if(WIN32)
    target_sources(octavox_dual_nulltest PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp)
elseif(APPLE)
    target_sources(octavox_dual_nulltest PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_mac.mm)
else()
    target_sources(octavox_dual_nulltest PRIVATE
        ${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp)
endif()
target_link_libraries(octavox_dual_nulltest PRIVATE sdk sdk_hosting)
target_include_directories(octavox_dual_nulltest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_dual_nulltest PRIVATE -Wall -Wextra)
endif()
add_dependencies(octavox_dual_nulltest octavoxnative)
