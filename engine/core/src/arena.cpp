#include "core/arena.h"
#include "core/assert.h"
#include "platform/memory.h"   // core -> platform: the allowed dependency direction

namespace ge {

namespace {
    // Round `value` up to the next multiple of `align` (power of two).
    usize align_up(usize value, usize align) {
        return (value + (align - 1)) & ~(align - 1);
    }
}

void Arena::init(usize size) {
    base_   = static_cast<u8*>(platform::mem_alloc(size));
    GE_ASSERT(base_ != nullptr);
    size_   = size;
    offset_ = 0;
}

void Arena::shutdown() {
    if (base_) {
        platform::mem_free(base_, size_);
        base_   = nullptr;
        size_   = 0;
        offset_ = 0;
    }
}

void* Arena::alloc(usize size, usize align) {
    const usize aligned_offset = align_up(offset_, align);
    GE_ASSERT(aligned_offset + size <= size_); // arena exhausted
    void* ptr = base_ + aligned_offset;
    offset_   = aligned_offset + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;
}

} // namespace ge
