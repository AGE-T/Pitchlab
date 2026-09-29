#include "vst/ui/editor.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

#ifdef PITCHLAB_WITH_VSTGUI

#include "public.sdk/source/vst/vstguieditor.h"
#include "vst/ui/views.h"
#include "vst/view_interfaces.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/icontrollistener.h"

#if LINUX
#include "vstgui/lib/platform/linux/x11platform.h"
#endif

namespace pitchlab::vst {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace ui_ = ::pitchlab::vst::ui;

namespace {

#if LINUX
// The host-side run-loop adapter (the SDK's plugin-binding pattern):
// VSTGUI's X11 frame needs an X11::IRunLoop; the HOST provides the event
// pump through IPlugFrame -> Steinberg::Linux::IRunLoop. Without this
// adapter, frame->open(parent, platformType) passes NO FrameConfig and
// VSTGUI's RunLoop::init (nullptr) dereferences null — the editor could
// not open on ANY Linux host (found by opening the real editor headlessly
// under Xvfb; measured, never silent).
class HostRunLoop final : public VSTGUI::X11::IRunLoop,
                          public VSTGUI::AtomicReferenceCounted {
 public:
  struct EvHandler final : Steinberg::Linux::IEventHandler {
    VSTGUI::X11::IEventHandler* handler = nullptr;
    tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
      QUERY_INTERFACE(iid, obj, Steinberg::Linux::IEventHandler::iid, IEventHandler)
      QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, FUnknown)
      *obj = nullptr;
      return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override {
      if (handler) handler->onEvent();
    }
  };
  struct TimerHandler final : Steinberg::Linux::ITimerHandler {
    VSTGUI::X11::ITimerHandler* handler = nullptr;
    tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
      QUERY_INTERFACE(iid, obj, Steinberg::Linux::ITimerHandler::iid, ITimerHandler)
      QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, FUnknown)
      *obj = nullptr;
      return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    void PLUGIN_API onTimer() override {
      if (handler) handler->onTimer();
    }
  };

  bool registerEventHandler(int fd, VSTGUI::X11::IEventHandler* handler) override {
    if (!host) return false;
    auto* h = new EvHandler;
    h->handler = handler;
    if (host->registerEventHandler(h, fd) == Steinberg::kResultTrue) {
      events.emplace_back(h);
      return true;
    }
    delete h;
    return false;
  }
  bool unregisterEventHandler(VSTGUI::X11::IEventHandler* handler) override {
    if (!host) return false;
    for (auto it = events.begin(); it != events.end(); ++it) {
      if ((*it)->handler == handler) {
        host->unregisterEventHandler(*it);
        delete *it;
        events.erase(it);
        return true;
      }
    }
    return false;
  }
  bool registerTimer(uint64_t interval, VSTGUI::X11::ITimerHandler* handler) override {
    if (!host) return false;
    auto* h = new TimerHandler;
    h->handler = handler;
    if (host->registerTimer(h, interval) == Steinberg::kResultTrue) {
      timers.emplace_back(h);
      return true;
    }
    delete h;
    return false;
  }
  bool unregisterTimer(VSTGUI::X11::ITimerHandler* handler) override {
    if (!host) return false;
    for (auto it = timers.begin(); it != timers.end(); ++it) {
      if ((*it)->handler == handler) {
        host->unregisterTimer(*it);
        delete *it;
        timers.erase(it);
        return true;
      }
    }
    return false;
  }

  explicit HostRunLoop(Steinberg::FUnknown* plugFrame)
      : host(Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop>(plugFrame)) {}

 private:
  std::vector<EvHandler*> events;
  std::vector<TimerHandler*> timers;
  Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> host;
};
#endif  // LINUX

// Logical editor size (DPI-scaled by VSTGUI's zoom support)
constexpr CCoord kEditorWidth = 680;
constexpr CCoord kEditorHeight = 450;
constexpr int kUiPollMs = 33;  // ~30 Hz meters/status polling

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

class PitchLabEditor final : public VSTGUIEditor, public IControlListener {
 public:
  explicit PitchLabEditor(EditController* controller) : VSTGUIEditor(controller) {}

  ~PitchLabEditor() override = default;

