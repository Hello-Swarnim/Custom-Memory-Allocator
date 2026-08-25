//
// tests.cpp
// ---------
// A small hand-rolled test suite (no external framework) that proves each
// allocator behaves correctly: alignment, reuse, splitting, coalescing, and
// STL integration. Run: ./tests   (exit code 0 = all passed).
//

#include "mem/arena_allocator.hpp"
#include "mem/pool_allocator.hpp"
#include "mem/free_list_allocator.hpp"
#include "mem/stl_allocator.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>
#include <set>
#include <random>

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            std::printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

static bool is_aligned(const void* p, std::size_t a) {
    return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0;
}

// -------------------------------------------------------------------------
static void test_arena() {
    std::printf("[arena] linear allocation, alignment, reset\n");
    mem::ArenaAllocator arena(1024);

    void* a = arena.allocate(100);
    void* b = arena.allocate(200);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(a != b);
    // b must sit after a.
    CHECK(reinterpret_cast<char*>(b) > reinterpret_cast<char*>(a));

    // Alignment request honoured.
    void* c = arena.allocate(1, 64);
    CHECK(is_aligned(c, 64));

    // Exhaustion returns nullptr rather than corrupting memory.
    CHECK(arena.allocate(100000) == nullptr);

    std::size_t before = arena.used();
    CHECK(before > 0);
    arena.reset();
    CHECK(arena.used() == 0);

    // After reset we hand out the same base address again.
    void* d = arena.allocate(100);
    CHECK(d == a);
}

// -------------------------------------------------------------------------
static void test_pool() {
    std::printf("[pool] O(1) alloc/free, exhaustion, reuse\n");
    const std::size_t N = 8;
    mem::PoolAllocator pool(sizeof(std::uint64_t), N);
    CHECK(pool.free_blocks() == N);

    std::vector<void*> blocks;
    for (std::size_t i = 0; i < N; ++i) {
        void* p = pool.allocate();
        CHECK(p != nullptr);
        CHECK(is_aligned(p, alignof(std::uint64_t)));
        blocks.push_back(p);
    }
    // All blocks distinct.
    std::set<void*> uniq(blocks.begin(), blocks.end());
    CHECK(uniq.size() == N);

    // Pool exhausted.
    CHECK(pool.allocate() == nullptr);
    CHECK(pool.used_blocks() == N);

    // Free one and get it straight back (LIFO reuse).
    pool.deallocate(blocks[3]);
    CHECK(pool.free_blocks() == 1);
    void* again = pool.allocate();
    CHECK(again == blocks[3]);

    // Writing through the blocks doesn't clobber neighbours.
    for (std::size_t i = 0; i < N; ++i)
        *static_cast<std::uint64_t*>(blocks[i]) = i;
    for (std::size_t i = 0; i < N; ++i)
        CHECK(*static_cast<std::uint64_t*>(blocks[i]) == i);
}

// -------------------------------------------------------------------------
static void test_free_list() {
    std::printf("[free-list] split, arbitrary sizes, coalesce\n");
    mem::FreeListAllocator fl(4096);
    CHECK(fl.block_count() == 1);   // one big block to start

    void* a = fl.allocate(64);
    void* b = fl.allocate(128);
    void* c = fl.allocate(256);
    CHECK(a && b && c);
    CHECK(is_aligned(a, alignof(std::max_align_t)));
    // Allocating split the big block into pieces.
    CHECK(fl.block_count() >= 4);   // a | b | c | remaining tail

    // Free the middle block: it should NOT merge (neighbours still in use).
    fl.deallocate(b);
    std::size_t frag_count = fl.block_count();

    // Free a and c; now everything is free and must coalesce back toward one
    // big block. The largest free block should grow substantially.
    fl.deallocate(a);
    fl.deallocate(c);
    CHECK(fl.block_count() < frag_count);          // blocks merged
    CHECK(fl.largest_free_block() > 256 + 128);    // hole reunited

    // Re-request the full-ish capacity to confirm coalescing truly reunited it.
    void* big = fl.allocate(3000);
    CHECK(big != nullptr);
    fl.deallocate(big);

    // Over-allocation fails cleanly.
    CHECK(fl.allocate(1 << 20) == nullptr);
}

