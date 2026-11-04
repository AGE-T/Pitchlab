// ---------------------------------------------------------------------------
// OCTAVOX native core -- DETERMINISTIC DUAL-MODULE VST3 NULL TEST (R19-P3b)
// ============================================================================
// PROVENANCE: this tool tests OUR OWN independently developed harmonizer
// plugin bundles (PitchLab_Lagrange6_Balanced.vst3 /
// PitchLab_ObsKernel6_Balanced.vst3 -- the R21 product identity; the class
// FUIDs are UNCHANGED from the R18-R20 OctavoxRE builds, built
// from native/vst3 around the octavox native DSP core).  It is NOT a
// reproduction of, and contains no code from, the original Eventide Octavox
// plug-in; the original binary was never executed.
//
// WHAT THIS TOOL PROVES: the two separately-compiled module bundles really
// select DIFFERENT interpolation kernels.  Both bundles are loaded
// SIMULTANEOUSLY into this ONE process -- two VST3::Hosting::Module::create
// calls, i.e. two real dlopen()s of distinct .so files that export IDENTICAL
// symbol names (GetPluginFactory / ModuleEntry / ModuleExit).  The pinned
// SDK's module_linux.cpp dlopens with RTLD_LAZY only (no RTLD_GLOBAL), which
// on glibc means the default RTLD_LOCAL scope: neither library's symbols
// enter the global symbol namespace, and each Module resolves its own
// GetPluginFactory through its own dlopen handle -- so the two factories
// coexist and never shadow each other.  (This is additionally proven at
// runtime below: after BOTH dlopens, both live factories are re-probed and
// must still answer with their own distinct class IDs.)
//
// One processor instance is created from EACH factory (queried by the
// variant-specific processor class FUID), both are initialized with the same
// SDK HostApplication context, and both are driven IN LOCKSTEP with
// byte-identical input blocks and byte-identical parameter queues.  Outputs
// are compared sample-for-sample over the analysis region:
//   * unity_exact (0 st): the core's parked integer-tap path is engaged in
//     both modules -- the outputs must be BIT-IDENTICAL;
//   * non-unity (+4 st / -24 st): the interpolation kernels are engaged --
//     the outputs MUST differ; a bit-identical non-unity result means the two
//     builds did NOT actually select different kernels: BUILD-CONFIGURATION
//     DEFECT (flagged loudly, exit code 1).
//
// This tool is deliberately NOT variant-compiled: it includes NO wrapper
// header (octavox_params.h / octavox_ids.h pull the build configuration) --
// both expected processor class FUIDs are hardcoded below, so one binary can
// verify either/both module builds.  Parameter normalization is computed
// directly from the published parameter domains (kPitch [-24,+24] step 0.01,
// kVoices [1,8] step 1, kSpread bool, kFeedback [0,0.9], kWetDry [0,1],
// kBypass bool -- see octavox_params.cpp's kSpecs table).
//
// Honest scope label: SDK module loader + factory contract + audio null
// comparison in one offline process.  NOT a DAW compatibility claim.
// ---------------------------------------------------------------------------
#include "wav_io.hpp"

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/funknown.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

//==========================================================================
// fixed test constants (the deterministic stimulus, identical for both
// modules -- the null-test contract)
//==========================================================================
namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kDurationSeconds = 3;
constexpr int kTotalSamples = static_cast<int>(kSampleRate) * kDurationSeconds;  // 144000
constexpr int kBlockSize = 128;       // lockstep process() block size throughout
constexpr int kMaxBlock = 4096;       // ProcessSetup::maxSamplesPerBlock
constexpr int kAnalysisBegin = 2304 + 4096;  // startup/latency + one grain = 6400
constexpr int kBestShiftRange = 8;    // best_shift search window [+/-8]

// -- parameter ids of the wrapper's global surface (octavox_params.h layout;
//    hardcoded: this tool must not include the variant-compiled headers) ----
constexpr uint32_t kPidBypass = 0;    // bool
constexpr uint32_t kPidPitch = 1;     // semitones [-24,+24] step 0.01
constexpr uint32_t kPidVoices = 2;    // integer 1..8 (step 1)
constexpr uint32_t kPidSpread = 3;    // bool
constexpr uint32_t kPidFeedback = 4;  // [0,0.9] continuous
constexpr uint32_t kPidWetDry = 5;    // [0,1] continuous

// -- the two variant processor class FUIDs (hardcoded; the Lagrange6 variant
//    keeps the R16-R18 lineage identity, the ObsKernel6 variant carries the
//    deterministically minted one -- see octavox_build_config.h) -----------
//    Lagrange6 : {6F4E6258-9A31-42E1-8F3C-5A971D2B4E60}
constexpr uint32_t kLag6L1 = 0x6F4E6258u, kLag6L2 = 0x9A3142E1u;
constexpr uint32_t kLag6L3 = 0x8F3C5A97u, kLag6L4 = 0x1D2B4E60u;
//    ObsKernel6: {E3E7FA70-6A38-0D35-B05F-64B91F1E2689}
constexpr uint32_t kObs6L1 = 0xE3E7FA70u, kObs6L2 = 0x6A380D35u;
constexpr uint32_t kObs6L3 = 0xB05F64B9u, kObs6L4 = 0x1F1E2689u;

