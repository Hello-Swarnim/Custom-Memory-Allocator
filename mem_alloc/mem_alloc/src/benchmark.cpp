//
// benchmark.cpp
// -------------
// Times our custom allocators against the system allocator (new/delete) on the
// workloads each is designed for. The whole point of a custom allocator is
// speed for a specific access pattern -- this shows the payoff.
//
// Run: ./benchmark
//

#include "mem/arena_allocator.hpp"
#include "mem/pool_allocator.hpp"
#include "mem/free_list_allocator.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

static double ms_since(Clock::time_point t0) {
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// Prevent the optimizer from deleting work whose result we "don't use".
static volatile std::uint64_t g_sink = 0;

// -------------------------------------------------------------------------
// Workload 1: allocate + free a huge number of same-sized objects.
// This is where the POOL allocator shines vs new/delete.
// -------------------------------------------------------------------------
static void bench_pool(std::size_t iters) {
    std::printf("\n== Fixed-size object churn (%zu allocations of 64 B) ==\n", iters);

    // --- system new/delete ---
    {
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < iters; ++i) {
            auto* p = new std::uint64_t[8];   // 64 bytes
            p[0] = i;
            acc += p[0];
            delete[] p;
        }
        g_sink = acc;
        std::printf("  new/delete       : %8.2f ms\n", ms_since(t0));
    }

    // --- pool allocator ---
    {
        mem::PoolAllocator pool(64, 1);   // single reused block is enough here
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < iters; ++i) {
            auto* p = static_cast<std::uint64_t*>(pool.allocate());
            p[0] = i;
            acc += p[0];
            pool.deallocate(p);
        }
        g_sink = acc;
        std::printf("  PoolAllocator    : %8.2f ms\n", ms_since(t0));
    }
}

// -------------------------------------------------------------------------
// Workload 2: allocate many objects that all die together (per-frame scratch).
// This is where the ARENA shines: allocation is a pointer bump and the whole
// batch is freed in O(1) with reset().
// -------------------------------------------------------------------------
static void bench_arena(std::size_t objects, std::size_t frames) {
    std::printf("\n== Batch scratch memory (%zu objs x %zu frames) ==\n",
                objects, frames);

    const std::size_t obj = 32;

    // --- system new/delete ---
    {
        std::vector<void*> live;
        live.reserve(objects);
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t i = 0; i < objects; ++i) {
                auto* p = static_cast<std::uint8_t*>(::operator new(obj));
                p[0] = static_cast<std::uint8_t>(i);
                acc += p[0];
                live.push_back(p);
            }
            for (void* p : live) ::operator delete(p);
            live.clear();
        }
        g_sink = acc;
        std::printf("  new/delete       : %8.2f ms\n", ms_since(t0));
    }

    // --- arena allocator ---
    {
        mem::ArenaAllocator arena(objects * 64 + 1024);
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t i = 0; i < objects; ++i) {
                auto* p = static_cast<std::uint8_t*>(arena.allocate(obj));
                p[0] = static_cast<std::uint8_t>(i);
                acc += p[0];
            }
            arena.reset();   // free the entire frame at once
        }
        g_sink = acc;
        std::printf("  ArenaAllocator   : %8.2f ms\n", ms_since(t0));
    }
}

// -------------------------------------------------------------------------
// Workload 3: mixed-size alloc/free through the general free-list allocator,
// compared with new/delete doing the same variable-size work.
// -------------------------------------------------------------------------
static void bench_free_list(std::size_t iters) {
    std::printf("\n== Mixed-size alloc/free (%zu ops) ==\n", iters);
    const std::size_t sizes[] = {16, 48, 128, 300, 512};
    const std::size_t nsizes  = sizeof(sizes) / sizeof(sizes[0]);

    // --- system new/delete ---
    {
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < iters; ++i) {
            std::size_t s = sizes[i % nsizes];
            auto* p = static_cast<std::uint8_t*>(::operator new(s));
            p[0] = static_cast<std::uint8_t>(i);
            acc += p[0];
            ::operator delete(p);
        }
        g_sink = acc;
        std::printf("  new/delete       : %8.2f ms\n", ms_since(t0));
    }

    // --- free-list allocator ---
    {
        mem::FreeListAllocator fl(1 << 16);
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < iters; ++i) {
            std::size_t s = sizes[i % nsizes];
            auto* p = static_cast<std::uint8_t*>(fl.allocate(s));
            p[0] = static_cast<std::uint8_t>(i);
            acc += p[0];
            fl.deallocate(p);
        }
        g_sink = acc;
        std::printf("  FreeListAllocator: %8.2f ms\n", ms_since(t0));
    }
}

int main() {
    std::printf("Custom allocator benchmarks (lower = faster)\n");
    std::printf("============================================\n");
    bench_pool(20'000'000);
    bench_arena(10'000, 5'000);
    bench_free_list(10'000'000);
    std::printf("\n(sink=%llu)\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
