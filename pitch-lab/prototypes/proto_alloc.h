#pragma once

// Pitch Lab — Task 28 allocation audit (the RT-probe half of the
// production T-A1 technique, applied to the research prototypes).
//
// The global operator new/delete replacements below are deliberately
// linked into EVERY task28 executable: they count allocations so the RT
// feasibility probe can assert the design rule "all allocation happens in
// prepare()" with real evidence. This library (pitchlab_proto28) must
// NEVER be linked into any product target — it is research-only by the
// task-28 boundary rules.

#include <cstdint>

namespace pitchlab::proto {

/// Reset the counters (call right before the audited region).
void allocAuditReset();

/// Number of allocations since the last reset.
[[nodiscard]] int64_t allocAuditCount();

/// Total bytes allocated since the last reset.
[[nodiscard]] int64_t allocAuditBytes();

}  // namespace pitchlab::proto
