#pragma once
//
// arena_allocator.hpp
// -------------------
// A linear ("bump" / "arena") allocator.
//
// Idea: grab one big block of memory up front, then hand out chunks of it by
// simply moving a cursor forward. Allocation is a pointer bump + an alignment
// round-up, so it is about as fast as allocation gets. There is no per-object
// free: you throw away *everything* at once with reset(). This is exactly the
// pattern game engines and compilers use for per-frame / per-request scratch
// memory, where the lifetime of many small objects is identical.
//
// Trade-off: O(1) allocation, zero fragmentation, near-zero bookkeeping, but
// you cannot free a single object -- only the whole arena.

#include <cstddef>   // std::size_t, std::max_align_t
#include <cstdint>   // std::uintptr_t
#include <cassert>
#include <new>       // ::operator new / delete

namespace mem {

class ArenaAllocator {
public:
    // Reserve `capacity` bytes from the system up front.
    explicit ArenaAllocator(std::size_t capacity)
        : capacity_(capacity),
          buffer_(static_cast<std::byte*>(::operator new(capacity))),
          offset_(0) {}

    ~ArenaAllocator() {
        ::operator delete(buffer_);
    }

    // Non-copyable: it owns a raw buffer.
    ArenaAllocator(const ArenaAllocator&)            = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Movable.
    ArenaAllocator(ArenaAllocator&& other) noexcept
        : capacity_(other.capacity_),
          buffer_(other.buffer_),
          offset_(other.offset_) {
        other.buffer_   = nullptr;
        other.capacity_ = 0;
        other.offset_   = 0;
    }

    // Allocate `size` bytes aligned to `alignment` (default: max alignment).
    // Returns nullptr if the arena is full.
    void* allocate(std::size_t size,
                   std::size_t alignment = alignof(std::max_align_t)) {
        // Round the current cursor up to the requested alignment.
        std::uintptr_t current =
            reinterpret_cast<std::uintptr_t>(buffer_) + offset_;
        std::uintptr_t aligned = align_up(current, alignment);
        std::size_t    padding = aligned - current;

        if (offset_ + padding + size > capacity_) {
            return nullptr;   // out of space
        }

        offset_ += padding + size;
        return reinterpret_cast<void*>(aligned);
    }

    // No-op: individual frees are not supported by an arena. Kept so the
    // interface mirrors the other allocators.
    void deallocate(void* /*ptr*/) noexcept {}

    // Reclaim the whole arena at once. Any pointer handed out before this call
    // is now dangling -- that is the contract of an arena.
    void reset() noexcept { offset_ = 0; }

    std::size_t used()      const noexcept { return offset_; }
    std::size_t capacity()  const noexcept { return capacity_; }
    std::size_t remaining() const noexcept { return capacity_ - offset_; }

private:
    // Round `n` up to the next multiple of `alignment` (alignment must be a
    // power of two -- true for all C++ fundamental alignments).
    static std::uintptr_t align_up(std::uintptr_t n, std::size_t alignment) {
        assert((alignment & (alignment - 1)) == 0 && "alignment must be power of two");
        std::uintptr_t mask = alignment - 1;
        return (n + mask) & ~mask;
    }

    std::size_t capacity_;
    std::byte*  buffer_;
    std::size_t offset_;
};

} // namespace mem