const VST3::UID kCidLagrange6{kLag6L1, kLag6L2, kLag6L3, kLag6L4};
const VST3::UID kCidObsKernel6{kObs6L1, kObs6L2, kObs6L3, kObs6L4};

std::string uidDashed(uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4) {
    char b[48];
    std::snprintf(b, sizeof b, "{%08X-%04X-%04X-%04X-%04X%08X}", l1,
                  (l2 >> 16) & 0xFFFFu, l2 & 0xFFFFu, (l3 >> 16) & 0xFFFFu,
                  l3 & 0xFFFFu, l4);
    return std::string(b);
}

//==========================================================================
// the test cases
//==========================================================================
struct CaseDef {
    const char* name;
    double pitchSt;
    bool expectBitIdentical;
    const char* expectLabel;
};

const CaseDef kAllCases[] = {
    {"unity_exact", 0.0, true, "bit-identical"},
    {"moderate_shift_plus400c", 4.0, false, "different"},
    {"shift_minus24st", -24.0, false, "different"},
};

//==========================================================================
// deterministic stimulus (identical for both modules)
//==========================================================================
// LCG per the project's deterministic-input spec:
//   s = s*6364136223846793005 + 1442695040888963407 (uint64 wraparound)
//   d = ((s >> 11) & (2^53 - 1)) / 2^53   in [0,1)
//   noise = (2d - 1) * amp
struct NoiseLcg {
    uint64_t s;
    explicit NoiseLcg(uint64_t seed) : s(seed ? seed : 1) {}
    double next01() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t r = (s >> 11) & ((1ULL << 53) - 1ULL);
        return static_cast<double>(r) / 9007199254740992.0;  // 2^53
    }
};

// 3.0 s composite: 220 Hz * 0.35 + 330 Hz * 0.2 + LCG noise (seed 987654321) * 0.05
void makeInput(std::vector<float>& in) {
    in.assign(static_cast<size_t>(kTotalSamples), 0.0f);
    NoiseLcg rng(987654321ULL);
    const double k2pi = 6.283185307179586476925286766559;
    for (int k = 0; k < kTotalSamples; ++k) {
        const double t = static_cast<double>(k) / kSampleRate;
        const double tone = 0.35 * std::sin(k2pi * 220.0 * t) +
                            0.20 * std::sin(k2pi * 330.0 * t);
        const double noise = (2.0 * rng.next01() - 1.0) * 0.05;
        in[static_cast<size_t>(k)] = static_cast<float>(tone + noise);
    }
}

//==========================================================================
// small helpers
//==========================================================================
std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if ((unsigned char)ch < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(ch));
            o += b;
        } else o += ch;
    }
    return o;
}

// JSON-safe number: finite -> number token, else a quoted "inf"/"-inf"/"nan".
std::string jsonNum(double v) {
    if (std::isfinite(v)) {
        char b[40];
        std::snprintf(b, sizeof b, "%.10g", v);
        return std::string(b);
    }
    if (std::isnan(v)) return "\"nan\"";
    return v > 0 ? "\"inf\"" : "\"-inf\"";
}

bool fileExists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

std::string defaultModulePath(const char* argv0, const std::string& bundle) {
    std::string exe = argv0 ? argv0 : "";
    const size_t slash = exe.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
    const std::string cands[] = {
        "/../../VST3/Release/" + bundle,
        "/../../../VST3/Release/" + bundle,
        "/" + bundle,
    };
    for (const std::string& suff : cands) {
        const std::string p = dir + suff;
        if (fileExists(p)) return p;
    }
    const std::string rel[] = {
        "build/VST3/Release/" + bundle,
        "VST3/Release/" + bundle,
    };
    for (const std::string& p : rel)
        if (fileExists(p)) return p;
    return "";
}

//==========================================================================
// one loaded module + its processor instance (the "host side" of one variant)
//==========================================================================
struct ModuleSession {
    VST3::Hosting::Module::Ptr module;      // real dlopen handle (dlclose in dtor)
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> audio;
    IPtr<IEditController> edit;
    std::string path;

    // factory identity (from the LIVE factory, after dlopen)
    int factoryClassCount = -1;
    std::string className, classCategory, classVersion, classIdHex;
    bool loaded = false;          // Module::create succeeded (dlopen happened)
    bool identityOk = false;       // exactly one Audio Module Class == expected CID
    bool residencyProbeOk = false; // re-probe of the LIVE factory after BOTH dlopens
    std::string loadError;

    // contract values (informational)
    int32_t audioInBuses = -1, audioOutBuses = -1, paramCount = -1;
    uint32_t latency = 0;
    bool instantiated = false;     // component created + initialized + setup done
};

// scan a LIVE factory for its Audio Module Class and check it against the
// expected variant CID (and make sure the OTHER variant's CID is absent)
bool scanFactoryIdentity(ModuleSession& m, const VST3::UID& expectedCid,
                         const VST3::UID& otherCid) {
    if (!m.module) return false;
    const auto& factory = m.module->getFactory();
    if (!factory.get()) return false;
    const auto infos = factory.classInfos();
    m.factoryClassCount = static_cast<int>(infos.size());
    int audioClasses = 0;
    bool foundExpected = false, foundOther = false;
    for (const auto& ci : infos) {
        if (ci.category().find("Audio Module Class") == std::string::npos) continue;
        ++audioClasses;
        m.className = ci.name();
        m.classCategory = ci.category();
        m.classVersion = ci.version();
        m.classIdHex = ci.ID().toString();
        if (ci.ID() == expectedCid) foundExpected = true;
        if (ci.ID() == otherCid) foundOther = true;
    }
    // exactly one class in total, exactly one Audio Module Class, its ID is
    // THIS variant's processor CID, and the other variant's CID is absent.
    return m.factoryClassCount == 1 && audioClasses == 1 && foundExpected &&
           !foundOther;
}

