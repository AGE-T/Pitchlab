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
# The plug-in module (bundle name OctavoxNative.vst3)
# ---------------------------------------------------------------------------
smtg_add_vst3plugin(octavoxnative
    PACKAGE_NAME "OctavoxNative"
    SOURCES_LIST
        octavox_native_processor.cpp
        octavox_native_processor.h
        octavox_params.cpp
        octavox_params.h
        octavox_ids.h
        factory.cpp
        ${VST3_SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_link_libraries(octavoxnative PRIVATE octavox_core_vst3 sdk)
target_include_directories(octavoxnative PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
if(NOT MSVC)  # R17-P2: -Wall/-Wextra are GCC/Clang options
    target_compile_options(octavox_vst3_wrapper_tests PRIVATE -Wall -Wextra)
endif()
