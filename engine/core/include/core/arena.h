#pragma once
#include "core/types.h"
#include "core/allocator.h"

namespace ge {

class Arena {
public:
    void init(usize size);
    void shutdown();

    void* alloc(usize size, usize align = 16);

    template <typename T>
    T* alloc_n(usize count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    void reset();

    Allocator allocator();

    usize used()     const { return offset_; }
    usize capacity() const { return size_; }

private:
    u8*   base_   = nullptr;
    usize size_   = 0;
    usize offset_ = 0;
};

} // namespace ge
