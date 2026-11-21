// Pitch Lab — Task 28 allocation audit implementation (see proto_alloc.h
// for the boundary warning). Counting is single-threaded by design: the
// task28 executables drive engines from one thread only.

#include "prototypes/proto_alloc.h"

#include <atomic>
#include <cstdlib>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
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
#if defined(_MSC_VER)
  // MSVC has no std::aligned_alloc (the CRT cannot free it with free()):
  // the documented _aligned_malloc/_aligned_free pair.
  void* p = _aligned_malloc((size + a - 1) / a * a, a < sizeof(void*) ? sizeof(void*) : a);
#else
  void* p = std::aligned_alloc(a < sizeof(void*) ? sizeof(void*) : a,
                               (size + a - 1) / a * a);
#endif
  if (!p) throw std::bad_alloc();
  return p;
}

#if defined(_MSC_VER)
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  _aligned_free(p);
}
#else
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}
#endif
