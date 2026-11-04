// ---------------------------------------------------------------------------
// OCTAVOX native core -- VST3 wrapper: module factory (R16-P4b)
// ============================================================================
// PROVENANCE: our own independent harmonizer implementation around the
// octavox native DSP core. NOT a reproduction of the original Eventide
// Octavox plug-in; no target code; the original binary was never executed.
//
// One class is registered: the OctavoxNativeProcessor single-component
// effect (processor + controller in one object, NOT distributable -- the
// kVstVersionString / class flags follow the SDK's adelay pattern).
// ---------------------------------------------------------------------------
#include "octavox_ids.h"
#include "octavox_native_processor.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using octavox::vst3wrap::OctavoxNativeProcessorUID;

#define stringPluginName octavox::vst3wrap::kPluginName

//------------------------------------------------------------------------
//  VST3 Plugin Factory
//------------------------------------------------------------------------
BEGIN_FACTORY_DEF(octavox::vst3wrap::kVendorName, octavox::vst3wrap::kVendorWeb,
                  octavox::vst3wrap::kVendorEmail, 1)

DEF_CLASS(OctavoxNativeProcessorUID, PClassInfo::kManyInstances, kVstAudioEffectClass,
          stringPluginName,
          0,  // NOT distributable (single component: processor + controller)
          octavox::vst3wrap::kPluginSubcategory,
          octavox::vst3wrap::kPluginVersion,  // plug-in version
          kVstVersionString,
          octavox::vst3wrap::OctavoxNativeProcessor::createInstance, nullptr)

END_FACTORY
