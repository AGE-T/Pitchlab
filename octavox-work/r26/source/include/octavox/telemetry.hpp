// ---------------------------------------------------------------------------
// OCTAVOX native DSP core -- m02-STYLE PITCH TELEMETRY (R31)
// ============================================================================
// PROVENANCE AND SCOPE (clean-room status of this file):
//   This component is OUR OWN design. It contains no code from, and is not
//   derived from code of, the original Eventide Octavox plug-in; the
//   original binary was never executed. What it DOES use is the published
//   BEHAVIOURAL SPECIFICATION our R26-R30 static audits established for the
//   original's pitch-correction telemetry surface:
//     * R28 F1.6 -- the m02 readout semantics (ids 7..13):
//         7 = cents from C4 (1200/ln2 reference, -65535 sentinel),
//         8 = frequency ratio,
//         9 = Q23-quantised ratio n/(n+8),
//         10 = sqrt(frac * (1 - 2^-23)),
//         11 = confidence-like combination,
//         12/13 = -cents / +cents correction twins, gated by a
//                 confidence test,
//     * R30 F2.3-F2.6 -- the delivery architecture: a PER-VOICE BLOCK-RATE
//       POLLER reads the engines once per process block, change-detects
//       every result and fans the changes out as ATOMIC DIRTY BITS
//       (the original: `lock or [listener->bits], 1 << shift`).
//   The PitchLab-side VALUE MAPPING is documented per id below; where the
//   original's exact quantity has no PitchLab equivalent, the closest
//   honest analogue is served and LABELLED as such (id 11). All reading
//   happens through the engine's public observer API (dsp.hpp, R31 block).
//
// THREADING: poll() runs on the AUDIO THREAD (wait-free, allocation-free,
// lock-free: one atomic OR per changed cell). The snapshot readers
// (frame()/dirtyBits()) may run on any thread; consumeDirtyBits() is the
// UI-side read-and-clear (atomic exchange). Snapshot reads are
// eventually-consistent pairs (a torn double is impossible -- each cell is
// a single std::atomic<double> with relaxed ordering; cross-cell
// consistency is not required for display telemetry, matching the
// original's per-field dirty-bit contract).
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_TELEMETRY_HPP_INCLUDED
#define OCTAVOX_TELEMETRY_HPP_INCLUDED

#include <atomic>
#include <cstdint>

#include "octavox/dsp.hpp"

namespace octavox {

/// The m02-style telemetry id space (R28 F1.6). Ids 0..6 are reserved for
/// future expansion (the original's m02 handled 7..13; we keep the same
/// numbering so audit cross-references stay readable).
enum TelemetryId : int {
    kTelCentsFromC4 = 7,   ///< 1200/ln2 * ln(f0/261.6256); -65535 when no pitch
    kTelFreqRatio   = 8,   ///< the voice's current gliding shift ratio
    kTelQ23Ratio    = 9,   ///< n/(n+8) with n = llround(ratio * 2^23)
    kTelPhaseConf   = 10,  ///< sqrt(frac * (1 - 2^-23)), frac = grain phase
    kTelConfidence  = 11,  ///< PitchLab mapping: periodicity x state weight
    kTelNegCents    = 12,  ///< -shiftCents when the gate is open, else 0
    kTelPosCents    = 13,  ///< +shiftCents when the gate is open, else 0
    kTelIdCount     = 14   ///< frame cells 0..13 (0..6 unused, kept 0)
};

/// Sentinel served for id 7 when no pitch is tracked (the original's
/// -65535 sentinel, R28 F1.6).
constexpr double kTelemetryNoPitchSentinel = -65535.0;

/// Reference frequency for id 7: middle C (C4) in the original's exact
/// rational form 261.6256 Hz (R28 F1.6).
constexpr double kTelemetryC4Hz = 261.6256;

constexpr int kTelemetryMaxVoices = 8;

/// One voice's telemetry frame: id-indexed cells [0, kTelIdCount).
struct VoiceTelemetryFrame {
    double cell[kTelIdCount] = {};
};

/// Per-voice block-rate poller + dirty-bit fanout (R30 F2.3-F2.6).
///
/// Dirty-bit layout: bit (voice * 8 + (id - 7)) in a uint64
/// (8 voices x 7 ids = 56 bits; bits 56..63 unused). A bit is set when the
/// corresponding cell CHANGED VALUE at a poll (exact comparison; the
/// original's change-detect+fanout semantics).
class TelemetryPoller {
public:
    /// Read the engine's reference channel once and update the frames +
    /// dirty bits. AUDIO THREAD: wait-free, allocation-free. `voices` is
    /// the engine's CURRENT voice count (cells of voices >= voices are
    /// zeroed and their bits are NOT set).
    void poll(const MultichannelShifter& engine, int voices);

    /// Relaxed snapshot read of one voice's cell (any thread).
    double cell(int voice, int id) const;

    /// The CURRENT dirty mask (any thread; does not clear).
    uint64_t dirtyBits() const;

    /// Read-and-clear the dirty mask (the UI-side consume; any thread).
    uint64_t consumeDirtyBits();

    /// Clear frames + dirty bits (on prepare/reset; NOT real-time).
    void reset();

private:
    /// Publish one cell: change-detect vs the stored value, store, and OR
    /// the dirty bit on change.
    void publish(int voice, int id, double v);

    std::atomic<double> cells_[kTelemetryMaxVoices][kTelIdCount] = {};
    std::atomic<uint64_t> dirty_{0};
};

}  // namespace octavox

#endif  // OCTAVOX_TELEMETRY_HPP_INCLUDED
