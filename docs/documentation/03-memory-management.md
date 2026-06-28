# Memory Management

Memory management is one of the most important design decisions in a game engine. MyEngine uses a hierarchy of custom allocators built on top of OS virtual memory, avoiding the standard C/C++ heap (`new`/`malloc`) entirely. This gives the engine full control over allocation patterns, cache behavior, and memory lifetimes.

## Table of Contents

- [Why Custom Allocators?](#why-custom-allocators)
- [The Memory Hierarchy](#the-memory-hierarchy)
- [The Allocator Interface (`allocator.h`)](#the-allocator-interface-allocatorh)
  - [Function-Pointer Design](#function-pointer-design)
  - [Alignment](#alignment)
- [Arena Allocator (`arena.h`)](#arena-allocator-arenah)
  - [How a Bump Allocator Works](#how-a-bump-allocator-works)
  - [Memory Layout](#memory-layout)
  - [When to Use an Arena](#when-to-use-an-arena)
  - [Implementation](#implementation)
- [Pool Allocator (`pool.h`)](#pool-allocator-poolh)
  - [How a Free-List Pool Works](#how-a-free-list-pool-works)
  - [The Embedded Free List Trick](#the-embedded-free-list-trick)
  - [When to Use a Pool](#when-to-use-a-pool)
  - [Implementation](#implementation-1)
- [Stack Allocator (`stack_alloc.h`)](#stack-allocator-stack_alloch)
  - [How a Stack Allocator Works](#how-a-stack-allocator-works)
  - [The Header Trick](#the-header-trick)
  - [When to Use a Stack Allocator](#when-to-use-a-stack-allocator)
  - [Implementation](#implementation-2)
- [Choosing the Right Allocator](#choosing-the-right-allocator)
- [Typical Engine Memory Layout](#typical-engine-memory-layout)

---

## Why Custom Allocators?

The default C/C++ heap (`new` / `malloc`) is a general-purpose allocator designed to handle any allocation pattern. This generality comes with costs:

1. **Per-allocation overhead.** `malloc` stores metadata (typically 16-32 bytes) alongside every allocation to track its size and state. For many small allocations, metadata can exceed the useful data.

2. **Fragmentation.** Over time, as allocations and frees happen in arbitrary order, the heap develops "holes" of free memory that are too small to satisfy new allocations. This wastes memory and can cause large allocations to fail even when there is plenty of total free memory.

3. **Unpredictable performance.** `malloc` may need to search its free lists, coalesce free blocks, or request more memory from the OS — operations with variable cost. In a game that must render a frame every 16.67 ms (60 FPS), unpredictable allocation cost can cause frame-rate hitches.

4. **Cache pollution.** When allocations are scattered across the heap, accessing them causes cache misses. A contiguous array allocated from an arena is cache-friendly; a linked list of individually `new`-ed nodes is not.

Custom allocators solve all of these problems by trading generality for specialization. If you know your allocation pattern (e.g., "allocate many things, free them all at once"), you can use an allocator designed for exactly that pattern, with zero overhead.

---

## The Memory Hierarchy

```
Application code
       |
       v
Allocator Interface  (ge::Allocator — function pointers)
       |
  +---------+---------+
  |         |         |
Arena     Pool     StackAlloc
  |         |         |
  v         v         v
platform::mem_alloc  (VirtualAlloc wrapper)
       |
       v
   Operating System  (virtual memory manager)
       |
       v
   Physical RAM  (managed by the OS page table)
```

Each allocator reserves a large contiguous block from the OS via `platform::mem_alloc()` at initialization time, then sub-allocates from that block using its own strategy. The OS only sees the initial large allocation — the thousands of small allocations within it are handled entirely in user space, with no system calls.

---

## The Allocator Interface (`allocator.h`)

### Function-Pointer Design

All containers and subsystems in the engine accept an `Allocator` value rather than calling `new`/`malloc` or requiring a specific allocator type. The `Allocator` struct is a type-erased interface based on function pointers:

```cpp
struct Allocator {
    using AllocFn = void* (*)(void* ctx, usize size, usize align);
    using FreeFn  = void  (*)(void* ctx, void* ptr, usize size);

    AllocFn alloc_fn = nullptr;
    FreeFn  free_fn  = nullptr;
    void*   ctx      = nullptr;

    void* alloc(usize size, usize align = 16) {
        return alloc_fn(ctx, size, align);
    }

    void free(void* ptr, usize size = 0) {
        if (free_fn) free_fn(ctx, ptr, size);
    }
};
```

The `ctx` pointer is the "this" pointer of the concrete allocator. When an Arena produces an `Allocator`, it sets `ctx = this`, `alloc_fn` to a function that calls `Arena::alloc()`, and `free_fn` to a no-op (because arenas do not support individual frees).

**Why function pointers instead of virtual functions?** Virtual dispatch requires a vtable pointer in every allocator object (8 bytes on 64-bit) and involves an indirect call through the vtable on every allocation. Function pointers are simpler, equally fast, and the `Allocator` struct is a plain value type that can be copied, stored in arrays, and passed by value without worrying about object lifetimes or slicing.

### Alignment

The `align_up` utility function rounds a value up to the next multiple of a given alignment:

```cpp
inline usize align_up(usize value, usize align) {
    return (value + (align - 1)) & ~(align - 1);
}
```

This is a standard bit-manipulation trick that works only when `align` is a power of two (which it always is for memory alignment). The expression `(align - 1)` creates a bitmask of the low bits, and `~(align - 1)` clears them, rounding up to the next aligned address.

Example with `align = 16` (binary: `10000`):
- `align - 1 = 15` (binary: `01111`)
- `~(align - 1)` (binary: `...10000`)
- `(value + 15) & ~15` rounds `value` up to the next multiple of 16.

Memory alignment is critical because modern CPUs access memory most efficiently at naturally-aligned addresses. Misaligned access can be 2-10x slower or, on some architectures, cause hardware exceptions.

---

## Arena Allocator (`arena.h`)

### How a Bump Allocator Works

The arena (also called a "linear allocator" or "bump allocator") is the simplest possible allocator. It maintains a single pointer (the "offset") that advances forward with each allocation:

```
init(1024):    offset=0
               |
               v
               [----------- 1024 bytes -----------]

alloc(64):     offset=64
                              |
               [##############|--------------------]
               ^alloc 1

alloc(128):    offset=192
                                                   |
               [##############|####################|--]
               ^alloc 1       ^alloc 2

reset():       offset=0
               |
               v
               [----------- 1024 bytes -----------]
               (all allocations logically freed at once)
```

Key properties:
- **Allocation is O(1):** advance the offset, return the pointer. No free lists, no searching.
- **No individual frees:** you cannot free allocation 1 without also freeing allocation 2. All memory is reclaimed at once via `reset()`.
- **Zero fragmentation:** allocations are packed contiguously with only alignment padding between them.
- **Cache-friendly:** sequential allocations are adjacent in memory.

### Memory Layout

```
base_ (from VirtualAlloc)
  |
  v
  +---+---+---+-pad-+---+---+---+-pad-+--- ... ---+
  | A | A | A |     | B | B | B |     |   unused   |
  +---+---+---+-pad-+---+---+---+-pad-+--- ... ---+
  ^                 ^                  ^            ^
  |                 |                  |            |
  alloc A (32B)     alloc B (48B)    offset_      size_
```

The padding between allocations ensures each allocation starts at an aligned address (default alignment is 16 bytes).

### When to Use an Arena

Arenas are ideal when:
- You allocate many things and free them all at once (per-frame scratch data).
- You know the maximum lifetime of all allocations (per-level data).
- You want the fastest possible allocation with no fragmentation.

The engine uses a **permanent arena** (64 MB, never reset) for long-lived data like loaded models and textures, and will use **per-frame scratch arenas** (reset each frame) for temporary data.

### Implementation

```cpp
void Arena::init(usize size) {
    base_   = static_cast<u8*>(platform::mem_alloc(size));
    size_   = size;
    offset_ = 0;
}

void* Arena::alloc(usize size, usize align) {
    const usize aligned_offset = align_up(offset_, align);
    GE_ASSERT(aligned_offset + size <= size_);
    void* ptr = base_ + aligned_offset;
    offset_   = aligned_offset + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;  // All allocations are "freed" — the memory is reusable
}
```

The `free` function for arenas is a no-op: when an `Arena` produces an `Allocator`, the `FreeFn` does nothing. You cannot free individual allocations from an arena.

---

## Pool Allocator (`pool.h`)

### How a Free-List Pool Works

A pool allocator divides its memory into fixed-size blocks. All blocks are the same size, determined at initialization. A "free list" (a linked list of available blocks) tracks which blocks are available.

```
init(block_size=64, block_count=4):

  +----------+----------+----------+----------+
  | block 0  | block 1  | block 2  | block 3  |
  | (free)   | (free)   | (free)   | (free)   |
  | next->1  | next->2  | next->3  | next->null|
  +----------+----------+----------+----------+
  ^
  free_head_

After alloc() x2:

  +----------+----------+----------+----------+
  | block 0  | block 1  | block 2  | block 3  |
  | (used)   | (used)   | (free)   | (free)   |
  |          |          | next->3  | next->null|
  +----------+----------+----------+----------+
                         ^
                         free_head_

After free(block 0):

  +----------+----------+----------+----------+
  | block 0  | block 1  | block 2  | block 3  |
  | (free)   | (used)   | (free)   | (free)   |
  | next->2  |          | next->3  | next->null|
  +----------+----------+----------+----------+
  ^
  free_head_
```

Key properties:
- **Allocation is O(1):** pop the head of the free list.
- **Deallocation is O(1):** push the freed block onto the head of the free list.
- **Zero fragmentation:** all blocks are the same size, so any free block can satisfy any allocation.
- **Fixed size only:** you cannot allocate more or less than `block_size` bytes.

### The Embedded Free List Trick

The pool uses an elegant technique: the free-list pointers are stored **inside the free blocks themselves.** Since free blocks are not being used for user data, the first `sizeof(void*)` bytes of each free block hold a pointer to the next free block.

```cpp
// During init — thread free blocks into a linked list:
for (u32 i = 0; i < block_count_ - 1; ++i) {
    void* current = base_ + i * block_size_;
    void* next    = base_ + (i + 1) * block_size_;
    *reinterpret_cast<void**>(current) = next;  // Store 'next' pointer inside the block
}

// alloc():
void* Pool::alloc() {
    void* block = free_head_;
    free_head_ = *reinterpret_cast<void**>(block);  // Follow the embedded pointer
    return block;
}

// free():
void Pool::free(void* ptr) {
    *reinterpret_cast<void**>(ptr) = free_head_;  // Point freed block to old head
    free_head_ = ptr;                             // New head is the freed block
}
```

This means the free list requires **zero additional memory** — no separate linked-list nodes, no metadata arrays. The cost is that each block must be at least `sizeof(void*)` bytes (8 bytes on 64-bit), which the pool enforces by rounding up small block sizes.

### When to Use a Pool

Pools are ideal for:
- Frequently allocated and freed objects of the same size (particles, components, nodes).
- Cases where you need both fast allocation AND fast individual deallocation.
- Replacing `new`/`delete` pairs for a specific type.

### Implementation

The initialization also ensures blocks are properly aligned:

```cpp
void Pool::init(usize block_size, u32 block_count) {
    // Ensure block_size is at least sizeof(void*) for the embedded free list pointer,
    // and aligned to max_align_t for safe use with any type.
    block_size_ = align_up(
        block_size < sizeof(void*) ? sizeof(void*) : block_size,
        alignof(std::max_align_t)
    );
    // ...
}
```

The `alignof(std::max_align_t)` ensures that every block is aligned for any type, matching the guarantee that `malloc` provides. This is important because the pool does not know what type will be stored in each block.

---

## Stack Allocator (`stack_alloc.h`)

### How a Stack Allocator Works

A stack allocator is similar to an arena (allocations advance a pointer forward), but it additionally supports **LIFO (Last-In, First-Out) frees**. The most recently allocated block can be freed, rolling the pointer back.

```
alloc(A, 32B):
  [AAAA|--- unused ---|]
       ^offset

alloc(B, 64B):
  [AAAA|BBBBBBBB|--- unused ---|]
                ^offset

free(B):           <-- LIFO: must free B before A
  [AAAA|--- unused ---|]
       ^offset

alloc(C, 48B):
  [AAAA|CCCCCC|--- unused ---|]
              ^offset
```

The key constraint is that frees must happen in reverse order. You cannot free `A` while `B` is still allocated. This matches the natural pattern of scope-based allocations: allocate at the start of a function, free when the function returns.

### The Header Trick

To support LIFO frees, the stack allocator secretly prepends a small header to each allocation. The header stores the previous offset, enabling the allocator to "roll back" when a block is freed:

```
Memory layout:

  +------+---------+------+---------+
  | Hdr  | Alloc A | Hdr  | Alloc B |------>
  | prev | data    | prev | data    |
  | =0   |         | =48  |         |
  +------+---------+------+---------+
                                     ^offset

When free(B) is called:
  1. Read B's header: prev_offset = 48
  2. Set offset_ = 48
  Result: B and its header are logically freed
```

Implementation:

```cpp
struct StackHeader {
    usize prev_offset;
};

void* StackAlloc::alloc(usize size, usize align) {
    const usize raw = offset_ + sizeof(StackHeader);
    const usize aligned = align_up(raw, min_align);
    const usize end = aligned + size;
    GE_ASSERT(end <= size_);

    // Place the header immediately before the user data
    auto* header = reinterpret_cast<StackHeader*>(base_ + aligned - sizeof(StackHeader));
    header->prev_offset = offset_;

    offset_ = end;
    return base_ + aligned;
}

void StackAlloc::free(void* ptr) {
    auto* header = reinterpret_cast<StackHeader*>(
        static_cast<u8*>(ptr) - sizeof(StackHeader));
    offset_ = header->prev_offset;
}
```

### When to Use a Stack Allocator

Stack allocators are ideal for:
- Temporary allocations that are scoped to a function or block.
- Recursive algorithms where each level allocates and frees.
- Cases where you need individual frees but the LIFO constraint is naturally satisfied.

---

## Choosing the Right Allocator

| Allocator | Alloc | Free | Fragmentation | Best For |
|-----------|-------|------|---------------|----------|
| **Arena** | O(1) | N/A (bulk reset only) | None | Per-frame scratch, long-lived permanent data |
| **Pool** | O(1) | O(1) | None (fixed blocks) | Frequent alloc/free of same-size objects |
| **Stack** | O(1) | O(1) LIFO only | None | Scoped/nested temporary allocations |
| **`malloc`** | O(1) amortized | O(1) amortized | Yes | Not used in engine code |

The general rule: **use an Arena unless you have a specific reason not to.** Arenas are the simplest, fastest, and most cache-friendly allocator. Use a Pool when you need individual frees of same-sized objects. Use a Stack when you need LIFO scoping.

---

## Typical Engine Memory Layout

```
At startup:
  1. Platform layer: VirtualAlloc several large blocks (64MB+ each)

  2. Permanent Arena (64 MB):
     - Loaded model data (vertices, indices, materials)
     - Texture pixel data (before GPU upload)
     - Type registry entries
     - CVar storage

  3. Per-Frame Scratch Arena (e.g., 4 MB, reset every frame):
     - Temporary string formatting
     - Intermediate computation results
     - Command list building scratch space

  4. Object Pools (sized to expected counts):
     - One pool for each component type (in ECS)
     - One pool for particle instances
     - One pool for audio voice state

All of this memory is owned by the engine, subdivided by the
allocators, and never touches the CRT heap.
```

This design gives the engine deterministic memory behavior: the total memory footprint is established at startup, fragmentation is impossible within each allocator, and allocation cost is constant. These properties are essential for maintaining a consistent frame rate.
