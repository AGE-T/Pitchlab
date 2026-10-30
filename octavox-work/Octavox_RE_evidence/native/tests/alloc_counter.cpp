// OCTAVOX round 12 -- steady-state allocation counter (test/bench harness).
// Overrides the global operator new/delete; the suites assert that the
// allocation count does NOT move between two points inside process() after
// warm-up (steady state is allocation-free by design).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace octavox {
namespace allocs {

std::atomic<long> g_count{0};
std::atomic<bool> g_armed{false};
// R18-P1: always-on process-wide totals (never armed, never a gate).
// They exist so the --diag trace can prove the per-case allocation state
// stays bounded across the aggregate suite (the armed g_count semantics
// above are unchanged and remain what the cases assert against).
std::atomic<long> g_total_new{0};
std::atomic<long> g_total_del{0};

long count() { return g_count.load(std::memory_order_relaxed); }
void arm(bool on) { g_armed.store(on, std::memory_order_relaxed); }
void reset() { g_count.store(0, std::memory_order_relaxed); }
long totalNew() { return g_total_new.load(std::memory_order_relaxed); }
long totalDelete() { return g_total_del.load(std::memory_order_relaxed); }

}  // namespace allocs
}  // namespace octavox

void* operator new(std::size_t sz) {
    octavox::allocs::g_total_new.fetch_add(1, std::memory_order_relaxed);
    if (octavox::allocs::g_armed.load(std::memory_order_relaxed))
        octavox::allocs::g_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    octavox::allocs::g_total_del.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    octavox::allocs::g_total_del.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void* operator new[](std::size_t sz) { return operator new(sz); }
void operator delete[](void* p) noexcept {
    octavox::allocs::g_total_del.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    octavox::allocs::g_total_del.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