// load the bundle (real dlopen) + verify identity + create the instance
bool loadAndVerify(ModuleSession& m, const std::string& path,
                   const VST3::UID& expectedCid, const VST3::UID& otherCid,
                   HostApplication* hostCtx) {
    m.path = path;
    std::string err;
    m.module = VST3::Hosting::Module::create(path, err);
    if (!m.module) {
        m.loadError = "Module::create failed: " + err;
        return false;
    }
    m.loaded = true;
    m.identityOk = scanFactoryIdentity(m, expectedCid, otherCid);
    if (!m.identityOk) {
        m.loadError = "factory identity check failed (class list/CID mismatch)";
        return false;
    }
    // create ONE processor instance through the factory, by the expected CID
    m.component = m.module->getFactory().createInstance<IComponent>(expectedCid);
    if (!m.component) {
        m.loadError = "createInstance(IComponent) by the expected CID failed";
        return false;
    }
    {
        IAudioProcessor* ap = nullptr;
        if (m.component->queryInterface(IAudioProcessor::iid,
                                        reinterpret_cast<void**>(&ap)) == kResultTrue &&
            ap)
            m.audio = IPtr<IAudioProcessor>(ap);  // adopts the add-ref'd pointer
    }
    if (!m.audio) { m.loadError = "component does not export IAudioProcessor"; return false; }
    {
        IEditController* ec = nullptr;
        if (m.component->queryInterface(IEditController::iid,
                                        reinterpret_cast<void**>(&ec)) == kResultTrue &&
            ec)
            m.edit = IPtr<IEditController>(ec);
    }
    if (m.component->initialize(hostCtx) != kResultOk) {
        m.loadError = "initialize(host context) failed";
        return false;
    }
    m.audioInBuses =
        m.component->getBusCount(MediaTypes::kAudio, BusDirections::kInput);
    m.audioOutBuses =
        m.component->getBusCount(MediaTypes::kAudio, BusDirections::kOutput);
    if (m.edit) m.paramCount = m.edit->getParameterCount();
    // mono bus + 48k/4096 processing setup (full standard lifecycle)
    SpeakerArrangement mono = SpeakerArr::kMono;
    if (m.audio->setBusArrangements(&mono, 1, &mono, 1) != kResultOk) {
        m.loadError = "mono bus negotiation rejected";
        return false;
    }
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = kSampleRate;
    ps.maxSamplesPerBlock = kMaxBlock;
    if (m.audio->setupProcessing(ps) != kResultOk) {
        m.loadError = "setupProcessing failed";
        return false;
    }
    // the wrapper prepares the engine (and reports its real latency) only
    // while ACTIVE -- do a probe activation to read the module's latency,
    // then deactivate again (the proven activation-cycle pattern from the
    // module integration harness)
    if (m.component->setActive(true) == kResultOk) {
        if (m.audio->setProcessing(true) != kResultOk) {
            m.component->setActive(false);
            m.loadError = "probe activation failed (setProcessing)";
            return false;
        }
        m.latency = m.audio->getLatencySamples();
        m.audio->setProcessing(false);
        m.component->setActive(false);
    } else {
        m.loadError = "probe activation failed (setActive)";
        return false;
    }
    m.instantiated = true;
    return true;
}

// full activation for one case (the module harness's proven reset pattern:
// re-negotiate + re-setup re-prepares the engine, so every case starts from
// a fresh engine state)
bool setupForRun(ModuleSession& m) {
    if (!m.audio || !m.component) return false;
    SpeakerArrangement mono = SpeakerArr::kMono;
    if (m.audio->setBusArrangements(&mono, 1, &mono, 1) != kResultOk) return false;
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = kSampleRate;
    ps.maxSamplesPerBlock = kMaxBlock;
    if (m.audio->setupProcessing(ps) != kResultOk) return false;
    if (m.component->setActive(true) != kResultOk) return false;
    return m.audio->setProcessing(true) == kResultOk;
}

void teardownRun(ModuleSession& m) {
    if (m.audio) m.audio->setProcessing(false);
    if (m.component) m.component->setActive(false);
}

// one process() call through the loaded module (planar mono, separate in/out)
void driveBlock(ModuleSession& m, const float* in, float* out, int n,
                IParameterChanges* changes) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = 1;
    ib.channelBuffers32 = const_cast<float**>(&in);  // pointer to the one channel
    ob.numChannels = 1;
    ob.channelBuffers32 = &out;
    AudioBusBuffers inB[1] = {ib}, outB[1] = {ob};
    ProcessData pd{};
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inB;
    pd.numInputs = 1;
    pd.outputs = outB;
    pd.numOutputs = 1;
    pd.inputParameterChanges = changes;
    m.audio->process(pd);
}

