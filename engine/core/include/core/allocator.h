#pragma once
#include "core/types.h"

namespace ge {

inline usize align_up(usize value, usize align) {
    return (value + (align - 1)) & ~(align - 1);
}

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

    template <typename T>
    T* alloc_t() {
        return static_cast<T*>(alloc(sizeof(T), alignof(T)));
    }

    template <typename T>
    T* alloc_n(usize count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    bool is_valid() const { return alloc_fn != nullptr; }
};

} // namespace ge
