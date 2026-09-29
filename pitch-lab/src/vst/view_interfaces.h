#pragma once

// Pitch Lab VST3 product layer — read-only view interfaces between the
// processor and the editor (specification §7): the editor NEVER touches the
// audio path; it polls atomically-published snapshots through these.

#include "pluginterfaces/base/funknown.h"

#include "vst/realtime_adapter.h"

namespace pitchlab {
namespace vst {

//------------------------------------------------------------------------
class IPitchLabMeters : public Steinberg::FUnknown {
 public:
  virtual MetersSnapshot PLUGIN_API getMeters() = 0;
  static const Steinberg::FUID iid;
};
DECLARE_CLASS_IID(IPitchLabMeters, 0x1A2B3C41, 0x4D5E6F71, 0x81923A4B, 0x5C6D7E8F)

//------------------------------------------------------------------------
class IPitchLabStatus : public Steinberg::FUnknown {
 public:
  virtual StatusSnapshot PLUGIN_API getStatus() = 0;
  static const Steinberg::FUID iid;
};
DECLARE_CLASS_IID(IPitchLabStatus, 0x2B3C4D52, 0x5E6F7082, 0x92A3B4C5, 0x6D7E8F9A)

}  // namespace vst
}  // namespace pitchlab
