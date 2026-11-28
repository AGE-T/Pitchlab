// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- minimal editor offscreen screenshot tool (R33)
// ============================================================================
// Renders the R33 minimal editor through the SAME model/renderer the live
// IPlugView uses and writes a deterministic PNG. The PNG encoder is
// self-contained (zlib stored-deflate blocks -- no zlib/libpng dependency),
// so the output is byte-stable for a given source state: every published
// screenshot (report, web, demo package) is the REAL renderer output.
//
// Modes:
//   default : the documented default surface + sentinel telemetry (the
//             exact frame the golden-hash test pins)
//   demo    : a musical preset (4 voices, varied transposes/gains/delays,
//             one mute, feedback) + LIVE telemetry from 3 s of vibrato
//             sine input (the LEDs and readouts show real tracked values)
//
// Usage: octavox_ui_screenshot <out.png> [default|demo]
// ---------------------------------------------------------------------------
#include "octavox_minimal_view.h"
#include "octavox_ui_renderer.h"

#include "octavox_native_processor.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using octavox::vst3wrap::MinimalView;
using octavox::vst3wrap::OctavoxNativeProcessor;
using octavox::vst3wrap::kFeedback;
using octavox::vst3wrap::kPitch;
using octavox::vst3wrap::kVoices;
using octavox::vst3wrap::kWetDry;
using octavox::vst3wrap::kStereoWidth;
using octavox::vst3wrap::normalizedFromPlain;
using octavox::vst3wrap::plainFromNormalized;
using octavox::vst3wrap::voiceDelayId;
using octavox::vst3wrap::voiceGainId;
using octavox::vst3wrap::voiceMuteId;
using octavox::vst3wrap::voiceOffsetId;

//==========================================================================
// Minimal deterministic PNG writer (8-bit truecolor, stored deflate)
//==========================================================================

