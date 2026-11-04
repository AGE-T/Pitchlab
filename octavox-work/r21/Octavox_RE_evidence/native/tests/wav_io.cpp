// OCTAVOX round 12 -- WAV I/O implementation (harness only).
// RIFF/WAVE parsing kept intentionally small: PCM only, 16/24/32-bit.

#include "wav_io.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace octavox {
namespace wav {

namespace {

uint32_t rd32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t rd16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int16_t clampRound16(double y) {
    if (y > 1.0) y = 1.0;
    if (y < -1.0) y = -1.0;
    // Python: np.round(y * 32767.0) -- round-half-to-even
    const double v = std::nearbyint(y * 32767.0);
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<int16_t>(static_cast<long long>(v));
}

}  // namespace

std::vector<float> readWav(const std::string& path, int& sr, int& channels) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    unsigned char hdr[12];
    if (std::fread(hdr, 1, 12, f) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        throw std::runtime_error(path + ": not a RIFF/WAVE file");
    }
    int bits = -1, audio_fmt = -1;
    std::vector<unsigned char> data;
    for (;;) {
        unsigned char ch[8];
        if (std::fread(ch, 1, 8, f) != 8) break;
        const uint32_t sz = rd32(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            std::vector<unsigned char> fmt(sz);
            if (sz && std::fread(fmt.data(), 1, sz, f) != sz) break;
            audio_fmt = rd16(fmt.data());
            channels = rd16(fmt.data() + 2);
            sr = static_cast<int>(rd32(fmt.data() + 4));
            bits = rd16(fmt.data() + 14);
        } else if (std::memcmp(ch, "data", 4) == 0) {
            data.resize(sz);
            if (sz && std::fread(data.data(), 1, sz, f) != sz) break;
        } else {
            if (std::fseek(f, static_cast<long>(sz) + (sz & 1), SEEK_CUR) != 0)
                break;
        }
        if (sz & 1) {  // word-align pad
            int c = std::fgetc(f);
            (void)c;
        }
    }
    std::fclose(f);
    // R21: accept PCM (tag 1, 16/24/32-bit int) AND IEEE-float (tag 3,
    // 32-bit) -- DAW-exported float WAVs are common in the real-audio
    // comparison harness. Harness-only extension.
    if ((audio_fmt != 1 && audio_fmt != 3) || bits == -1 || channels <= 0)
        throw std::runtime_error(path + ": unsupported/missing fmt chunk");
    if (audio_fmt == 3 && bits != 32)
        throw std::runtime_error(path + ": unsupported float sample width");
    if (audio_fmt == 1 && bits != 16 && bits != 24 && bits != 32)
        throw std::runtime_error(path + ": unsupported sample width");
    const size_t frame = static_cast<size_t>(channels) * (bits / 8);
    const size_t nframes = data.size() / frame;
    std::vector<float> out(nframes * channels);
    const unsigned char* p = data.data();
    for (size_t i = 0; i < out.size(); ++i) {
        double v;
        if (bits == 16) {
            const int16_t s = static_cast<int16_t>(rd16(p));
            v = static_cast<double>(s) / 32768.0;
            p += 2;
        } else if (bits == 24) {
            const uint32_t u = static_cast<uint32_t>(p[0]) |
                               (static_cast<uint32_t>(p[1]) << 8) |
                               (static_cast<uint32_t>(p[2]) << 16);
            int32_t s = static_cast<int32_t>(u);
            if (s & 0x800000) s |= static_cast<int32_t>(0xff000000);
            v = static_cast<double>(s) / 8388608.0;
            p += 3;
        } else if (audio_fmt == 3) {
            // IEEE 754 float32 (tag 3): the bits ARE the sample.
            uint32_t u = rd32(p);
            float fv;
            std::memcpy(&fv, &u, 4);
            v = static_cast<double>(fv);
            p += 4;
        } else {
            const uint32_t u = rd32(p);
            int32_t s = static_cast<int32_t>(u);
            v = static_cast<double>(s) / 2147483648.0;
            p += 4;
        }
        out[i] = static_cast<float>(v);
    }
    return out;
}