// parameter events at stream position 0 (the module harness's queue pattern)
void addEvent(ParameterChanges& changes, uint32_t id, int32_t offset, double norm) {
    int32_t idx = 0;
    IParamValueQueue* q = changes.addParameterData(id, idx);
    if (q) {
        int32_t pidx = 0;
        q->addPoint(offset, norm, pidx);
    }
}

// the common surface (identical for both instances, all cases): voices 1,
// spread OFF, feedback 0, wet/dry 1 (all-wet), bypass OFF; the pitch is the
// per-case variable.  Normalized values computed directly from the published
// parameter domains (step-aligned, so the module's quantizing round-trip is
// exact -- verified against octavox_params.cpp's conversion code).
void addParamEvents(ParameterChanges& ch, double pitchSt) {
    addEvent(ch, kPidBypass, 0, 0.0);                    // bypass OFF
    addEvent(ch, kPidPitch, 0, (pitchSt + 24.0) / 48.0);  // [-24,+24] span 48
    addEvent(ch, kPidVoices, 0, 0.0);                   // (1-1)/7 = 1 voice
    addEvent(ch, kPidSpread, 0, 0.0);                   // spread OFF
    addEvent(ch, kPidFeedback, 0, 0.0);                 // 0 / 0.9 = 0
    addEvent(ch, kPidWetDry, 0, 1.0);                   // all-wet (already [0,1])
}

// proper teardown: terminate + release every interface, then dlclose
void shutdownInstance(ModuleSession& m) {
    teardownRun(m);
    if (m.component && m.instantiated) m.component->terminate();
    m.edit = nullptr;
    m.audio = nullptr;
    m.component = nullptr;
    m.module = nullptr;  // ~Module -> ModuleExit + dlclose
}

//==========================================================================
// per-case results
//==========================================================================
struct CaseResult {
    std::string name;
    double pitchSt = 0.0;
    bool expectBitIdentical = false;
    std::string expectLabel;
    bool ran = false;
    bool pass = false;
    bool buildDefect = false;  // non-unity case came out bit-identical
    std::string note;

    // metrics over the analysis region k >= kAnalysisBegin
    double maxAbsDiff = 0.0, rmsDiff = 0.0, rmsA = 0.0, rmsB = 0.0;
    double relDiffDb = 0.0, corr = 0.0, bitIdenticalFrac = 0.0;
    double bestShift = 0.0, bestShiftMaxDiff = 0.0;
    bool fullStreamBitIdentical = false;
    uint32_t latencyA = 0, latencyB = 0;
    bool latencyEqual = false;

    std::string wavA, wavB, wavDiff;
};

void computeMetrics(CaseResult& r, const std::vector<float>& a,
                    const std::vector<float>& b) {
    const long long n = kTotalSamples - kAnalysisBegin;
    double sumDiff2 = 0.0, maxD = 0.0;
    double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
    long long bitEq = 0;
    for (int k = kAnalysisBegin; k < kTotalSamples; ++k) {
        const double av = a[static_cast<size_t>(k)];
        const double bv = b[static_cast<size_t>(k)];
        const double d = av - bv;
        const double ad = std::fabs(d);
        if (ad > maxD) maxD = ad;
        sumDiff2 += d * d;
        sa += av;
        sb += bv;
        saa += av * av;
        sbb += bv * bv;
        sab += av * bv;
        if (std::memcmp(&a[static_cast<size_t>(k)], &b[static_cast<size_t>(k)],
                        sizeof(float)) == 0)
            ++bitEq;
    }
    r.maxAbsDiff = maxD;
    r.rmsDiff = std::sqrt(sumDiff2 / static_cast<double>(n));
    r.rmsA = std::sqrt(saa / static_cast<double>(n));
    r.rmsB = std::sqrt(sbb / static_cast<double>(n));
    if (r.rmsA > 0.0)
        r.relDiffDb = 20.0 * std::log10(r.rmsDiff / r.rmsA);
    else
        r.relDiffDb = (r.rmsDiff == 0.0) ? -HUGE_VAL : HUGE_VAL;
    const double ma = sa / static_cast<double>(n), mb = sb / static_cast<double>(n);
    const double va = saa / static_cast<double>(n) - ma * ma;
    const double vb = sbb / static_cast<double>(n) - mb * mb;
    const double cov = sab / static_cast<double>(n) - ma * mb;
    r.corr = (va > 0.0 && vb > 0.0)
                 ? cov / std::sqrt(va * vb)
                 : ((va == 0.0 && vb == 0.0) ? 1.0 : 0.0);
    r.bitIdenticalFrac = static_cast<double>(bitEq) / static_cast<double>(n);
    r.fullStreamBitIdentical =
        std::memcmp(a.data(), b.data(), static_cast<size_t>(kTotalSamples) * sizeof(float)) == 0;

    // best alignment shift in [-kBestShiftRange, +kBestShiftRange] minimizing
    // max_abs_diff (ties resolve to the smallest |shift|)
    double best = HUGE_VAL;
    int bestS = 0;
    for (int i = 0; i <= kBestShiftRange; ++i) {
        for (int ci = 0; ci < 2; ++ci) {
            const int s = (i == 0) ? 0 : (ci == 0 ? i : -i);
            if (i == 0 && ci == 1) continue;
            const int lo = kAnalysisBegin + (s < 0 ? -s : 0);
            const int hi = kTotalSamples - 1 - (s > 0 ? s : 0);
            double mx = 0.0;
            for (int k = lo; k <= hi; ++k) {
                const double d = std::fabs(static_cast<double>(a[static_cast<size_t>(k)]) -
                                           static_cast<double>(b[static_cast<size_t>(k + s)]));
                if (d > mx) mx = d;
            }
            if (mx < best) { best = mx; bestS = s; }
        }
    }
    r.bestShift = bestS;
    r.bestShiftMaxDiff = best;
}

