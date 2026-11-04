// OCTAVOX R21 -- independent harmony mapper reference-vector verifier.
// ============================================================================
// PROVENANCE: our own tool (round 21). Verifies the independent harmony
// mapper (octavox/harmony.hpp) against the machine-readable reference
// vectors generated from the SAME recovered rules by the independent
// Python generator (evidence/analysis_round21/r21_harmony_vector_generation.py
// -> r21_harmony_vectors.json). Cross-language agreement <= 1e-12 relative
// is required (bit-equality holds on one platform; the tolerance covers
// libm exp() differences only).
//
// Modes:
//   --verify <vectors.json>   run every vector; report pass/fail + max rel
//                             deviation; exit 1 on any failure
//   --dump                    print the recovered tables + a representative
//                             mapping dump (documentation aid)
// ============================================================================
#include "octavox/harmony.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// A tiny JSON array-of-objects reader for the vectors file (the file is
// machine-generated with a fixed, simple layout: flat objects with
// numeric/string fields; no nested arrays except the top level).
struct Vec {
    std::string id;
    int key = 9;
    std::string scale;         // row name -> row index via the name table
    int scale_row = 1;         // resolved row index (row-level API)
    int scale_choice = 0;      // resolved engine choice (choice-level API)
    bool use_choice_api = false;
    std::string temperament;  // name -> enum
    int interval = 14;
    int voice = 0;             // which voice's ratio is the expected value
    double fine_cents = 0.0;
    double global_tune_cents = 0.0;
    double expected_ratio = 1.0;
    std::string comment;
};

std::string jsonStr(const std::string& s, size_t& i) {
    std::string out;
    if (s[i] != '"') return out;
    ++i;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) { ++i; }
        out += s[i++];
    }
    ++i;
    return out;
}

double jsonNum(const std::string& s, size_t& i) {
    size_t j = i;
    while (j < s.size() && (isdigit((unsigned char)s[j]) || s[j] == '-' ||
                            s[j] == '+' || s[j] == '.' || s[j] == 'e' ||
                            s[j] == 'E'))
        ++j;
    const double v = std::atof(s.substr(i, j - i).c_str());
    i = j;
    return v;
}

std::vector<Vec> parseVectors(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::string s;
    char buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, got);
    std::fclose(f);

    std::vector<Vec> out;
    // target the "vectors": [ ... ] array (skip the top-level metadata
    // object with its nested tables)
    size_t i = s.find("\"vectors\": [");
    if (i == std::string::npos) i = 0;
    while (i < s.size()) {
        // find each object's opening brace inside the vectors array
        while (i < s.size() && s[i] != '{') ++i;
        if (i >= s.size()) break;
        ++i;
        Vec v;
        while (i < s.size() && s[i] != '}') {
            while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == '\n' ||
                                    s[i] == '\r' || s[i] == '\t'))
                ++i;
            if (i >= s.size() || s[i] == '}') break;
            if (s[i] != '"') { ++i; continue; }  // progress guard
            const std::string key = jsonStr(s, i);
            while (i < s.size() && (s[i] == ' ' || s[i] == ':')) ++i;
            if (s[i] == '"') {
                const std::string val = jsonStr(s, i);
                if (key == "id") v.id = val;
                else if (key == "scale") v.scale = val;
                else if (key == "temperament") v.temperament = val;
                else if (key == "comment") v.comment = val;
                else if (key == "api") v.use_choice_api = (val == "choice");
            } else if (s[i] == '-' || s[i] == '+' || isdigit((unsigned char)s[i])) {
                const double val = jsonNum(s, i);
                if (key == "key") v.key = (int)val;
                else if (key == "scale_row") v.scale_row = (int)val;
                else if (key == "scale_choice") v.scale_choice = (int)val;
                else if (key == "interval") v.interval = (int)val;
                else if (key == "voice") v.voice = (int)val;
                else if (key == "fine_cents") v.fine_cents = val;
                else if (key == "global_tune_cents") v.global_tune_cents = val;
                else if (key == "expected_ratio") v.expected_ratio = val;
            } else {
                ++i;  // unknown value type: skip one char (progress guard)
            }
        }
        ++i;  // skip '}'
        out.push_back(v);
    }
    return out;
}

