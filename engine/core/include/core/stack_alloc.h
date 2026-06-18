#pragma once
#include "core/types.h"
#include "core/allocator.h"

namespace ge {

class StackAlloc {
public:
    void init(usize size);
    void shutdown();

    void* alloc(usize size, usize align = 16);
    void  free(void* ptr);
    void  reset();

    Allocator allocator();

    usize used()     const { return offset_; }
    usize capacity() const { return size_; }

private:
    u8*   base_   = nullptr;
    usize size_   = 0;
    usize offset_ = 0;
};

} // namespace ge
