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

long count() { return g_count.load(std::memory_order_relaxed); }
void arm(bool on) { g_armed.store(on, std::memory_order_relaxed); }
void reset() { g_count.store(0, std::memory_order_relaxed); }

}  // namespace allocs
}  // namespace octavox

void* operator new(std::size_t sz) {
    if (octavox::allocs::g_armed.load(std::memory_order_relaxed))
        octavox::allocs::g_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t sz) { return operator new(sz); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