void judgeCase(CaseResult& r) {
    const bool bitIdenticalStream =
        (r.maxAbsDiff == 0.0) && (r.bitIdenticalFrac >= 1.0);
    if (r.expectBitIdentical) {
        if (!bitIdenticalStream)
            r.note = "unity case is NOT bit-identical (parked integer-tap path "
                     "expectation violated)";
        if (!r.latencyEqual) {
            if (!r.note.empty()) r.note += "; ";
            r.note += "latency mismatch between the two modules";
        }
        r.pass = bitIdenticalStream && r.latencyEqual;
    } else {
        if (bitIdenticalStream) {
            r.buildDefect = true;
            r.note = "BUILD-CONFIGURATION DEFECT: non-unity output is BIT-IDENTICAL "
                     "across the two module builds -- they did NOT select different "
                     "interpolation kernels";
        }
        if (!r.latencyEqual) {
            if (!r.note.empty()) r.note += "; ";
            r.note += "latency mismatch between the two modules";
        }
        r.pass = (!bitIdenticalStream) && r.latencyEqual;
    }
}

CaseResult runCase(const CaseDef& cd, ModuleSession& A, ModuleSession& B,
                   const std::vector<float>& in, const std::string& outDir) {
    CaseResult r;
    r.name = cd.name;
    r.pitchSt = cd.pitchSt;
    r.expectBitIdentical = cd.expectBitIdentical;
    r.expectLabel = cd.expectLabel;
    if (!A.instantiated || !B.instantiated) {
        r.note = "module setup incomplete";
        return r;
    }
    // per-case fresh activation on BOTH instances (fresh engine state)
    if (!setupForRun(A) || !setupForRun(B)) {
        teardownRun(A);
        teardownRun(B);
        r.note = "activation failed";
        return r;
    }
    r.latencyA = A.audio->getLatencySamples();
    r.latencyB = B.audio->getLatencySamples();
    r.latencyEqual = (r.latencyA == r.latencyB);

    // identical parameter queues, one per instance (two independent hosts)
    ParameterChanges chA, chB;
    addParamEvents(chA, cd.pitchSt);
    addParamEvents(chB, cd.pitchSt);

    std::vector<float> outA(static_cast<size_t>(kTotalSamples), 0.0f);
    std::vector<float> outB(static_cast<size_t>(kTotalSamples), 0.0f);
    std::vector<float> blockIn(static_cast<size_t>(kBlockSize));
    // LOCKSTEP: block-by-block, instance A then instance B, each getting its
    // OWN copy of the identical input block (a write into the input buffer by
    // one module can never contaminate the other's stimulus)
    for (int off = 0; off < kTotalSamples; off += kBlockSize) {
        const int n = std::min(kBlockSize, kTotalSamples - off);
        std::memcpy(blockIn.data(), in.data() + off, static_cast<size_t>(n) * sizeof(float));
        driveBlock(A, blockIn.data(), outA.data() + off, n, off == 0 ? &chA : nullptr);
        std::memcpy(blockIn.data(), in.data() + off, static_cast<size_t>(n) * sizeof(float));
        driveBlock(B, blockIn.data(), outB.data() + off, n, off == 0 ? &chB : nullptr);
    }
    teardownRun(A);
    teardownRun(B);
    r.ran = true;

    computeMetrics(r, outA, outB);
    judgeCase(r);

    // 32-bit float WAVs: both module outputs + the sample-for-sample diff
    r.wavA = outDir + "/" + r.name + "_lagrange6.wav";
    r.wavB = outDir + "/" + r.name + "_obskernel6.wav";
    r.wavDiff = outDir + "/" + r.name + "_diff.wav";
    try {
        writeWavF32(r.wavA, outA.data(), kTotalSamples, 1, static_cast<int>(kSampleRate));
        writeWavF32(r.wavB, outB.data(), kTotalSamples, 1, static_cast<int>(kSampleRate));
        std::vector<float> diff(static_cast<size_t>(kTotalSamples));
        for (int k = 0; k < kTotalSamples; ++k)
            diff[static_cast<size_t>(k)] =
                outA[static_cast<size_t>(k)] - outB[static_cast<size_t>(k)];
        writeWavF32(r.wavDiff, diff.data(), kTotalSamples, 1, static_cast<int>(kSampleRate));
    } catch (const std::exception& e) {
        if (!r.note.empty()) r.note += "; ";
        r.note += std::string("wav write failed: ") + e.what();
        r.pass = false;
    }
    return r;
}

//==========================================================================
// environment
//==========================================================================
std::string osName() {
#if defined(__linux__)
    struct utsname u;
    if (uname(&u) == 0) return std::string(u.sysname) + " " + u.release;
#endif
    return "unknown";
}

std::string cpuName() {
#if defined(__linux__)
    struct utsname u;
    if (uname(&u) == 0) return std::string(u.machine);
#endif
    return "unknown";
}

