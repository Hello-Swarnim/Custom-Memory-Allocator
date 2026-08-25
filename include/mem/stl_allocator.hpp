#pragma once
//
// stl_allocator.hpp
// -----------------
// An adapter that lets any of our raw allocators plug into the STL.
//
// std::vector, std::list, std::map etc. do not call new/delete directly -- they
// go through an "Allocator" type parameter that must satisfy a specific
// interface (allocate(n) / deallocate(p, n), rebind, etc.). This wrapper turns
// one of our byte-level allocators into exactly that, so you can write:
//
//     mem::FreeListAllocator backend(1 << 20);
//     std::vector<int, mem::StlAllocator<int, mem::FreeListAllocator>> v{backend};
//
// and every element of `v` now lives inside our own memory, not the system heap.
//
// The Backend must provide:  void* allocate(std::size_t bytes);
//                            void  deallocate(void* p);

#include <cstddef>
#include <new>       // std::bad_alloc

namespace mem {

template <typename T, typename Backend>
class StlAllocator {
public:
    using value_type = T;

    // std::allocator_traits uses this to make an allocator for a different type
    // (e.g. std::list allocates nodes, not T's).
    template <typename U>
    struct rebind { using other = StlAllocator<U, Backend>; };

    explicit StlAllocator(Backend& backend) noexcept : backend_(&backend) {}

    // Converting copy: same backend, different element type.
    template <typename U>
    StlAllocator(const StlAllocator<U, Backend>& other) noexcept
        : backend_(other.backend()) {}

    T* allocate(std::size_t n) {
        void* p = backend_->allocate(n * sizeof(T));
        if (p == nullptr) throw std::bad_alloc();
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t /*n*/) noexcept {
        backend_->deallocate(p);
    }

    Backend* backend() const noexcept { return backend_; }

private:
    Backend* backend_;
};

// Two adapters are equal iff they draw from the same backend, so containers can
// move elements allocated by one into another.
template <typename T, typename U, typename B>
bool operator==(const StlAllocator<T, B>& a, const StlAllocator<U, B>& b) noexcept {
    return a.backend() == b.backend();
}
template <typename T, typename U, typename B>
bool operator!=(const StlAllocator<T, B>& a, const StlAllocator<U, B>& b) noexcept {
    return !(a == b);
}

} // namespace mem
