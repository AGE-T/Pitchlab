// Pitch Lab — Task 28 allocation audit implementation (see proto_alloc.h
// for the boundary warning). Counting is single-threaded by design: the
// task28 executables drive engines from one thread only.

#include "prototypes/proto_alloc.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace pitchlab::proto {

namespace {
std::atomic<int64_t> g_count{0};
std::atomic<int64_t> g_bytes{0};
}  // namespace

void allocAuditReset() {
  g_count.store(0, std::memory_order_relaxed);
  g_bytes.store(0, std::memory_order_relaxed);
}

int64_t allocAuditCount() { return g_count.load(std::memory_order_relaxed); }

int64_t allocAuditBytes() { return g_bytes.load(std::memory_order_relaxed); }

}  // namespace pitchlab::proto

// ---- global replacements (active in every binary linking this TU) ----

namespace {
void countAlloc(std::size_t size) {
  pitchlab::proto::g_count.fetch_add(1, std::memory_order_relaxed);
  pitchlab::proto::g_bytes.fetch_add(static_cast<int64_t>(size),
                                     std::memory_order_relaxed);
}
}  // namespace

void* operator new(std::size_t size) {
  countAlloc(size);
  if (size == 0) size = 1;
  void* p = std::malloc(size);
  if (!p) throw std::bad_alloc();
  return p;
}

void operator delete(void* p) noexcept { std::free(p); }

void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void* operator new(std::size_t size, std::align_val_t align) {
  countAlloc(size);
  if (size == 0) size = 1;
  const std::size_t a = static_cast<std::size_t>(align);
  void* p = std::aligned_alloc(a < sizeof(void*) ? sizeof(void*) : a,
                               (size + a - 1) / a * a);
  if (!p) throw std::bad_alloc();
  return p;
}

void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}