int coreCount() {
#if defined(__linux__)
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return static_cast<int>(n);
#endif
    return 0;
}

//==========================================================================
// JSON report
//==========================================================================
void writeJson(const std::string& path, const ModuleSession& lag,
               const ModuleSession& obs, const std::vector<CaseResult>& cases,
               bool overallPass) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"tool\": \"octavox_dual_nulltest\",\n");
    std::fprintf(f, "  \"tool_round\": \"R19-P3b\",\n");
    std::fprintf(f, "  \"environment\": {\"os\": \"%s\", \"cpu\": \"%s\", \"cores\": %d},\n",
                 jsonEscape(osName()).c_str(), jsonEscape(cpuName()).c_str(), coreCount());
    std::fprintf(f, "  \"modules\": {\n");
    const char* keys[2] = {"lagrange6", "obskernel6"};
    const ModuleSession* mods[2] = {&lag, &obs};
    for (int i = 0; i < 2; ++i) {
        const ModuleSession& m = *mods[i];
        std::fprintf(f, "    \"%s\": {\"path\": \"%s\", \"loaded\": %s, "
                        "\"factory_class_id_ok\": %s, \"factory_class_count\": %d, "
                        "\"class_name\": \"%s\", \"class_category\": \"%s\", "
                        "\"class_id\": \"%s\", \"version\": \"%s\", "
                        "\"simultaneous_residency_probe\": %s, "
                        "\"parameter_count\": %d, \"latency_samples\": %u, "
                        "\"load_error\": \"%s\"}%s\n",
                     keys[i], jsonEscape(m.path).c_str(),
                     m.loaded ? "true" : "false", m.identityOk ? "true" : "false",
                     m.factoryClassCount, jsonEscape(m.className).c_str(),
                     jsonEscape(m.classCategory).c_str(), jsonEscape(m.classIdHex).c_str(),
                     jsonEscape(m.classVersion).c_str(),
                     m.residencyProbeOk ? "true" : "false",
                     static_cast<int>(m.paramCount),
                     static_cast<unsigned>(m.latency),
                     jsonEscape(m.loadError).c_str(), i == 0 ? "," : "");
    }
    std::fprintf(f, "  },\n");
    std::fprintf(f, "  \"signal\": {\"sample_rate\": %d, \"duration_seconds\": %d, "
                    "\"samples\": %d, \"block_size\": %d, \"analysis_begin\": %d, "
                    "\"analysis_samples\": %d, "
                    "\"description\": \"220Hz*0.35 + 330Hz*0.2 + LCG(seed 987654321)*0.05\"},\n",
                 static_cast<int>(kSampleRate), kDurationSeconds, kTotalSamples, kBlockSize,
                 kAnalysisBegin, kTotalSamples - kAnalysisBegin);
    std::fprintf(f, "  \"cases\": [\n");
    bool first = true;
    for (const auto& c : cases) {
        if (!first) std::fprintf(f, ",\n");
        first = false;
        std::fprintf(f, "    {\"name\": \"%s\", \"pitch_st\": %s, \"ran\": %s, "
                        "\"expect\": \"%s\", \"max_abs_diff\": %s, \"rms_diff\": %s, "
                        "\"rms_a\": %s, \"rms_b\": %s, \"rel_diff_db\": %s, "
                        "\"correlation\": %s, \"bit_identical_fraction\": %s, "
                        "\"full_stream_bit_identical\": %s, \"latency_a\": %u, "
                        "\"latency_b\": %u, \"latency_equal\": %s, "
                        "\"best_shift\": %d, \"best_shift_max_abs_diff\": %s, "
                        "\"wav_lagrange6\": \"%s\", \"wav_obskernel6\": \"%s\", "
                        "\"wav_diff\": \"%s\", \"build_config_defect\": %s, "
                        "\"pass\": %s, \"note\": \"%s\"}",
                     jsonEscape(c.name).c_str(), jsonNum(c.pitchSt).c_str(),
                     c.ran ? "true" : "false", jsonEscape(c.expectLabel).c_str(),
                     jsonNum(c.maxAbsDiff).c_str(), jsonNum(c.rmsDiff).c_str(),
                     jsonNum(c.rmsA).c_str(), jsonNum(c.rmsB).c_str(),
                     jsonNum(c.relDiffDb).c_str(), jsonNum(c.corr).c_str(),
                     jsonNum(c.bitIdenticalFrac).c_str(),
                     c.fullStreamBitIdentical ? "true" : "false",
                     static_cast<unsigned>(c.latencyA),
                     static_cast<unsigned>(c.latencyB),
                     c.latencyEqual ? "true" : "false",
                     static_cast<int>(c.bestShift), jsonNum(c.bestShiftMaxDiff).c_str(),
                     jsonEscape(c.wavA).c_str(), jsonEscape(c.wavB).c_str(),
                     jsonEscape(c.wavDiff).c_str(),
                     c.buildDefect ? "true" : "false", c.pass ? "true" : "false",
                     jsonEscape(c.note).c_str());
    }
    std::fprintf(f, "\n  ],\n");
    std::fprintf(f, "  \"verdict\": \"%s\"\n", overallPass ? "PASS" : "FAIL");
    std::fprintf(f, "}\n");
    std::fclose(f);
}