void writeWav(const std::string& path, const float* x, int n, int channels,
              int sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    const uint32_t dataBytes = static_cast<uint32_t>(n) * channels * 2;
    const uint32_t riffSize = 36 + dataBytes;
    std::vector<unsigned char> hdr(44);
    std::memcpy(hdr.data(), "RIFF", 4);
    std::memcpy(hdr.data() + 4, &riffSize, 4);
    std::memcpy(hdr.data() + 8, "WAVE", 4);
    std::memcpy(hdr.data() + 12, "fmt ", 4);
    const uint32_t fmtSz = 16;
    std::memcpy(hdr.data() + 16, &fmtSz, 4);
    const uint16_t af = 1, ch = static_cast<uint16_t>(channels),
                   bits = 16;
    const uint32_t rate = static_cast<uint32_t>(sr);
    const uint32_t byterate = rate * ch * 2;
    const uint16_t block = static_cast<uint16_t>(ch * 2);
    std::memcpy(hdr.data() + 20, &af, 2);
    std::memcpy(hdr.data() + 22, &ch, 2);
    std::memcpy(hdr.data() + 24, &rate, 4);
    std::memcpy(hdr.data() + 28, &byterate, 4);
    std::memcpy(hdr.data() + 32, &block, 2);
    std::memcpy(hdr.data() + 34, &bits, 2);
    std::memcpy(hdr.data() + 36, "data", 4);
    std::memcpy(hdr.data() + 40, &dataBytes, 4);
    std::fwrite(hdr.data(), 1, 44, f);
    std::vector<int16_t> q(static_cast<size_t>(n) * channels);
    for (size_t i = 0; i < q.size(); ++i) {
        q[i] = clampRound16(static_cast<double>(x[i]));
    }
    if (!q.empty()) std::fwrite(q.data(), 2, q.size(), f);
    std::fclose(f);
}

}  // namespace wav
}  // namespace octavox

void writeWavF32(const std::string& path, const float* x, int n, int channels,
                 int sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    const uint32_t dataBytes = static_cast<uint32_t>(n) * channels * 4;
    const uint32_t riffSize = 36 + dataBytes;
    std::vector<unsigned char> hdr(44);
    std::memcpy(hdr.data(), "RIFF", 4);
    std::memcpy(hdr.data() + 4, &riffSize, 4);
    std::memcpy(hdr.data() + 8, "WAVE", 4);
    std::memcpy(hdr.data() + 12, "fmt ", 4);
    const uint32_t fmtSz = 16;
    std::memcpy(hdr.data() + 16, &fmtSz, 4);
    const uint16_t af = 3, ch = static_cast<uint16_t>(channels),  // IEEE float
                   bits = 32;
    const uint32_t rate = static_cast<uint32_t>(sr);
    const uint32_t byterate = rate * ch * 4;
    const uint16_t block = static_cast<uint16_t>(ch * 4);
    std::memcpy(hdr.data() + 20, &af, 2);
    std::memcpy(hdr.data() + 22, &ch, 2);
    std::memcpy(hdr.data() + 24, &rate, 4);
    std::memcpy(hdr.data() + 28, &byterate, 4);
    std::memcpy(hdr.data() + 32, &block, 2);
    std::memcpy(hdr.data() + 34, &bits, 2);
    std::memcpy(hdr.data() + 36, "data", 4);
    std::memcpy(hdr.data() + 40, &dataBytes, 4);
    std::fwrite(hdr.data(), 1, 44, f);
    // little-endian explicit (the sandbox, Windows and all DAW targets of
    // this evidence are little-endian; RIFF/WAVE float is LE by convention)
    const size_t total = static_cast<size_t>(n) * channels;
    std::vector<unsigned char> bytes(total * 4);
    for (size_t i = 0; i < total; ++i) {
        uint32_t b;
        std::memcpy(&b, &x[i], 4);
        bytes[i * 4 + 0] = static_cast<unsigned char>(b & 0xff);
        bytes[i * 4 + 1] = static_cast<unsigned char>((b >> 8) & 0xff);
        bytes[i * 4 + 2] = static_cast<unsigned char>((b >> 16) & 0xff);
        bytes[i * 4 + 3] = static_cast<unsigned char>((b >> 24) & 0xff);
    }
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}
