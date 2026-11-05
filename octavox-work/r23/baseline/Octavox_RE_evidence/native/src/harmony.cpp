// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- independent harmony mapper implementation (R21-P2)
// ============================================================================
// PROVENANCE: our own independent implementation of the recovered mapping
// rules (see harmony.hpp's header comment + docs/R21_HARMONY_MAPPER_SPEC.md
// for every table's VA and every rule's instruction addresses).
// The tables below are VERBATIM dumps of the recovered .rdata (OBS).
// ---------------------------------------------------------------------------
#include "octavox/harmony.hpp"

#include <algorithm>
#include <cmath>

namespace octavox {
namespace harmony {

namespace {

// ---- the 5 temperament cent-tables (.rdata 0x1803BB690, OBS verbatim) ----
constexpr int kTemperamentCents[5][12] = {
    {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100},   // 12-TET
    {0, 112, 204, 316, 386, 498, 590, 702, 814, 884, 996, 1088},    // just major
    {0, 71, 182, 316, 386, 498, 590, 702, 814, 884, 996, 1088},     // just minor
    {0, 90, 204, 294, 408, 498, 612, 702, 792, 906, 996, 1110},     // pythagorean
    {0, 76, 193, 310, 386, 503, 580, 697, 772, 890, 1007, 1083},    // meantone 1/4
};

// ---- the 19-row scale-pattern table (.rdata 0x1803BB780, OBS verbatim) --
// Row names (recovered structure; the mux-referenced subset is certain,
// rows 0/8/9 are never selected by the 15-choice mux -- documented):
//   0 ionian(default/user slot) 1 ionian   2 dorian     3 phrygian
//   4 lydian                    5 mixolyd. 6 aeolian    7 locrian
//   8 (ionian duplicate, unreferenced)     9 (aeolian duplicate, unref.)
//  10 harmonic minor          11 melodic minor
//  12 chromatic (99-fillers)  13 whole tone
//  14 penta major             15 penta minor
//  16 enigmatic               17 neapolitan 18 hungarian
constexpr int kScalePatterns[19][12] = {
    {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7},       // 0
    {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7},       // 1
    {1, 0, 2, 3, 0, 4, 0, 5, 0, 6, 7, 0},       // 2
    {1, 2, 0, 3, 0, 4, 0, 5, 6, 0, 7, 0},       // 3
    {1, 0, 2, 0, 3, 0, 4, 5, 0, 6, 0, 7},       // 4
    {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 7, 0},       // 5
    {1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 7, 0},       // 6
    {1, 2, 0, 3, 0, 4, 5, 0, 6, 0, 7, 0},       // 7
    {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7},       // 8
    {1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 7, 0},       // 9
    {1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 0, 7},       // 10
    {1, 0, 2, 3, 0, 4, 0, 5, 0, 6, 0, 7},       // 11
    {1, 99, 2, 99, 3, 4, 99, 5, 99, 6, 99, 7},  // 12
    {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0},       // 13
    {2, 0, 3, 0, 4, 0, 0, 6, 0, 7, 0, 0},       // 14
    {2, 0, 0, 3, 0, 4, 0, 6, 0, 0, 7, 0},       // 15
    {1, 2, 0, 0, 3, 0, 4, 0, 5, 0, 6, 7},       // 16
    {1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7},       // 17
    {1, 0, 2, 3, 0, 0, 4, 5, 6, 0, 0, 7},       // 18
};

// ---- the 17-entry key map (.rdata 0x1803BB610, OBS verbatim) -------------
// (17 key spellings -> 12 pitch classes; enharmonic collapse; A = 9)
constexpr int kKeyMap[17] = {0, 1, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 8, 9, 10, 10, 11};

// ---- the 29-entry interval mux (records [45..47], OBS verbatim) ----------
// v = 14 + s + delta, delta = 1 iff s mod 7 in {2, 6} (mathematical mod).
// Selection order: -2oct,-14th,-13th,-12th,-11th,-10th,-9th,-8th,-7th,-6th,
// -5th,-4th,-3rd,-2nd,unison,+2nd,+3rd,+4th,+5th,+6th,+7th,+8th,+9th,+10th,
// +11th,+12th,+13th,+14th,+2oct  (s = -14..+14).
constexpr int kIntervalMux[29] = {0, 1, 3, 3, 4, 5, 7, 7, 8, 10, 10, 11, 12, 14,
                                  14, 15, 17, 17, 18, 19, 21, 21, 22, 24, 24, 25,
                                  26, 28, 28};

constexpr const char* kIntervalLabels[29] = {
    "-2 oct", "-14th", "-13th", "-12th", "-11th", "-10th", "-9th", "-8th",
    "-7th", "-6th", "-5th", "-4th", "-3rd", "-2nd", "unison", "+2nd", "+3rd",
    "+4th", "+5th", "+6th", "+7th", "+8th", "+9th", "+10th", "+11th", "+12th",
    "+13th", "+14th", "+2 oct"};

// ---- the 15-entry scal-mux (record [16], OBS verbatim; the authored
// irregularity: the last six choices load pattern rows shifted +2 from
// their name-matching rows) -----------------------------------------------
// choice:  0 ionian, 1 dorian, 2 phrygian, 3 lydian, 4 mixolydian,
//          5 aeolian, 6 locrian, 7 harmonic minor, 8 melodic minor,
//          9 chromatic, 10 whole tone, 11 penta maj, 12 penta min,
//          13 enigmatic, 14 neapolitan
constexpr int kScalMux[15] = {1, 2, 3, 4, 5, 6, 7, 10, 11, 14, 15, 16, 17, 18, 13};

// C-style integer division/mod (the recovered arithmetic's semantics).
int cdiv(int a, int b) { return a / b; }
int cmod(int a, int b) { return a % b; }

// The zone-run grid fill (0x1800E65B0..0x1800E6790, OBS):
// for each non-zero pattern slot, grid[run positions] = cents[slot], with
// run length = pattern_value - previous_value (gap doubling) and single
// positions for 99-fillers; size = the final position count.
int buildGrid(const int pattern[12], const int cents[12], int grid[12]) {
    int size = 0;
    int prev = 0;
    for (int slot = 0; slot < 12; ++slot) {
        const int p = pattern[slot];
        if (p == 0) continue;
        if (p == 99) {  // filler: a single position
            grid[size++] = cents[slot];
        } else {
            const int run = p - prev;  // gap doubling
            for (int r = 0; r < run && size < 12; ++r) grid[size++] = cents[slot];
            prev = p;
        }
    }
    return size;
}

// The walk (0x1800E6ED5..0x1800E7120, OBS): fills the 8-row ratio table.
// sel = 14 - v (the recovered sign law: "+" labels walk DOWN via the
// negative branch, "-" labels walk UP via the positive branch).
// Rows 0..: row0 = the tonic-relative walk target; row j+1 = the target
// relative to degree j (the voices' diatonic-stack bases).
// The pcent is added to the target BEFORE the int truncation (the
// recovered expression); row0 adds it in float.
void walk(int sel, const int grid[12], int size, double pcent,
          double outCents[8]) {
    const int q = cdiv(sel, 7);
    const double octCents = 1200.0 * static_cast<double>(q);
    if (sel >= 0) {
        // positive branch: walks UP
        const int start = std::min(cmod(sel, 7), size - 1);
        outCents[0] = static_cast<double>(grid[cmod(start, size)]) + octCents +
                      pcent;
        for (int j = 0; j < size && j < 7; ++j) {
            const int target = grid[cmod(start + j, size)];
            const int base = grid[j];
            // octave-wrap upward: if target < base add 1200
            const int wrapped = (target >= base) ? target : target + 1200;
            const int delta = static_cast<int>(wrapped + pcent) - base;
            outCents[j + 1] = static_cast<double>(delta) + octCents;
        }
        // tail fill: table[size+1..7] = table[0] (the recovered fill law:
        // rows beyond the grid size repeat the row-0 value)
        for (int j = size + 1; j < 8; ++j) outCents[j] = outCents[0];
    } else {
        // negative branch: walks DOWN
        const int step = 7 * q - sel;  // positive
        const int m = std::min(step, size - 1);
        const int start = (1 - q) * size - m;
        // row0: (grid[start % size] - 1200) % 1200 + oct + pcent (C mod)
        const int g0 = grid[cmod(start, size)] - 1200;
        const int r0 = cmod(g0, 1200);
        outCents[0] = static_cast<double>(r0) + octCents + pcent;
        for (int j = 0; j < size && j < 7; ++j) {
            const int target = grid[cmod(start + j, size)];
            const int base = grid[j];
            // octave-wrap downward: if target > base subtract 1200
            const int wrapped = (target <= base) ? target : target - 1200;
            const int delta = static_cast<int>(wrapped + pcent) - base;
            outCents[j + 1] = static_cast<double>(delta) + octCents;
        }
        for (int j = size + 1; j < 8; ++j) outCents[j] = outCents[0];
    }
}

double centsToRatio(double cents) {
    // Fixed operation order (left-to-right), double precision. The
    // original multiplies by the pre-rounded constant 0.00057762265 --
    // a <=1-ulp difference (documented).
    return std::exp(cents * 0.6931471805599453 / 1200.0);
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

MapResult mapImpl(const MapRequest& req, int patternRow, bool rawIndexSwitch) {
    const int key = clampi(req.key, 0, kNumKeys - 1);
    const int temper = clampi(static_cast<int>(req.temperament), 0, 4);
    const int* cents = kTemperamentCents[temper];
    const int* pattern = kScalePatterns[clampi(patternRow, 0, 18)];

    MapResult res;
    res.grid_size = buildGrid(pattern, cents, res.grid);

    // key-ratio law (0x1800E6557..0x1800E6589, OBS): cents[|k-9|], negated
    // below the reference A (keymap value 9), positive above.
    const int k = kKeyMap[key];
    int keyCents = 0;
    if (k < 9)
        keyCents = -cents[9 - k];
    else if (k > 9)
        keyCents = cents[k - 9];
    res.key_ratio = centsToRatio(static_cast<double>(keyCents));

    for (int v = 0; v < kNumVoices; ++v) {
        const int sel3641 = clampi(req.voices[v].interval, 0, kNumIntervals - 1);
        int sel;
        if (rawIndexSwitch) {
            // the chromatic/whole-tone switchover (records [17..19], OBS):
            // the walk uses the UN-BUMPED raw choice index.
            sel = 14 - sel3641;
        } else {
            sel = 14 - kIntervalMux[sel3641];
        }
        double rowCents[8];
        walk(sel, res.grid, res.grid_size, req.voices[v].fine_cents, rowCents);
        // voice v's row: voice 0 = row0 (tonic-relative); voice v>=1 =
        // row v (degree v-1-relative) -- the recovered 8-row table layout.
        const double vc = (v == 0) ? rowCents[0] : rowCents[v];
        res.voice_cents[v] = vc;
        res.ratio[v] = res.key_ratio * centsToRatio(vc + req.global_tune_cents);
    }
    return res;
}

}  // namespace

MapResult mapRow(const MapRequest& req) { return mapImpl(req, req.scale, false); }

MapResult mapChoice(const MapRequest& req) {
    const int choice = clampi(req.scale, 0, kNumScaleChoices - 1);
    const int muxOut = kScalMux[choice];
    // the raw-index switch fires when the scal-mux OUTPUT is 14/15 (the
    // "chromatic"/"whole tone" CHOICES -- c_cmp2/c_cmp3, records [17]/[18])
    const bool rawSwitch = (muxOut == 14 || muxOut == 15);
    return mapImpl(req, muxOut, rawSwitch);
}

const int* temperamentCents(Temperament t) {
    return kTemperamentCents[clampi(static_cast<int>(t), 0, 4)];
}
const int* scalePatternRow(int row) { return kScalePatterns[clampi(row, 0, 18)]; }
const int* intervalMuxTable() { return kIntervalMux; }
const char* intervalLabel(int i) {
    return kIntervalLabels[clampi(i, 0, 28)];
}
const int* keyMap() { return kKeyMap; }
const int* scalMuxTable() { return kScalMux; }

}  // namespace harmony
}  // namespace octavox