//==========================================================================
// usage / listing
//==========================================================================
void printUsage(const char* argv0) {
    std::printf(
        "usage: %s [options]\n"
        "  --lagrange6 <path>   Lagrange6 bundle (PitchLab_Lagrange6_Balanced.vst3)\n"
        "  --obskernel6 <path>  ObsKernel6 bundle (PitchLab_ObsKernel6_Balanced.vst3)\n"
        "  --out <dir>          output dir for the WAVs (default: dual_nulltest_out)\n"
        "  --json <path>        JSON report path (default: <out>/dual_nulltest.json)\n"
        "  --case <name>        run only the named case (see --list)\n"
        "  --list               list the case names and exit\n"
        "  --help               this help\n",
        argv0);
}

}  // namespace

//==========================================================================
// main
//==========================================================================
int main(int argc, char** argv) {
    std::string pathLag, pathObs, outDir = "dual_nulltest_out", jsonPath, onlyCase;
    bool wantList = false, wantHelp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lagrange6" && i + 1 < argc) pathLag = argv[++i];
        else if (a == "--obskernel6" && i + 1 < argc) pathObs = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--case" && i + 1 < argc) onlyCase = argv[++i];
        else if (a == "--list") wantList = true;
        else if (a == "--help" || a == "-h") wantHelp = true;
        else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            printUsage(argv[0]);
            return 2;
        }
    }
    if (wantHelp) { printUsage(argv[0]); return 0; }
    if (wantList) {
        for (const auto& c : kAllCases) std::printf("%s\n", c.name);
        return 0;
    }
    if (jsonPath.empty()) jsonPath = outDir + "/dual_nulltest.json";

    // resolve default module paths (mirror of the module harness's candidates)
    if (pathLag.empty()) pathLag = defaultModulePath(argv[0], "PitchLab_Lagrange6_Balanced.vst3");
    if (pathObs.empty()) pathObs = defaultModulePath(argv[0], "PitchLab_ObsKernel6_Balanced.vst3");
    if (pathLag.empty() || pathObs.empty()) {
        std::fprintf(stderr,
                     "error: module bundle(s) not found (pass --lagrange6 / --obskernel6)\n"
                     "  lagrange6 : %s\n  obskernel6: %s\n",
                     pathLag.empty() ? "<not found>" : pathLag.c_str(),
                     pathObs.empty() ? "<not found>" : pathObs.c_str());
        return 2;
    }
    if (fileExists(pathLag) && fileExists(pathObs)) {
        std::error_code canEc;
        const std::filesystem::path cl = std::filesystem::weakly_canonical(pathLag, canEc);
        const std::filesystem::path co = std::filesystem::weakly_canonical(pathObs, canEc);
        if (!canEc && cl == co) {
            std::fprintf(stderr, "error: --lagrange6 and --obskernel6 point to the SAME "
                                 "bundle; the dual test needs two distinct builds\n");
            return 2;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    std::printf("octavox_dual_nulltest (R19-P3b) -- deterministic dual-module VST3 null test\n");
    std::printf("harness scope: two separately-built bundles loaded SIMULTANEOUSLY in ONE\n");
    std::printf("process (SDK VST3::Hosting::Module loader = real dlopen + GetPluginFactory);\n");
    std::printf("lockstep process() driving with identical stimulus; sample-for-sample\n");
    std::printf("output comparison. NOT a DAW test.\n\n");

    // ---- load BOTH bundles (two real dlopens, both stay resident) ----------
    ModuleSession lag, obs;
    HostApplication hostCtx;  // one host context shared by both instances
    const bool okLag = loadAndVerify(lag, pathLag, kCidLagrange6, kCidObsKernel6, &hostCtx);
    const bool okObs = loadAndVerify(obs, pathObs, kCidObsKernel6, kCidLagrange6, &hostCtx);

    // per-module identity lines
    const char* tags[2] = {"lagrange6", "obskernel6"};
    ModuleSession* mods[2] = {&lag, &obs};
    for (int i = 0; i < 2; ++i) {
        const ModuleSession& m = *mods[i];
        std::printf("[%s] %s\n", tags[i], m.path.c_str());
        if (!m.module) {
            std::printf("    LOAD FAILED: %s\n", m.loadError.c_str());
            continue;
        }
        std::printf("    factory classes: %d | category: '%s' | class: '%s' | version: '%s'\n",
                    m.factoryClassCount, m.classCategory.c_str(), m.className.c_str(),
                    m.classVersion.c_str());
        std::printf("    class id found:    %s\n", m.classIdHex.c_str());
        std::printf("    class id expected: %s  -> %s\n",
                    uidDashed(i == 0 ? kLag6L1 : kObs6L1, i == 0 ? kLag6L2 : kObs6L2,
                              i == 0 ? kLag6L3 : kObs6L3, i == 0 ? kLag6L4 : kObs6L4)
                        .c_str(),
                    m.identityOk ? "IDENTITY OK" : "IDENTITY MISMATCH");
        if (m.instantiated)
            std::printf("    instance: params %d, buses %d in / %d out, latency %u samples\n",
                        static_cast<int>(m.paramCount), static_cast<int>(m.audioInBuses),
                        static_cast<int>(m.audioOutBuses), static_cast<unsigned>(m.latency));
        if (!m.loadError.empty())
            std::printf("    error: %s\n", m.loadError.c_str());
    }

    // ---- simultaneous-residency live probe: AFTER both dlopens, both live
    // factories must still answer with their OWN distinct class ids (proves
    // the two same-named GetPluginFactory exports never shadowed each other)
    if (okLag) lag.residencyProbeOk = scanFactoryIdentity(lag, kCidLagrange6, kCidObsKernel6);
    if (okObs) obs.residencyProbeOk = scanFactoryIdentity(obs, kCidObsKernel6, kCidLagrange6);
    const bool bothLoaded = okLag && okObs;
    std::printf("\nsimultaneous load: lagrange6=%s obskernel6=%s; live factory re-probe "
                "after both dlopens: %s\n",
                okLag ? "loaded" : "FAILED", okObs ? "loaded" : "FAILED",
                bothLoaded && lag.residencyProbeOk && obs.residencyProbeOk ? "OK"
                                                                           : "FAILED");
    std::printf("loader note: module_linux.cpp dlopen flags = RTLD_LAZY (no RTLD_GLOBAL ->\n");
    std::printf("    glibc default RTLD_LOCAL); per-handle dlsym resolves each module's own\n");
    std::printf("    GetPluginFactory -- identical exported symbol names do not collide.\n\n");

    // ---- the stimulus ------------------------------------------------------
    std::vector<float> in;
    makeInput(in);
    std::printf("signal: %d s @ %d Hz, %d samples, block %d, mono; 220Hz*0.35 + 330Hz*0.2 "
                "+ LCG(987654321)*0.05\n",
                kDurationSeconds, static_cast<int>(kSampleRate), kTotalSamples, kBlockSize);
    std::printf("analysis region: k >= %d (2304 startup/latency + 4096 grain), "
                "%d samples\n\n",
                kAnalysisBegin, kTotalSamples - kAnalysisBegin);

    // ---- run the cases -----------------------------------------------------
    std::vector<CaseResult> results;
    for (const auto& cd : kAllCases) {
        if (!onlyCase.empty() && onlyCase != cd.name) continue;
        CaseResult r = runCase(cd, lag, obs, in, outDir);
        results.push_back(std::move(r));
    }
    if (results.empty()) {
        std::fprintf(stderr, "error: no case matched --case '%s' (see --list)\n",
                     onlyCase.c_str());
        return 2;
    }

    // ---- table ---------------------------------------------------------------
    std::printf("%-27s %11s %11s %9s %8s %9s %-14s %-5s\n", "case", "maxdiff", "rmsdiff",
                "reldB", "corr", "bitident", "expect", "ok");
    std::printf("%s\n", std::string(105, '-').c_str());
    for (const auto& r : results) {
        char reldB[16];
        if (std::isfinite(r.relDiffDb)) std::snprintf(reldB, sizeof reldB, "%9.2f", r.relDiffDb);
        else std::snprintf(reldB, sizeof reldB, "%9s", r.relDiffDb < 0 ? "-inf" : "inf");
        char bitId[24];
        std::snprintf(bitId, sizeof bitId, "%9.6f", r.bitIdenticalFrac);
        const char* okCol = !r.ran ? "SKIP" : (r.pass ? "ok" : "FAIL");
        std::printf("%-27s %11.4g %11.4g %s %8.4f %s %-14s %-5s\n", r.name.c_str(),
                    r.maxAbsDiff, r.rmsDiff, reldB, r.corr, bitId, r.expectLabel.c_str(),
                    okCol);
        if (!r.note.empty()) std::printf("    -> %s\n", r.note.c_str());
    }
    std::printf("%s\n", std::string(105, '-').c_str());

    // ---- loud defect banners -------------------------------------------------
    for (const auto& r : results) {
        if (r.buildDefect)
            std::printf(
                "!!!! BUILD-CONFIGURATION DEFECT: case '%s' is BIT-IDENTICAL between the\n"
                "!!!! Lagrange6 and ObsKernel6 modules -- the compiled builds did NOT select\n"
                "!!!! different interpolation kernels.\n",
                r.name.c_str());
    }

    // ---- verdict --------------------------------------------------------------
    int casesPassed = 0, casesFailed = 0, casesSkipped = 0, defects = 0;
    for (const auto& r : results) {
        if (!r.ran) ++casesSkipped;
        else if (r.pass) ++casesPassed;
        else ++casesFailed;
        if (r.buildDefect) ++defects;
    }
    const bool identityOk = okLag && okObs && lag.identityOk && obs.identityOk &&
                            lag.residencyProbeOk && obs.residencyProbeOk;
    const bool overall = identityOk && casesFailed == 0 && casesSkipped == 0;
    std::printf("identity checks: %s | cases: %d passed, %d failed, %d skipped, "
                "%d build-config defect(s)\n",
                identityOk ? "OK" : "FAILED", casesPassed, casesFailed, casesSkipped,
                defects);
    std::printf("verdict: %s\n", overall ? "PASS" : "FAIL");

    // ---- teardown (proper order: interfaces released BEFORE the dlcloses) ----
    shutdownInstance(lag);
    shutdownInstance(obs);

    writeJson(jsonPath, lag, obs, results, overall);
    std::printf("json: %s\n", jsonPath.c_str());
    std::printf("wavs: %s/<case>_lagrange6.wav, <case>_obskernel6.wav, <case>_diff.wav\n",
                outDir.c_str());
    return overall ? 0 : 1;
}
