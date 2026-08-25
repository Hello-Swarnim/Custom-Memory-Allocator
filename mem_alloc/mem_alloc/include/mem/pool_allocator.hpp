#pragma once
//
// pool_allocator.hpp
// ------------------
// A fixed-size-block "pool" (a.k.a. free-list) allocator.
//
// Idea: every allocation is the SAME size (chosen up front). We carve one big
// buffer into N equal slots and thread a singly linked "free list" through the
// slots themselves -- each free slot stores a pointer to the next free slot in
// its own bytes, so the bookkeeping costs zero extra memory.
//
//   allocate() = pop the head of the free list           -> O(1)
//   deallocate() = push the block back onto the free list -> O(1)
//
// This is how you allocate millions of same-sized objects (graph nodes, ECS
// components, network packets, particles) far faster than malloc, with no
// fragmentation, because every hole is exactly one object wide.

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>
#include <algorithm>   // std::max

namespace mem {

class PoolAllocator {
public:
    // block_size  : bytes handed out per allocation
    // block_count : number of blocks in the pool
    PoolAllocator(std::size_t block_size, std::size_t block_count)
        : block_count_(block_count),
          free_count_(block_count) {
        // A block must be big enough to store a "next free" pointer while it
        // sits on the free list, and correctly aligned for a pointer.
        block_size_ = std::max(block_size, sizeof(Node));
        // Round block size up to pointer alignment so the embedded Node* is aligned.
        block_size_ = align_up(block_size_, alignof(Node));

        buffer_ = static_cast<std::byte*>(::operator new(block_size_ * block_count_));
        build_free_list();
    }

    ~PoolAllocator() { ::operator delete(buffer_); }

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Hand out one block. Returns nullptr when the pool is exhausted.
    void* allocate() noexcept {
        if (free_list_ == nullptr) return nullptr;   // pool exhausted
        Node* head = free_list_;
        free_list_ = head->next;   // pop
        --free_count_;
        return head;
    }

    // Return a block to the pool. `ptr` must have come from this pool.
    void deallocate(void* ptr) noexcept {
        if (ptr == nullptr) return;
        assert(owns(ptr) && "pointer did not come from this pool");
        Node* node = static_cast<Node*>(ptr);
        node->next = free_list_;   // push
        free_list_ = node;
        ++free_count_;
    }

    std::size_t block_size()   const noexcept { return block_size_; }
    std::size_t block_count()  const noexcept { return block_count_; }
    std::size_t free_blocks()  const noexcept { return free_count_; }
    std::size_t used_blocks()  const noexcept { return block_count_ - free_count_; }

private:
    // Each free slot is reinterpreted as a Node that points to the next free slot.
    struct Node { Node* next; };

    void build_free_list() {
        free_list_ = nullptr;
        // Link every block into the list, front to back. Building back-to-front
        // would also work; this order hands out low addresses first.
        for (std::size_t i = block_count_; i-- > 0; ) {
            Node* node = reinterpret_cast<Node*>(buffer_ + i * block_size_);
            node->next = free_list_;
            free_list_ = node;
        }
    }

    bool owns(const void* p) const noexcept {
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        auto base = reinterpret_cast<std::uintptr_t>(buffer_);
        return addr >= base && addr < base + block_size_ * block_count_;
    }

    static std::size_t align_up(std::size_t n, std::size_t alignment) {
        std::size_t mask = alignment - 1;
        return (n + mask) & ~mask;
    }

    std::size_t block_size_;
    std::size_t block_count_;
    std::size_t free_count_;
    std::byte*  buffer_;
    Node*       free_list_ = nullptr;
};

} // namespace mem
