// Pitch Lab VST3 product layer — the module entry / plug-in factory
// (the official Steinberg VST3 SDK pattern; platform entry points are
// provided by the SDK's smtg_target_add_library_main — linuxmain.cpp on
// Linux, dllmain.cpp on Windows, macmain.cpp on macOS).

#include "public.sdk/source/main/moduleinit.h"
#include "public.sdk/source/main/pluginfactory.h"

#include "vst/parameters.h"
#include "vst/processor.h"

#define stringPluginName "Pitch Lab"
#define stringVendorName "AGE-T / Pitch Lab"
#define stringVendorWeb "https://github.com/AGE-T/Pitchlab"
#define stringVendorEmail "pitchlab@example.invalid"
#define stringPluginCategory "Fx|Pitch Shift"

using namespace Steinberg;
using namespace Steinberg::Vst;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------

BEGIN_FACTORY_DEF(stringVendorName, stringVendorWeb, stringVendorEmail)

//---The Pitch Lab single-component effect--------------------------------
DEF_CLASS2(INLINE_UID_FROM_FUID(pitchlab::vst::PitchLabProcessorUID),
           PClassInfo::kManyInstances,   // cardinality
           kVstAudioEffectClass,         // the component category (do not change)
           stringPluginName,             // plug-in name
           Vst::kDistributable,          // NOT distributable would be kNotDistributable —
                                         // single component: keep the SDK default flag here
           stringPluginCategory,         // subcategory (Fx|Pitch Shift)
           "0.1.0",                      // plug-in version
           kVstVersionString,            // the VST 3 SDK version (do not change)
           pitchlab::vst::PitchLabProcessor::createInstance)

END_FACTORY

//------------------------------------------------------------------------
// InitModule/DeinitModule aggregators come from the SDK's moduleinit.cpp
// (linked into the module). The engine registry init runs as a module
// initializer — the single authoritative v0.1 registration point, sealed
// once per process at module load.
static const Steinberg::ModuleInitializer kRegistryInit{[] {
  pitchlab::vst::initEngineRegistryOnce();
}};
