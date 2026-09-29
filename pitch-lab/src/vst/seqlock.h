#pragma once

// Pitch Lab VST3 product layer — lock-free publication primitives.
//
// The VST audio thread, the preparation thread and the UI/main thread
// communicate ONLY through these primitives (product-phase specification
// §4.3: no locks, no allocation, no blocking on the audio path).
//
//  * SeqLock<T>   — a single-writer/multi-reader seqlock for small POD
//                   snapshots (parameter snapshot, status, meters).
//  * RetireStack  — an intrusive lock-free LIFO for objects retired by the
//                   audio thread and freed later by the preparation thread.
//
// Both are header-only, allocation-free on the read/write fast paths, and
// rely only on <atomic>.

#include <atomic>

namespace pitchlab::vst {

/// Single-writer / multi-reader seqlock over a trivially-copyable payload.
/// Writer: any ONE thread (the parameter owner). Readers: audio thread and
/// UI thread. A reader retries (bounded) until it observes a stable
/// even-version copy; writers are rare (parameter changes), so retries are
/// practically zero.
template <typename T>
class SeqLock {
 public:
  SeqLock() = default;
  SeqLock(const SeqLock&) = delete;
  SeqLock& operator=(const SeqLock&) = delete;

  /// Publish a new payload (writer thread only).
  void store(const T& value) {
    const uint64_t v = version_.load(std::memory_order_relaxed);
    version_.store(v + 1, std::memory_order_release);  // odd = writing
    // A relaxed data race on the payload is intended: readers only accept
    // copies taken under an even, unchanged version (the seqlock protocol).
    payload_ = value;  // NOLINT(cert-oop54-cpp)
    version_.store(v + 2, std::memory_order_release);  // even = stable
  }

  /// Read a stable copy (any thread). `spinBound` bounds the retry loop;
  /// the payload is always small, and writers are rare.
  [[nodiscard]] T load(int spinBound = 64) const {
    T out{};
    for (int i = 0;; ++i) {
      const uint64_t before = version_.load(std::memory_order_acquire);
      if ((before & 1u) != 0u) continue;  // write in flight
      out = payload_;                     // NOLINT
      const uint64_t after = version_.load(std::memory_order_acquire);
      if (before == after) return out;    // stable copy
      if (i >= spinBound) return out;     // degenerate fallback (never hit in practice)
    }
  }

  [[nodiscard]] uint64_t version() const {
    return version_.load(std::memory_order_acquire);
  }

 private:
  mutable std::atomic<uint64_t> version_{0};
  T payload_{};
};

/// Intrusive lock-free retire stack: the audio thread pushes retired nodes
/// (never touches them again after the push); the preparation thread pops
/// and deletes them. `Node` must derive RetireStack::Node.
class RetireStack {
 public:
  struct Node {
    Node* next = nullptr;
    virtual ~Node() = default;
  };

  /// Audio thread: push a retired chain (release-publishes the node).
  static void push(std::atomic<Node*>& head, Node* node) {
    node->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(node->next, node, std::memory_order_release,
                                       std::memory_order_relaxed)) {
    }
  }

  /// Preparation thread: pop the whole stack (returns nullptr when empty).
  static Node* popAll(std::atomic<Node*>& head) {
    return head.exchange(nullptr, std::memory_order_acquire);
  }
};

}  // namespace pitchlab::vst
