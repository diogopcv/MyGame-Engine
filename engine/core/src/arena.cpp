#include "core/arena.h"
#include "core/assert.h"
#include "platform/memory.h"

namespace ge {

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
    GE_ASSERT(aligned_offset + size <= size_);
    void* ptr = base_ + aligned_offset;
    offset_   = aligned_offset + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;
}

namespace {
    void* arena_alloc_fn(void* ctx, usize size, usize align) {
        return static_cast<Arena*>(ctx)->alloc(size, align);
    }
    void arena_free_fn(void*, void*, usize) {}
}

Allocator Arena::allocator() {
    return { arena_alloc_fn, arena_free_fn, this };
}

} // namespace ge