namespace {

uint32_t crc32Of(const uint8_t* d, size_t n, uint32_t crc) {
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t adler32Of(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + d[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void writeChunk(std::FILE* f, const char* type, const uint8_t* d, size_t n) {
    uint8_t len[4];
    be32(len, static_cast<uint32_t>(n));
    std::fwrite(len, 1, 4, f);
    std::fwrite(type, 1, 4, f);
    if (n) std::fwrite(d, 1, n, f);
    std::vector<uint8_t> crcBuf(4 + n);
    std::memcpy(crcBuf.data(), type, 4);
    if (n) std::memcpy(crcBuf.data() + 4, d, n);
    uint8_t crc[4];
    be32(crc, crc32Of(crcBuf.data(), crcBuf.size(), 0));
    std::fwrite(crc, 1, 4, f);
}

/// Write `w x h` pixels (0x00RRGGBB) as a PNG. Returns false on I/O error.
bool writePng(const char* path, const uint32_t* pix, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(kSig, 1, 8, f);

    // IHDR: w, h, 8-bit truecolor (2), no interlace.
    uint8_t ihdr[13];
    be32(ihdr, static_cast<uint32_t>(w));
    be32(ihdr + 4, static_cast<uint32_t>(h));
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // colour type: truecolor
    ihdr[10] = 0;  // compression: deflate
    ihdr[11] = 0;  // filter: adaptive
    ihdr[12] = 0;  // interlace: none
    writeChunk(f, "IHDR", ihdr, 13);

    // Raw scanlines: filter byte 0 + RGB triples.
    std::vector<uint8_t> raw(static_cast<size_t>(h) * (1 + 3 * w));
    for (int y = 0; y < h; ++y) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * (1 + 3 * w);
        row[0] = 0;  // filter: none
        const uint32_t* src = pix + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            row[1 + 3 * x] = static_cast<uint8_t>((src[x] >> 16) & 0xFF);
            row[2 + 3 * x] = static_cast<uint8_t>((src[x] >> 8) & 0xFF);
            row[3 + 3 * x] = static_cast<uint8_t>(src[x] & 0xFF);
        }
    }

    // zlib stream: 2-byte header + stored deflate blocks + adler32.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    const size_t kMaxStored = 65535;
    if (raw.empty()) {
        z.push_back(1);
        z.push_back(0);
        z.push_back(0);
    }
    while (pos < raw.size()) {
        const size_t n = raw.size() - pos < kMaxStored ? raw.size() - pos
                                                       : kMaxStored;
        const bool last = (pos + n) == raw.size();
        z.push_back(static_cast<uint8_t>(last ? 1 : 0));
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        const uint16_t nlen = static_cast<uint16_t>(~n);
        z.push_back(static_cast<uint8_t>(nlen & 0xFF));
        z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint8_t adler[4];
    be32(adler, adler32Of(raw.data(), raw.size()));
    z.insert(z.end(), adler, adler + 4);
    writeChunk(f, "IDAT", z.data(), z.size());
    writeChunk(f, "IEND", nullptr, 0);
    std::fclose(f);
    return true;
}

//==========================================================================
// The harness (the wrapper suite's pattern)
//==========================================================================

OctavoxNativeProcessor* makeProcessor() {
    auto* p = new OctavoxNativeProcessor();
    p->initialize(nullptr);
    ProcessSetup ps;
    std::memset(&ps, 0, sizeof(ps));
    ps.processMode = kRealtime;
    ps.symbolicSampleSize = kSample32;
    ps.sampleRate = 48000.0;
    ps.maxSamplesPerBlock = 4096;
    p->setupProcessing(ps);
    p->setActive(true);
    return p;
}

void driveBlock(OctavoxNativeProcessor* p, float* const* in, float* const* out,
                int n, IParameterChanges* changes = nullptr) {
    AudioBusBuffers ib{}, ob{};
    ib.numChannels = 2;
    ib.channelBuffers32 = const_cast<float**>(in);
    ob.numChannels = 2;
    ob.channelBuffers32 = const_cast<float**>(out);
    AudioBusBuffers inBusses[1] = {ib};
    AudioBusBuffers outBusses[1] = {ob};
    ProcessData pd;
    std::memset(&pd, 0, sizeof(pd));
    pd.processMode = kRealtime;
    pd.symbolicSampleSize = kSample32;
    pd.numSamples = n;
    pd.inputs = inBusses;
    pd.numInputs = 1;
    pd.outputs = outBusses;
    pd.numOutputs = 1;
    pd.inputParameterChanges = changes;
    p->process(pd);
}

}  // namespace

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "pitchlab_r33_editor.png";
    const bool demo = (argc > 2 && std::strcmp(argv[2], "demo") == 0);

    Steinberg::IPtr<OctavoxNativeProcessor> p(makeProcessor(), false);  // born-1 ownership
    // createView transfers the born-1 reference (the VST3 host contract).
    Steinberg::IPtr<MinimalView> view(
        static_cast<MinimalView*>(p->createView(Steinberg::Vst::ViewType::kEditor)), false);
    if (!p || !view) {
        std::fprintf(stderr, "screenshot: view creation failed\n");
        return 1;
    }
    auto& m = view->testModel();

    if (demo) {
        // A musical preset: 4 voices, harmony map ON, feedback, varied
        // per-voice transposes / gains / delays, V3 muted.
        //
        // Architecturally faithful application (what a real host does):
        // the preset rides the FIRST BLOCK'S PARAMETER QUEUE (the wrapper's
        // audio path -- engine + telemetry -- reads parameters exclusively
        // from IParameterChanges), AND the display container is synced via
        // setParamNormalized (the host's controller-side echo).
        ParameterChanges changes;
        auto addParam = [&](uint32_t id, double plain) {
            int32 idx = 0;
            IParamValueQueue* q = changes.addParameterData(id, idx);
            if (q) {
                int32 pidx = 0;
                q->addPoint(0, normalizedFromPlain(id, plain), pidx);
            }
            p->setParamNormalized(id, normalizedFromPlain(id, plain));
        };
        addParam(kVoices, 4);
        addParam(kPitch, 0.0);
        addParam(kFeedback, 0.18);
        addParam(kWetDry, 1.0);
        addParam(kStereoWidth, 0.60);  // R34: the width knob mid-travel
        addParam(voiceOffsetId(1), 7.0);    // V2: a fifth above (spread slot)
        addParam(voiceOffsetId(2), -5.0);   // V3: a fourth below
        addParam(voiceOffsetId(3), 12.0);   // V4: an octave above
        addParam(voiceGainId(1), 0.8);
        addParam(voiceGainId(3), 0.65);
        addParam(voiceMuteId(2), 1.0);      // V3 muted (shown red)
        addParam(voiceDelayId(1), 9.5);
        addParam(voiceDelayId(3), 14.0);

        // Live telemetry: 3 s of 220 Hz with a slow +-6 Hz vibrato, then
        // ONE tick so the dirty LEDs are lit from the gliding estimate.
        const double sr = 48000.0;
        std::vector<float> inL(4096), outL(4096), inR(4096), outR(4096);
        float* inCh[2] = {inL.data(), inR.data()};
        float* outCh[2] = {outL.data(), outR.data()};
        double phase = 0.0, t = 0.0;
        for (int block = 0; block < static_cast<int>(3.0 * sr / 4096); ++block) {
            for (int i = 0; i < 4096; ++i, t += 1.0 / sr) {
                const double f = 220.0 + 6.0 * std::sin(2.0 * 3.14159265358979 * 0.7 * t);
                phase += 2.0 * 3.14159265358979 * f / sr;
                const float s = static_cast<float>(0.5 * std::sin(phase));
                inL[i] = s;
                inR[i] = s;
            }
            driveBlock(p, inCh, outCh, 4096,
                       block == 0 ? &changes : nullptr);
        }
    }

    m.tick();  // cache params + telemetry (lights the dirty LEDs once)

    std::vector<uint32_t> buf(
        static_cast<size_t>(octavox::vst3wrap::MinimalUiModel::kWidth) *
            octavox::vst3wrap::MinimalUiModel::kHeight,
        0);
    octavox::vst3wrap::ui::UiRenderer r(
        buf.data(), octavox::vst3wrap::MinimalUiModel::kWidth,
        octavox::vst3wrap::MinimalUiModel::kHeight);
    m.render(r);
    if (!writePng(out, buf.data(), octavox::vst3wrap::MinimalUiModel::kWidth,
                  octavox::vst3wrap::MinimalUiModel::kHeight)) {
        std::fprintf(stderr, "screenshot: cannot write %s\n", out);
        return 1;
    }
    std::printf("wrote %s (%dx%d, %s)\n", out,
                octavox::vst3wrap::MinimalUiModel::kWidth,
                octavox::vst3wrap::MinimalUiModel::kHeight,
                demo ? "demo preset + live telemetry" : "default surface");
    return 0;
}