octavox::harmony::Temperament temperFromName(const std::string& n) {
    if (n == "just_major" || n == "Just Major") return octavox::harmony::Temperament::JustMajor;
    if (n == "just_minor" || n == "Just Minor") return octavox::harmony::Temperament::JustMinor;
    if (n == "pythagorean" || n == "Pythagorean") return octavox::harmony::Temperament::Pythagorean;
    if (n == "meantone" || n == "meantone_1_4" || n == "Meantone") return octavox::harmony::Temperament::Meantone14;
    return octavox::harmony::Temperament::Equal12;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace octavox::harmony;
    const char* verifyPath = nullptr;
    bool dump = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--verify") && i + 1 < argc) verifyPath = argv[++i];
        else if (!std::strcmp(argv[i], "--dump")) dump = true;
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }

    if (dump) {
        std::printf("== PitchLab harmony mapper: recovered tables (OBS verbatim) ==\n");
        const char* tnames[5] = {"12-TET", "just major", "just minor", "pythagorean", "meantone 1/4"};
        for (int t = 0; t < 5; ++t) {
            std::printf("%-13s:", tnames[t]);
            for (int c = 0; c < 12; ++c) std::printf(" %d", temperamentCents((Temperament)t)[c]);
            std::printf("\n");
        }
        std::printf("key map (17 -> 12):");
        for (int k = 0; k < 17; ++k) std::printf(" %d", keyMap()[k]);
        std::printf("   (A = 9)\n");
        std::printf("interval mux (29):");
        for (int i = 0; i < 29; ++i) std::printf(" %d", intervalMuxTable()[i]);
        std::printf("\n");
        std::printf("scal mux (15):");
        for (int i = 0; i < 15; ++i) std::printf(" %d", scalMuxTable()[i]);
        std::printf("\n\n== representative mapping (ionian, equal, A, per voice) ==\n");
        MapRequest req;
        req.key = 13;  // A (key-map value 9)
        req.scale = 1;
        const int ivs[8] = {14, 14, 14, 14, 14, 14, 14, 14};  // all unison
        for (int v = 0; v < 8; ++v) req.voices[v].interval = ivs[v];
        MapResult r = mapRow(req);
        for (int v = 0; v < 8; ++v)
            std::printf("voice %d: unison ratio = %.12f\n", v + 1, r.ratio[v]);
        std::printf("\n== representative mapping (harmonic minor, meantone, C, voice1 '"
                    "+3rd' label walks DOWN a 4th-class step) ==\n");
        req.key = 0;  // C
        req.scale = 10;
        req.temperament = Temperament::Meantone14;
        req.voices[0].interval = 16;  // "+3rd"
        r = mapRow(req);
        std::printf("voice 1 cents = %.3f, ratio = %.12f (key_ratio = %.12f)\n",
                    r.voice_cents[0], r.ratio[0], r.key_ratio);
        return 0;
    }

    if (!verifyPath) {
        std::fprintf(stderr, "usage: %s --verify <vectors.json> | --dump\n", argv[0]);
        return 2;
    }

    std::vector<Vec> vecs = parseVectors(verifyPath);
    if (vecs.empty()) {
        std::fprintf(stderr, "no vectors parsed from %s\n", verifyPath);
        return 2;
    }
    long pass = 0, fail = 0;
    double worstRel = 0.0;
    const char* worstId = "";
    for (const Vec& v : vecs) {
        MapRequest req;
        req.key = v.key;
        req.temperament = temperFromName(v.temperament);
        req.global_tune_cents = v.global_tune_cents;
        if (v.use_choice_api)
            req.scale = v.scale_choice;
        else
            req.scale = v.scale_row;
        for (int i = 0; i < kNumVoices; ++i) {
            req.voices[i].interval = v.interval;
            req.voices[i].fine_cents = v.fine_cents;
        }
        const MapResult r = v.use_choice_api ? mapChoice(req) : mapRow(req);
        const int voice = v.voice < 0 || v.voice > 7 ? 0 : v.voice;
        const double got = r.ratio[voice];
        const double rel = std::fabs(got - v.expected_ratio) /
                           std::max(1e-300, std::fabs(v.expected_ratio));
        if (rel <= 1e-12) {
            ++pass;
            if (rel > worstRel) { worstRel = rel; worstId = v.id.c_str(); }
        } else {
            ++fail;
            std::printf("FAIL %s: got %.17g want %.17g (rel %.3g)  [%s]\n",
                        v.id.c_str(), got, v.expected_ratio, rel, v.comment.c_str());
            if (rel > worstRel) { worstRel = rel; worstId = v.id.c_str(); }
        }
    }
    std::printf("harmony vector verification: %ld passed, %ld failed, %ld total\n",
                pass, fail, pass + fail);
    std::printf("worst relative deviation: %.3g (%s) [tolerance 1e-12]\n", worstRel, worstId);
    return fail == 0 ? 0 : 1;
}
