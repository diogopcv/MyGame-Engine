#pragma once
#include "core/types.h"

// A linear (a.k.a. arena / bump) allocator: the simplest and fastest scheme
// in any engine. It owns one contiguous block and hands out aligned slices by
// advancing an offset. There is no per-allocation free — you reset() the
// whole arena at once. This is the backbone of data-oriented memory
// management: per-frame scratch arenas, per-level arenas, etc.
namespace ge {

class Arena {
public:
    // Grabs `size` bytes from the platform's virtual memory.
    void init(usize size);

    // Returns the backing memory to the OS.
    void shutdown();

    // Returns `size` bytes aligned to `align` (must be a power of two).
    // Asserts if the arena is exhausted.
    void* alloc(usize size, usize align = 16);

    // Typed convenience: arena.alloc_n<Mesh>(128).
    template <typename T>
    T* alloc_n(usize count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    // Frees everything in O(1) by rewinding the offset. The backing memory
    // stays reserved so the next frame reuses the same pages.
    void reset();

    usize used()     const { return offset_; }
    usize capacity() const { return size_; }

private:
    u8*   base_   = nullptr;
    usize size_   = 0;
    usize offset_ = 0;
};

} // namespace ge