// -------------------------------------------------------------------------
// Randomized stress test for the general allocator. Runs a long, unpredictable
// sequence of allocations and frees -- the kind of chaotic pattern that shakes
// out bugs a scripted test would never reach.
//
// The key idea: every live block is filled with a unique "tag" byte. If the
// allocator ever hands out memory that OVERLAPS a block still in use, writing
// the new tag corrupts the old block's tag, and the integrity check catches it.
// This is the single most important property of an allocator -- that two live
// allocations never share memory -- and it's exactly what randomized testing is
// good at proving. A fixed RNG seed keeps the run reproducible.
// -------------------------------------------------------------------------
static void test_free_list_stress() {
    std::printf("[free-list stress] 200k randomized ops, integrity + coalescing\n");
    const std::size_t CAP = 1 << 16;   // 64 KiB
    mem::FreeListAllocator fl(CAP);

    std::mt19937 rng(12345);   // fixed seed => deterministic, reproducible test
    std::uniform_int_distribution<int>         coin(0, 99);
    std::uniform_int_distribution<std::size_t> size_dist(1, 512);

    struct Live { std::uint8_t* ptr; std::size_t size; std::uint8_t tag; };
    std::vector<Live> live;
    std::uint8_t next_tag     = 1;
    bool         integrity_ok = true;
    long         allocs = 0, frees = 0, oom = 0;

    // Confirm a block still holds its own tag byte-for-byte.
    auto check_block = [&](const Live& b) {
        for (std::size_t i = 0; i < b.size; ++i)
            if (b.ptr[i] != b.tag) { integrity_ok = false; return; }
    };

    const int ITERS = 200000;
    for (int it = 0; it < ITERS && integrity_ok; ++it) {
        bool do_alloc = live.empty() || coin(rng) < 60;   // ~60% allocate
        if (do_alloc) {
            std::size_t s = size_dist(rng);
            auto* p = static_cast<std::uint8_t*>(fl.allocate(s));
            if (p) {
                std::uint8_t tag = next_tag++;
                if (next_tag == 0) next_tag = 1;   // never use 0
                for (std::size_t i = 0; i < s; ++i) p[i] = tag;   // stamp the block
                live.push_back({p, s, tag});
                ++allocs;
            } else {
                ++oom;   // legitimately full/fragmented -- not an error
            }
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            std::size_t idx = pick(rng);
            check_block(live[idx]);            // must be intact right before free
            fl.deallocate(live[idx].ptr);
            live[idx] = live.back();           // O(1) remove from tracking list
            live.pop_back();
            ++frees;
        }

        // Every ~1024 ops, sweep ALL live blocks for corruption.
        if ((it & 0x3FF) == 0)
            for (const auto& b : live) { check_block(b); if (!integrity_ok) break; }
    }

    // Free whatever is left; a correct allocator must coalesce it all back.
    for (const auto& b : live) fl.deallocate(b.ptr);
    live.clear();

    std::printf("        (%ld allocs, %ld frees, %ld out-of-memory events)\n",
                allocs, frees, oom);
    CHECK(integrity_ok);                            // no block ever corrupted => no overlaps
    CHECK(allocs > 1000);                          // the workload really exercised it
    CHECK(frees  > 1000);
    CHECK(fl.block_count() == 1);                  // fully coalesced back to a single block
    CHECK(fl.largest_free_block() >= CAP - 256);   // ~all memory recovered
}

// -------------------------------------------------------------------------
static void test_stl_integration() {
    std::printf("[stl] std::vector backed by FreeListAllocator\n");
    mem::FreeListAllocator backend(1 << 20);   // 1 MiB
    using Alloc = mem::StlAllocator<int, mem::FreeListAllocator>;

    std::vector<int, Alloc> v{Alloc(backend)};
    for (int i = 0; i < 1000; ++i) v.push_back(i);

    long long sum = 0;
    for (int x : v) sum += x;
    CHECK(v.size() == 1000);
    CHECK(sum == 999LL * 1000 / 2);   // 0 + 1 + ... + 999
}

// -------------------------------------------------------------------------
int main() {
    std::printf("Running allocator tests\n-----------------------\n");
    test_arena();
    test_pool();
    test_free_list();
    test_free_list_stress();
    test_stl_integration();

    std::printf("-----------------------\n%d checks, %d failure(s)\n",
                g_checks, g_failures);
    if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