  // ---- VSTGUIEditor ------------------------------------------------------
  bool open(void* parent, const PlatformType& platformType) override {
    const CRect frameRect(0, 0, kEditorWidth, kEditorHeight);
    frame = new CFrame(frameRect, this);
    frame->setBackgroundColor(ui_::Palette::bg());
    buildUi();
#if LINUX
    // the X11 frame REQUIRES a run loop (see HostRunLoop above)
    VSTGUI::X11::FrameConfig config;
    config.runLoop = VSTGUI::owned(new HostRunLoop(plugFrame));
    frame->open(parent, platformType, &config);
#else
    frame->open(parent, platformType);
#endif
    setIdleRate(kUiPollMs);
    return true;
  }

  void close() override {
    if (frame != nullptr) {
      frame->removeAll();
      frame->forget();
      frame = nullptr;
    }
  }

  // ---- idle polling (meters/status; NEVER the audio path) ------------------
  VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) override {
    if (message == VSTGUI::CVSTGUITimer::kMsgTimer) {
      if (frame != nullptr) {
        pollMetersAndStatus();
        syncControlValues();
      }
    }
    return VSTGUIEditor::notify(sender, message);
  }

  void valueChanged(CControl* control) override {
    // UI change -> the authoritative VST parameter path. BOTH
    // responsibilities, in the VST3-correct order:
    //   1. setParamNormalized — update the CONTROLLER's own parameter value
    //      (the single source of truth; this is the single-component
    //      processor override that ALSO publishes the snapshot to the
    //      realtime adapter and notifies latency).
    //   2. performEdit — notify the HOST of the change (the automation
    //      gesture path). performEdit is NOT a substitute for (1): the SDK
    //      EditController::performEdit only forwards to IComponentHandler,
    //      it never touches the local value.
    // The previous implementation called performEdit WITHOUT
    // setParamNormalized: the controller value stayed unchanged, so the
    // 33 ms syncControlValues() loop wrote the OLD value back into the UI
    // (the "slider cannot be moved" revert loop), and engine-configuration
    // parameters (engine, excursion, crossfade, grain, FFT, hop — the
    // kNoFlags, non-automatable set) never reached the host echo path at
    // all, so they could not be changed from the UI reliably. The musical
    // parameters (pitch/LFO/mix/level/bypass, kCanAutomate) only appeared
    // to work because hosts echo automatable performEdits back.
    EditController* ec = getController();
    if (ec == nullptr || control == nullptr) return;
    const ParamID tag = static_cast<ParamID>(control->getTag());
    const ParamValue norm = control->getValueNormalized();
    ec->beginEdit(tag);
    if (ec->setParamNormalized(tag, norm) == kResultTrue) {
      ec->performEdit(tag, ec->getParamNormalized(tag));
    }
    ec->endEdit(tag);
    onUiStateChanged();
  }

 private:
  // ---- interface access through the controller (queryInterface, released) --
  template <typename I>
  I* getInterface(const FUID& iid) {
    void* obj = nullptr;
    EditController* ec = getController();
    if (ec == nullptr) return nullptr;
    if (ec->queryInterface(iid, &obj) == kResultOk) {
      return static_cast<I*>(obj);
    }
    return nullptr;
  }

  // ---- UI construction --------------------------------------------------------
  void buildUi() {
    CViewContainer* root = frame;
    buildHeader(root);
    buildEngineSelector(root);
    buildPitchPanel(root);
    buildEnginePanel(root);
    buildOutputPanel(root);
    buildMetersAndStatus(root);
    onUiStateChanged();
  }

  void buildHeader(CViewContainer* root) {
    const CRect r(12, 10, kEditorWidth - 12, 44);
    auto* panel = new ui_::PanelView(r);
    root->addView(panel);

    auto* logo = new ui_::MicroLabel(CRect(10, 8, 90, 24), "PITCH LAB",
                                     ui_::Palette::accent(), 0);
    panel->addView(logo);

    engineName_ = new ui_::MicroLabel(CRect(10, 24, 320, 38), "native.vardelay",
                                      ui_::Palette::textDim(), 0);
    panel->addView(engineName_);

    adaptation_ = new ui_::MicroLabel(
        CRect(kEditorWidth - 12 - 210 - 12, 8, kEditorWidth - 12 - 12, 24),
        "CONTINUOUS REALTIME", ui_::Palette::accent(), 1);
    panel->addView(adaptation_);

    format_ = new ui_::MicroLabel(
        CRect(kEditorWidth - 12 - 210 - 12, 24, kEditorWidth - 12 - 12, 38), "— kHz · — · — ms",
        ui_::Palette::textFaint(), 1);
    panel->addView(format_);

    latency_ = new ui_::MicroLabel(CRect(320, 8, 460, 24), "LATENCY —", ui_::Palette::textDim(), 1);
    panel->addView(latency_);
  }

  void buildEngineSelector(CViewContainer* root) {
    auto* label = new ui_::MicroLabel(CRect(12, 54, 160, 68), "ENGINE",
                                      ui_::Palette::textFaint(), 0);
    root->addView(label);

    const CRect r(12, 70, 160, 70 + 5 * 34.0);
    std::vector<std::string> labels;
    for (int i = 0; i < engineCount(); ++i) {
      const char* id = engineIdForIndex(i);
      labels.emplace_back(id != nullptr ? shortEngineName(id) : "?");
    }
    engineSelector_ = new ui_::PLSegmented(r, this, param::kEngine, labels, true);
    // The selector must reflect the AUTHORITATIVE controller value the
    // moment the editor opens. The previous implementation relied on the
    // CControl default (0 = the first engine): while the actual parameter
    // was e.g. the default native.vardelay, the selector showed the first
    // engine selected until the first 33 ms sync tick corrected it — and
    // a click on the ALREADY-"selected" wrong segment then computed a
    // "no change" for the sync loop, compounding the revert behaviour.
    EditController* ec = getController();
    engineSelector_->setValueNormalized(
        ec != nullptr ? ec->getParamNormalized(param::kEngine) : 0.0);
    root->addView(engineSelector_);
  }

  void buildPitchPanel(CViewContainer* root) {
    const CRect r(172, 54, 460, 190);
    auto* panel = new ui_::PanelView(r);
    root->addView(panel);

    auto* label = new ui_::MicroLabel(CRect(12, 10, 200, 24), "PITCH",
                                      ui_::Palette::textFaint(), 0);
    panel->addView(label);

    pitchValue_ = new ui_::ValueLabel(CRect(150, 8, 276, 30), "+0.00 st",
                                      ui_::Palette::accent());
    panel->addView(pitchValue_);

    pitchSlider_ = addSlider(panel, param::kPitch, CRect(12, 36, 265, 56), true);

    auto* lfoLabel = new ui_::MicroLabel(CRect(12, 70, 200, 84), "PITCH CURVE · LFO",
                                         ui_::Palette::textFaint(), 0);
    panel->addView(lfoLabel);
    lfoRateSlider_ = addSlider(panel, param::kLfoRate, CRect(12, 92, 180, 112), true);
    lfoRateValue_ = new ui_::MicroLabel(CRect(185, 96, 270, 110), "5.00 Hz",
                                        ui_::Palette::text(), 1);
    panel->addView(lfoRateValue_);
    lfoDepthSlider_ = addSlider(panel, param::kLfoDepth, CRect(12, 118, 180, 138), true);
    lfoDepthValue_ = new ui_::MicroLabel(CRect(185, 122, 270, 136), "0.00 st",
                                         ui_::Palette::text(), 1);
    panel->addView(lfoDepthValue_);

    auto* note = new ui_::MicroLabel(CRect(12, 142, 265, 156),
                                     "the live ratio curve: host automation + LFO, "
                                     "envelope-clamped",
                                     ui_::Palette::textFaint(), 0);
    panel->addView(note);
  }

  void buildEnginePanel(CViewContainer* root) {
    const CRect r(472, 54, kEditorWidth - 12, 286);
    enginePanel_ = new ui_::PanelView(r);
    root->addView(enginePanel_);

    panelTitle_ = new ui_::MicroLabel(CRect(12, 10, 190, 24), "VARDELAY",
                                      ui_::Palette::textDim(), 0);
    enginePanel_->addView(panelTitle_);

    // sliders for the CURRENT engine's parameters only (created lazily)
    rebuildEnginePanelControls();
  }

  void buildOutputPanel(CViewContainer* root) {
    const CRect r(172, 198, 460, 286);
    auto* panel = new ui_::PanelView(r);
    root->addView(panel);

    auto* label = new ui_::MicroLabel(CRect(12, 10, 200, 24), "OUTPUT",
                                      ui_::Palette::textFaint(), 0);
    panel->addView(label);

    mixSlider_ = addSlider(panel, param::kMix, CRect(12, 36, 130, 56), true);
    auto* mixLabel = new ui_::MicroLabel(CRect(135, 40, 220, 54), "DRY/WET",
                                         ui_::Palette::text(), 1);
    panel->addView(mixLabel);

    levelSlider_ = addSlider(panel, param::kOutputLevel, CRect(12, 66, 130, 86), true);
    auto* levelLabel = new ui_::MicroLabel(CRect(135, 70, 220, 84), "LEVEL dB",
                                           ui_::Palette::text(), 1);
    panel->addView(levelLabel);

    bypassButton_ = new ui_::PLButton(CRect(12, 96, 96, 120), this, param::kBypass, "BYPASS");
    panel->addView(bypassButton_);

    auto* note = new ui_::MicroLabel(CRect(112, 96, 265, 120), "bypass: latency-compensated",
                                     ui_::Palette::textFaint(), 0);
    panel->addView(note);
  }

  void buildMetersAndStatus(CViewContainer* root) {
    const CRect r(12, 296, 208, 396);
    auto* panel = new ui_::PanelView(r);
    root->addView(panel);

    auto* inLabel = new ui_::MicroLabel(CRect(12, 8, 100, 22), "METERS · IN",
                                        ui_::Palette::textFaint(), 0);
    panel->addView(inLabel);
    inMeters_ = new ui_::PLMeters(CRect(12, 28, 184, 54));
    panel->addView(inMeters_);

    auto* outLabel = new ui_::MicroLabel(CRect(12, 62, 100, 76), "METERS · OUT",
                                         ui_::Palette::textFaint(), 0);
    panel->addView(outLabel);
    outMeters_ = new ui_::PLMeters(CRect(12, 82, 184, 108));
    panel->addView(outMeters_);

    auto* hint = new ui_::MicroLabel(CRect(12, 116, 184, 130), "peak/rms · stereo",
                                     ui_::Palette::textFaint(), 0);
    panel->addView(hint);

    // status panel
    const CRect sr(220, 296, kEditorWidth - 12, 396);
    auto* statusPanel = new ui_::PanelView(sr);
    root->addView(statusPanel);

    auto* stLabel = new ui_::MicroLabel(CRect(12, 8, 300, 22), "RUNTIME STATUS (LIVE VALUES ONLY)",
                                        ui_::Palette::textFaint(), 0);
    statusPanel->addView(stLabel);

    statusLine1_ = new ui_::MicroLabel(CRect(12, 26, sr.getWidth() - 12, 42), "—",
                                       ui_::Palette::textDim(), 0);
    statusPanel->addView(statusLine1_);
    statusLine2_ = new ui_::MicroLabel(CRect(12, 44, sr.getWidth() - 12, 60), "—",
                                       ui_::Palette::textDim(), 0);
    statusPanel->addView(statusLine2_);
    statusLine3_ = new ui_::MicroLabel(CRect(12, 62, sr.getWidth() - 12, 78), "—",
                                       ui_::Palette::textDim(), 0);
    statusPanel->addView(statusLine3_);
    statusLine4_ = new ui_::MicroLabel(CRect(12, 80, sr.getWidth() - 12, 96), "—",
                                       ui_::Palette::textDim(), 0);
    statusPanel->addView(statusLine4_);

    auto* footer = new ui_::MicroLabel(
        CRect(12, 402, kEditorWidth - 12, 418),
        "PITCH LAB V0.1 · AGE-T · ENGINES: V0.1 RESEARCH REGISTRY (5) · NO QUALITY SCORE BY DESIGN",
        ui_::Palette::textFaint(), 0);
    root->addView(footer);
  }

  // ---- helpers ---------------------------------------------------------------
  ui_::PLSlider* addSlider(CViewContainer* parent, uint32_t tag, const CRect& r, bool accent) {
    const ParamMeta* meta = findMeta(tag);
    auto* slider = new ui_::PLSlider(r, this, static_cast<int32_t>(tag), meta, 0);
    slider->setAccent(accent);
    EditController* ec = getController();
    slider->setValueNormalized(ec != nullptr ? ec->getParamNormalized(tag) : 0.0);
    parent->addView(slider);
    sliderByTag_[tag] = slider;
    return slider;
  }

  static const ParamMeta* findMeta(uint32_t tag) {
    const ParamMeta* table = parameterTable();
    const uint32_t n = parameterCount();
    for (uint32_t i = 0; i < n; ++i) {
      if (table[i].tag == tag) return &table[i];
    }
    return nullptr;
  }

  static std::string shortEngineName(const char* id) {
    // "native.vardelay" -> "VARDELAY"; "native.pv.phaselocked" -> "PV-LOCKED"
    std::string s(id);
    if (s.rfind("native.", 0) == 0) s = s.substr(7);
    for (auto& ch : s) ch = static_cast<char>(::toupper(static_cast<unsigned char>(ch)));
    if (s == "PV.PHASELOCKED") s = "PV-LOCKED";
    if (s == "PV.CLASSIC") s = "PV-CLASSIC";
    return s;
  }

  void rebuildEnginePanelControls() {
    // remove the previous engine's sliders (values persist in the parameters).
    // removeView(view) FORGETS the view (withForget defaults true — the
    // VSTGUI contract): an explicit forget() here would double-release
    // (use-after-free; found by opening the real editor under Xvfb).
    for (ui_::PLSlider* s : engineSliders_) {
      // P0.3 (Task 24): the authoritative live-control collection must
      // contain ONLY live controls — the removed slider is FORGOTTEN
      // (freed) by removeView, so the stale entry in sliderByTag_ left
      // behind by the previous implementation was a use-after-free
      // dereferenced by the 33 ms polling timer (syncControlValues) until
      // the engine switched back to this engine's panel. NOTE: the tag is
      // read BEFORE the removal — removeView frees the view, so touching
      // it afterwards is exactly the use-after-free class this fix closes
      // (the first version of this fix read it after and crashed the
      // editor open under Xvfb — caught by the CI run).
      const uint32_t removedTag = static_cast<uint32_t>(s->getTag());
      enginePanel_->removeView(s);
      sliderByTag_.erase(removedTag);
    }
    for (ui_::MicroLabel* l : engineValues_) {
      enginePanel_->removeView(l);  // forgets (see note above)
    }
    engineSliders_.clear();
    engineValues_.clear();
    // P2.2 (Task 24): the varispeed adaptation note is lifecycle-managed —
    // previously a NEW note was created on every rebuild without removing
    // the old one, so repeated engine switches ACCUMULATED overlapping
    // notes (stale children, unbounded view growth).
    if (varispeedNote_ != nullptr) {
      enginePanel_->removeView(varispeedNote_);
      varispeedNote_ = nullptr;
    }
    if (getController() == nullptr) return;

    const ParamSnapshot snap = currentSnapshot();
    std::vector<uint32_t> tags;
    switch (snap.engineIndex) {
      case 0:  // native.varispeed
        tags = {param::kVsQuality, param::kVsAllowAliasing};
        panelTitle_->set("VARISPEED", ui_::Palette::amber());
        break;
      case 1:  // native.vardelay
        tags = {param::kVdExcursion, param::kVdCrossfade};
        panelTitle_->set("VARDELAY", ui_::Palette::textDim());
        break;
      // ENGINE-INDEX MAPPING FIX: the cases MUST follow the v0.1 REGISTRY
      // ORDER (engine_registry.cpp, §14-aligned): 0 varispeed, 1 vardelay,
      // 2 native.pv.classic, 3 native.pv.phaselocked, 4 native.granular.
      // The previous implementation assumed the implementation-chronology
      // order (granular, pv.classic, pv.phaselocked at 2/3/4) — the
      // SELECTOR LABELS and the AUDIO PATH were always correct
      // (engineIdForIndex + reg.at), so selecting the segment labelled
      // PV-CLASSIC ran pv.classic but showed the GRANULAR panel: the
      // engine-specific controls edited parameters the running engine
      // ignores (the "engine-specific controls do not behave as expected"
      // symptom, persisting after the write-through fix).
      case 2:  // native.pv.classic
        tags = {param::kPvcFft, param::kPvcHop};
        panelTitle_->set("PV-CLASSIC", ui_::Palette::textDim());
        break;
      case 3:  // native.pv.phaselocked
        tags = {param::kPvpFft, param::kPvpHop};
        panelTitle_->set("PV-LOCKED", ui_::Palette::textDim());
        break;
      case 4:  // native.granular
        tags = {param::kGrGrain, param::kGrOverlap, param::kGrJitter, param::kGrWindow};
        panelTitle_->set("GRANULAR", ui_::Palette::textDim());
        break;
      default:
        break;
    }
    CCoord y = 36.0;
    for (uint32_t tag : tags) {
      const ParamMeta* meta = findMeta(tag);
      if (meta == nullptr) continue;
      bool isDiscrete = meta->stepCount > 0;
      ui_::PLSlider* slider = nullptr;
      if (isDiscrete) {
        slider = addDiscreteSlider(enginePanel_, tag, CRect(12, y, 170, y + 20));
      } else {
        slider = addSliderTo(enginePanel_, tag, CRect(12, y, 170, y + 20));
      }
      if (slider != nullptr) {
        engineSliders_.push_back(slider);
        auto* valueLabel = new ui_::MicroLabel(CRect(178, y + 3, 190, y + 17), "",
                                               ui_::Palette::text(), 1);
        enginePanel_->addView(valueLabel);
        engineValues_.push_back(valueLabel);
      }
      y += 30.0;
    }
    if (snap.engineIndex == 0) {
      varispeedNote_ = new ui_::MicroLabel(
          CRect(12, 160, 190, 210),
          "RATE-FOLLOWING ENGINE: WINDOWED SPLICE ADAPTATION (0.2 s WINDOWS, "
          "15 ms SPLICES) — NOT THE OFFLINE RENDER",
          ui_::Palette::amber(), 0);
      enginePanel_->addView(varispeedNote_);
    }
  }

  ui_::PLSlider* addSliderTo(CViewContainer* parent, uint32_t tag, const CRect& r) {
    const ParamMeta* meta = findMeta(tag);
    auto* slider = new ui_::PLSlider(r, this, static_cast<int32_t>(tag), meta, 0);
    slider->setAccent(false);
    slider->setValueNormalized(getController()->getParamNormalized(tag));
    parent->addView(slider);
    sliderByTag_[tag] = slider;
    return slider;
  }

  ui_::PLSlider* addDiscreteSlider(CViewContainer* parent, uint32_t tag, const CRect& r) {
    // discrete engine parameters are also sliders with snapped values (the
    // model normalises with step rounding)
    return addSliderTo(parent, tag, r);
  }

  [[nodiscard]] ParamSnapshot currentSnapshot() const {
    ParamSnapshot snap;
    EditController* ec = const_cast<PitchLabEditor*>(this)->getController();
    if (ec == nullptr) return snap;
    const ParamMeta* table = parameterTable();
    const uint32_t n = parameterCount();
    for (uint32_t i = 0; i < n; ++i) {
      applyNormalised(snap, table[i].tag, ec->getParamNormalized(table[i].tag));
    }
    return snap;
  }

  // ---- polling ------------------------------------------------------------------
  void pollMetersAndStatus() {
    if (IPitchLabMeters* m = getInterface<IPitchLabMeters>(IPitchLabMeters::iid)) {
      const MetersSnapshot ms = m->getMeters();
      m->release();
      if (inMeters_ != nullptr) {
        // P2.4 (Task 24): the input panel shows the INPUT clip indicator —
        // previously the shared runtime clip state (fed by BOTH the input
        // and the output loop) was shown as "outClip" on both panels.
        inMeters_->setLevels(ms.inPeak[0], ms.inPeak[1], ms.inRms[0], ms.inRms[1],
                             ms.inClip[0], ms.inClip[1]);
      }
      if (outMeters_ != nullptr) {
        outMeters_->setLevels(ms.outPeak[0], ms.outPeak[1], ms.outRms[0], ms.outRms[1],
                              ms.outClip[0], ms.outClip[1]);
      }
    }
    if (IPitchLabStatus* st = getInterface<IPitchLabStatus>(IPitchLabStatus::iid)) {
      const StatusSnapshot ss = st->getStatus();
      st->release();
      updateStatusText(ss);
    }
  }

  void updateStatusText(const StatusSnapshot& ss) {
    // P1.11 (Task 24): the status snapshot is numeric-only (the audio thread
    // no longer formats strings — spec §4.3); ALL string derivation happens
    // here, on the UI thread, through the registry.
    char buf[160];
    const char* engineId = ss.engineIndex >= 0 ? engineIdForIndex(ss.engineIndex) : nullptr;
    std::snprintf(buf, sizeof(buf), "%s",
                  ss.chainReady ? (engineId != nullptr ? engineId : "?") : "building chain…");
    statusLine1_->set(buf, ui_::Palette::textDim());
    std::snprintf(buf, sizeof(buf), "fs %.0f Hz · %d ch · block %d · latency %lld fr (%.1f ms)",
                  ss.sampleRate, ss.channels, ss.maxBlock, (long long)ss.latencyFrames,
                  ss.sampleRate > 0.0 ? ss.latencyFrames / ss.sampleRate * 1000.0 : 0.0);
    statusLine2_->set(buf, ui_::Palette::textFaint());
    std::snprintf(buf, sizeof(buf),
                  "jobs %d · re-prepares %llu · clamps %llu · faults %llu · auto-drop %llu",
                  ss.liveJobs, (unsigned long long)ss.reprepares,
                  (unsigned long long)ss.clampEvents, (unsigned long long)ss.faults,
                  (unsigned long long)ss.automationDropped);
    statusLine3_->set(buf, ss.faults > 0 ? ui_::Palette::rose() : ui_::Palette::textFaint());
    std::snprintf(buf, sizeof(buf), "envelope [%.4f, %.4f] %s", ss.envelopeMin, ss.envelopeMax,
                  ss.bypassActive ? "· BYPASSED" : "");
    statusLine4_->set(buf, ui_::Palette::textFaint());

    // header
    const CColor adaptColor = ss.spliceMode ? ui_::Palette::amber() : ui_::Palette::accent();
    adaptation_->set(ss.chainReady ? (ss.spliceMode ? "WINDOWED SPLICE ADAPTATION"
                                                    : "CONTINUOUS REALTIME")
                                   : "—",
                     adaptColor);
    const char* engineName = ss.engineIndex >= 0 ? engineNameForIndex(ss.engineIndex) : nullptr;
    std::snprintf(buf, sizeof(buf), "%s", ss.chainReady ? (engineName != nullptr ? engineName : "—")
                                                         : "—");
    engineName_->set(buf, ui_::Palette::textDim());
    std::snprintf(buf, sizeof(buf), "LATENCY %.1f ms",
                  ss.sampleRate > 0.0 ? ss.latencyFrames / ss.sampleRate * 1000.0 : 0.0);
    latency_->set(buf, ui_::Palette::textDim());
    std::snprintf(buf, sizeof(buf), "%.1f kHz · %d CH · BLOCK %d", ss.sampleRate / 1000.0,
                  ss.channels, ss.maxBlock);
    format_->set(buf, ui_::Palette::textFaint());
  }

  void syncControlValues() {
    EditController* ec = getController();
    if (ec == nullptr) return;
    // reflect authoritative values (host automation etc.)
    for (auto& [tag, slider] : sliderByTag_) {
      const double v = ec->getParamNormalized(tag);
      if (std::fabs(v - slider->getValueNormalized()) > 1e-6) {
        slider->setValueNormalized(v);
        slider->invalid();
      }
    }
    if (engineSelector_ != nullptr) {
      const double v = ec->getParamNormalized(param::kEngine);
      if (std::fabs(v - engineSelector_->getValueNormalized()) > 1e-6) {
        engineSelector_->setValueNormalized(v);
        engineSelector_->invalid();
        rebuildEnginePanelControls();  // engine switch -> panel rebuild
      }
    }
    if (bypassButton_ != nullptr) {
      const double v = ec->getParamNormalized(param::kBypass);
      if (std::fabs(v - bypassButton_->getValueNormalized()) > 1e-6) {
        bypassButton_->setValueNormalized(v);
        bypassButton_->invalid();
      }
    }
    updateValueLabels();
  }

  void updateValueLabels() {
    const ParamSnapshot snap = currentSnapshot();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%+.2f st", snap.pitchSt);
    pitchValue_->set(buf, ui_::Palette::accent());
    std::snprintf(buf, sizeof(buf), "%.2f Hz", snap.lfoRateHz);
    lfoRateValue_->set(buf, ui_::Palette::text());
    std::snprintf(buf, sizeof(buf), "%.2f st", snap.lfoDepthSt);
    lfoDepthValue_->set(buf, ui_::Palette::text());

    // engine panel value labels (in slider order)
    const std::vector<std::string> texts = engineParamTexts(snap);
    for (std::size_t i = 0; i < engineValues_.size() && i < texts.size(); ++i) {
      engineValues_[i]->set(texts[i].c_str(), ui_::Palette::text());
    }
  }

  [[nodiscard]] static std::vector<std::string> engineParamTexts(const ParamSnapshot& s) {
    char buf[48];
    std::vector<std::string> out;
    switch (s.engineIndex) {
      case 0:
        out.push_back(kVsQualityNames[s.vsQuality]);
        out.push_back(s.vsAllowAliasing ? "on" : "off");
        break;
      case 1:
        std::snprintf(buf, sizeof(buf), "%.2f s", s.vdExcursionSec);
        out.push_back(buf);
        std::snprintf(buf, sizeof(buf), "%lld", (long long)s.vdCrossfadeFrames);
        out.push_back(buf);
        break;
      case 2:  // native.pv.classic (registry order — see the panel fix above)
        std::snprintf(buf, sizeof(buf), "%d", s.pvcFftSize);
        out.push_back(buf);
        std::snprintf(buf, sizeof(buf), "%d", s.pvcHop);
        out.push_back(buf);
        break;
      case 3:  // native.pv.phaselocked
        std::snprintf(buf, sizeof(buf), "%d", s.pvpFftSize);
        out.push_back(buf);
        std::snprintf(buf, sizeof(buf), "%d", s.pvpHop);
        out.push_back(buf);
        break;
      case 4:  // native.granular
        std::snprintf(buf, sizeof(buf), "%.3f s", s.grGrainSec);
        out.push_back(buf);
        std::snprintf(buf, sizeof(buf), "%lld", (long long)s.grOverlap);
        out.push_back(buf);
        std::snprintf(buf, sizeof(buf), "%lld", (long long)s.grJitterFrames);
        out.push_back(buf);
        out.push_back(s.grWindowTriangular ? "tri" : "hann");
        break;
      default:
        break;
    }
    return out;
  }

  void onUiStateChanged() {
    // a control changed (e.g. engine) — rebuild dependent panels.
    // P2.3 (Task 24): the last-engine state is INSTANCE-OWNED — the previous
    // function-static was shared across ALL editor instances (incorrect
    // ownership: instance A's switch would silently "fulfill" instance B's
    // rebuild).
    if (getController() == nullptr || enginePanel_ == nullptr) return;
    const ParamSnapshot snap = currentSnapshot();
    if (snap.engineIndex != lastEngineIndex_) {
      lastEngineIndex_ = snap.engineIndex;
      rebuildEnginePanelControls();
      updateValueLabels();
    }
  }

  // ---- members ------------------------------------------------------------
  std::vector<ui_::PLSlider*> engineSliders_;
  std::vector<ui_::MicroLabel*> engineValues_;
  std::map<uint32_t, ui_::PLSlider*> sliderByTag_;  // AUTHORITATIVE live-control
                                                    // collection (P0.3: only live
                                                    // controls, ever)
  ui_::MicroLabel* varispeedNote_ = nullptr;  // lifecycle-managed (P2.2)
  int lastEngineIndex_ = -1;                  // instance-owned (P2.3)

  ui_::PLSegmented* engineSelector_ = nullptr;
  ui_::PLSlider* pitchSlider_ = nullptr;
  ui_::PLSlider* lfoRateSlider_ = nullptr;
  ui_::PLSlider* lfoDepthSlider_ = nullptr;
  ui_::PLSlider* mixSlider_ = nullptr;
  ui_::PLSlider* levelSlider_ = nullptr;
  ui_::PLButton* bypassButton_ = nullptr;
  ui_::PLMeters* inMeters_ = nullptr;
  ui_::PLMeters* outMeters_ = nullptr;
  ui_::MicroLabel* engineName_ = nullptr;
  ui_::MicroLabel* adaptation_ = nullptr;
  ui_::MicroLabel* format_ = nullptr;
  ui_::MicroLabel* latency_ = nullptr;
  ui_::MicroLabel* panelTitle_ = nullptr;
  ui_::MicroLabel* pitchValue_ = nullptr;
  ui_::MicroLabel* lfoRateValue_ = nullptr;
  ui_::MicroLabel* lfoDepthValue_ = nullptr;
  ui_::MicroLabel* statusLine1_ = nullptr;
  ui_::MicroLabel* statusLine2_ = nullptr;
  ui_::MicroLabel* statusLine3_ = nullptr;
  ui_::MicroLabel* statusLine4_ = nullptr;
  CViewContainer* enginePanel_ = nullptr;
};

}  // namespace

IPlugView* createPitchLabEditor(IEditController* controller) {
  if (auto* ec = dynamic_cast<EditController*>(controller)) {
    return static_cast<IPlugView*>(new PitchLabEditor(ec));
  }
  return nullptr;
}

}  // namespace pitchlab::vst

#else  // !PITCHLAB_WITH_VSTGUI (headless build: no editor, honest nullptr)

namespace pitchlab::vst {

Steinberg::IPlugView* createPitchLabEditor(Steinberg::Vst::IEditController* /*controller*/) {
  return nullptr;
}

}  // namespace pitchlab::vst

#endif  // PITCHLAB_WITH_VSTGUI

