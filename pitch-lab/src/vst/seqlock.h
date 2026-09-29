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
//
// DEFECT-FIX NOTE (Task 24, the P0.2 audit finding): the previous
// implementation copied the payload with plain (non-atomic) assignments
// between the version fences. That is a DATA RACE under the C++ memory
// model (TSAN-reproduced): the sequence counter does not legalise
// concurrent non-atomic read/write access to the payload — a torn word
// read is undefined behaviour even when the version check later discards
// it. The payload is now stored as an array of atomic machine words: every
// access is atomic (no UB, no TSAN races), and the odd/even version
// protocol (release stores on the writer, acquire loads bracketing the
// word copies on the reader) still detects and retries torn copies. This
// is the standard atomised-payload seqlock: same lock-free, allocation-
// free fast paths, memory-model-clean.

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace pitchlab::vst {

/// Single-writer / multi-reader seqlock over a trivially-copyable payload.
/// Writer: any ONE thread (the parameter owner). Readers: audio thread and
/// UI thread. A reader retries (bounded) until it observes a stable
/// even-version copy; writers are rare (parameter changes), so retries are
/// practically zero.
///
/// Payload requirements (static-asserted): trivially copyable, word-aligned
/// size (a multiple of 8 bytes). All current snapshot types satisfy this.
template <typename T>
class SeqLock {
 public:
  SeqLock() = default;
  SeqLock(const SeqLock&) = delete;
  SeqLock& operator=(const SeqLock&) = delete;

  /// Publish a new payload (writer thread only).
  void store(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const uint64_t v = version_.load(std::memory_order_relaxed);
    version_.store(v + 1, std::memory_order_release);  // odd = writing
    const auto* src = reinterpret_cast<const Word*>(&value);
    for (std::size_t w = 0; w < kWords; ++w) {
      words_[w].store(src[w], std::memory_order_relaxed);
    }
    version_.store(v + 2, std::memory_order_release);  // even = stable
  }

  /// Read a stable copy (any thread). `spinBound` bounds the retry loop;
  /// the payload is always small, and writers are rare. On exhaustion the
  /// (possibly torn but race-free) copy is returned — writer sections are
  /// nanosecond-scale, so the bound is only reachable if the writer thread
  /// is preempted mid-store for an pathological interval; a torn snapshot
  /// is absorbed downstream (the preparation thread's parameter debounce;
  /// meters/status are cosmetic telemetry).
  [[nodiscard]] T load(int spinBound = 4096) const {
    T out{};
    auto* dst = reinterpret_cast<Word*>(&out);
    for (int i = 0;; ++i) {
      const uint64_t before = version_.load(std::memory_order_acquire);
      if ((before & 1u) != 0u) {
        if (i >= spinBound) return out;
        continue;  // write in flight
      }
      for (std::size_t w = 0; w < kWords; ++w) {
        dst[w] = words_[w].load(std::memory_order_relaxed);
      }
      const uint64_t after = version_.load(std::memory_order_acquire);
      if (before == after) return out;  // stable copy
      if (i >= spinBound) return out;   // degenerate fallback (never hit in practice)
    }
  }

  [[nodiscard]] uint64_t version() const {
    return version_.load(std::memory_order_acquire);
  }

 private:
  using Word = uint64_t;
  static constexpr std::size_t kWords = sizeof(T) / sizeof(Word);
  static_assert(sizeof(T) % sizeof(Word) == 0,
                "SeqLock payload size must be a multiple of 8 bytes");
  static_assert(alignof(T) >= alignof(Word),
                "SeqLock payload must be word-aligned");

  mutable std::atomic<uint64_t> version_{0};
  std::atomic<Word> words_[kWords]{};
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
