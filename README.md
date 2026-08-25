# Custom Memory Allocators in C++

A from-scratch implementation of three classic memory allocators, an STL adapter
that lets standard containers use them, a correctness test suite, and a
benchmark harness comparing them against the system allocator (`new`/`delete`).

The point of the project is to show *why* custom allocators exist: the standard
allocator is general-purpose and therefore slow for specific, predictable access
patterns. By exploiting structure in how memory is used, each allocator here
beats `new`/`delete` by **4–17×** on the workload it is designed for.

Everything is header-only C++17 with no dependencies.

## Measured results

Compiled with `g++ -O2`, 20M / 50M-operation workloads (your numbers will vary
by machine, the ratios won't):

| Workload                          | `new`/`delete` | Custom       | Speed-up |
|-----------------------------------|---------------:|-------------:|---------:|
| Fixed-size object churn (64 B)    |      412.9 ms  |  **26.4 ms** |   ~15×   |
| Per-frame batch scratch memory    |     1285.0 ms  |  **76.4 ms** |   ~17×   |
| Mixed-size alloc/free             |      190.9 ms  |  **49.4 ms** |    ~4×   |

Correctness is checked by a 54-assertion suite plus a **200,000-operation
randomized stress test** on the general allocator, and verified clean under
**AddressSanitizer** and **UndefinedBehaviorSanitizer** (no leaks, no
out-of-bounds access, no UB).

## The three allocators

### 1. Arena / bump allocator — `include/mem/arena_allocator.hpp`
Grabs one big block up front and hands out memory by advancing a cursor. An
allocation is a pointer bump plus an alignment round-up — about as fast as
allocation can be. There is no per-object free; you discard *everything* at once
with `reset()`. This is the pattern game engines and compilers use for
per-frame / per-request scratch memory.

*O(1) allocation, zero fragmentation, no individual free.*

### 2. Pool / fixed-block allocator — `include/mem/pool_allocator.hpp`
Every allocation is the same size. The buffer is carved into equal slots, and a
singly linked "free list" is threaded *through the free slots themselves* (each
free slot stores the pointer to the next free slot in its own bytes, so the
bookkeeping costs zero extra memory). Allocate = pop the list; free = push the
list.

*O(1) allocate and free, zero fragmentation, single fixed size only.*

### 3. Free-list general allocator — `include/mem/free_list_allocator.hpp`
The "do anything" allocator — a tiny `malloc`. Supports arbitrary sizes and
freeing any block at any time. Memory is a chain of blocks with headers;
allocation does **first-fit + splitting** (chop a large free block down to size),
and freeing does **coalescing** (merge adjacent free blocks back together so
repeated use doesn't shatter memory into unusable slivers). This is the classic
design behind real allocators like dlmalloc.

*Full flexibility; the price is a scan on allocate instead of O(1).*

### STL adapter — `include/mem/stl_allocator.hpp`
Wraps any of the above in the `Allocator` interface the STL expects, so you can
drop them straight into standard containers:

```cpp
mem::FreeListAllocator backend(1 << 20);   // 1 MiB of our own memory
using Alloc = mem::StlAllocator<int, mem::FreeListAllocator>;

std::vector<int, Alloc> v{Alloc(backend)};
v.push_back(42);   // this element now lives inside our allocator, not the heap
```

## Build & run

```sh
make test     # build and run the correctness suite (54 checks + stress test)
make bench    # build and run the benchmarks
make          # build both into ./bin
make clean
```

Requires any C++17 compiler (`g++` or `clang++`). Override with e.g.
`make CXX=clang++`.

To reproduce the sanitizer run:

```sh
g++ -std=c++17 -g -fsanitize=address,undefined -Iinclude src/tests.cpp -o tests_asan && ./tests_asan
```

## Layout

```
include/mem/
  arena_allocator.hpp       linear bump allocator
  pool_allocator.hpp        fixed-size free-list pool
  free_list_allocator.hpp   general-purpose allocator (split + coalesce)
  stl_allocator.hpp         std::allocator-compatible adapter
src/
  tests.cpp                 correctness suite + randomized stress test
  benchmark.cpp             timing vs new/delete
Makefile
```

## Concepts demonstrated

Pointer arithmetic and alignment (power-of-two round-up), RAII and the Rule of
Five (owning a raw buffer, deleted copies, defined move), embedding a free list
inside free memory, boundary-tag block splitting and coalescing, the STL
allocator interface and `rebind`, measuring/avoiding external fragmentation, and
randomized stress testing under a memory-integrity invariant (every live block
holds a unique tag byte, so any overlapping allocation is detected as
corruption).
