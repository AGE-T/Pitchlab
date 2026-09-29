#pragma once

// Pitch Lab VST3 product layer — the VSTGUI editor entry.
//
// The editor is a real product interface (specification §7): engine
// selector, pitch + LFO, per-engine parameter panels, dry/wet, bypass,
// output level, stereo in/out meters, honest runtime status — all drawn in
// the Pitch Lab visual identity (dark zinc panels, emerald accent, amber
// adaptation badge, monospace micro-labels). It binds to the VST parameter
// model (parameters.h — the ONE table) and reads meters/status ONLY
// through the read-only view interfaces (view_interfaces.h).

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

namespace pitchlab::vst {

/// Create the Pitch Lab editor for the given controller (main thread).
/// Returns nullptr if VSTGUI is unavailable (headless builds).
Steinberg::IPlugView* createPitchLabEditor(Steinberg::Vst::IEditController* controller);

}  // namespace pitchlab::vst
