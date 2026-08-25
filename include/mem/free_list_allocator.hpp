#pragma once
//
// free_list_allocator.hpp
// -----------------------
// A general-purpose allocator -- the "do anything" kind, like a tiny malloc.
// Unlike the arena and the pool, this one supports allocations of ARBITRARY
// size and can free any single block at any time.
//
// How it works:
//   * One big buffer is carved into a chain of blocks. Every block starts with
//     a small header describing its payload size, whether it is free, and links
//     to the physically adjacent blocks (address-ordered doubly linked list).
//   * allocate(): walk the chain, first-fit -- take the first free block large
//     enough. If it is much larger than needed, SPLIT it so the leftover stays
//     available.
//   * deallocate(): mark the block free and COALESCE it with any free physical
//     neighbours, so repeated alloc/free does not shatter memory into unusable
//     slivers (external fragmentation).
//
// This is the classic teaching design behind real allocators (dlmalloc,
// ptmalloc). It trades the O(1) speed of the pool for full flexibility.

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>

namespace mem {

class FreeListAllocator {
public:
    explicit FreeListAllocator(std::size_t capacity)
        : capacity_(capacity),
          buffer_(static_cast<std::byte*>(::operator new(capacity))) {
        // The entire buffer starts life as one big free block.
        head_        = reinterpret_cast<Block*>(buffer_);
        head_->size  = capacity_ - kHeaderSize;
        head_->free  = true;
        head_->next  = nullptr;
        head_->prev  = nullptr;
    }

    ~FreeListAllocator() { ::operator delete(buffer_); }

    FreeListAllocator(const FreeListAllocator&)            = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;

    // Allocate `size` bytes. Returns nullptr if no block is large enough.
    void* allocate(std::size_t size) noexcept {
        size = align_up(size, kAlign);
        if (size == 0) size = kAlign;

        // First-fit scan.
        for (Block* b = head_; b != nullptr; b = b->next) {
            if (b->free && b->size >= size) {
                split_if_worthwhile(b, size);
                b->free = false;
                return payload_of(b);
            }
        }
        return nullptr;   // no fit
    }

    // Free a block previously returned by allocate().
    void deallocate(void* ptr) noexcept {
        if (ptr == nullptr) return;
        Block* b = block_of(ptr);
        assert(!b->free && "double free detected");
        b->free = true;

        // Merge with the following block if it is also free...
        if (b->next && b->next->free) coalesce(b, b->next);
        // ...and with the preceding block if it is free.
        if (b->prev && b->prev->free) coalesce(b->prev, b);
    }

    // --- introspection helpers, handy for the demo / tests ---------------

    std::size_t capacity() const noexcept { return capacity_; }

    // Number of distinct blocks currently in the chain (used + free).
    std::size_t block_count() const noexcept {
        std::size_t n = 0;
        for (Block* b = head_; b; b = b->next) ++n;
        return n;
    }

    // Largest single free payload available -- a measure of fragmentation.
    std::size_t largest_free_block() const noexcept {
        std::size_t best = 0;
        for (Block* b = head_; b; b = b->next)
            if (b->free && b->size > best) best = b->size;
        return best;
    }

private:
    struct Block {
        std::size_t size;   // payload bytes (excludes this header)
        bool        free;
        Block*      next;   // physically next block (higher address)
        Block*      prev;   // physically previous block (lower address)
    };

    // Header size, rounded up so every payload that follows it is aligned.
    static constexpr std::size_t kAlign      = alignof(std::max_align_t);
    static constexpr std::size_t kHeaderSize =
        (sizeof(Block) + (kAlign - 1)) & ~(kAlign - 1);

    static std::size_t align_up(std::size_t n, std::size_t a) {
        return (n + (a - 1)) & ~(a - 1);
    }

    static std::byte* payload_of(Block* b) {
        return reinterpret_cast<std::byte*>(b) + kHeaderSize;
    }
    static Block* block_of(void* payload) {
        return reinterpret_cast<Block*>(
            static_cast<std::byte*>(payload) - kHeaderSize);
    }

    // If block `b` is large enough to hold `size` PLUS a whole new header and a
    // non-trivial payload, chop the tail off into a fresh free block.
    void split_if_worthwhile(Block* b, std::size_t size) {
        if (b->size < size + kHeaderSize + kAlign) return;   // not worth it

        std::byte* raw   = reinterpret_cast<std::byte*>(b);
        Block*     tail  = reinterpret_cast<Block*>(raw + kHeaderSize + size);
        tail->size = b->size - size - kHeaderSize;
        tail->free = true;

        // Splice `tail` into the physical chain between b and b->next.
        tail->next = b->next;
        tail->prev = b;
        if (b->next) b->next->prev = tail;
        b->next = tail;

        b->size = size;
    }

    // Absorb `right` into `left` (they must be physically adjacent & free).
    void coalesce(Block* left, Block* right) {
        left->size += kHeaderSize + right->size;
        left->next  = right->next;
        if (right->next) right->next->prev = left;
    }

    std::size_t capacity_;
    std::byte*  buffer_;
    Block*      head_;
};

} // namespace mem
