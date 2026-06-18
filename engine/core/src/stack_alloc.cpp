#include "core/stack_alloc.h"
#include "core/assert.h"
#include "platform/memory.h"

namespace ge {

namespace {
    struct StackHeader {
        usize prev_offset;
    };
}

void StackAlloc::init(usize size) {
    base_ = static_cast<u8*>(platform::mem_alloc(size));
    GE_ASSERT(base_ != nullptr);
    size_   = size;
    offset_ = 0;
}

void StackAlloc::shutdown() {
    if (base_) {
        platform::mem_free(base_, size_);
        base_   = nullptr;
        size_   = 0;
        offset_ = 0;
    }
}

void* StackAlloc::alloc(usize size, usize align) {
    GE_ASSERT(align > 0 && (align & (align - 1)) == 0);
    const usize min_align = alignof(StackHeader) > align ? alignof(StackHeader) : align;

    const usize raw = offset_ + sizeof(StackHeader);
    const usize aligned = align_up(raw, min_align);
    const usize end = aligned + size;
    GE_ASSERT(end <= size_);

    auto* header = reinterpret_cast<StackHeader*>(base_ + aligned - sizeof(StackHeader));
    header->prev_offset = offset_;

    offset_ = end;
    return base_ + aligned;
}

void StackAlloc::free(void* ptr) {
    GE_ASSERT(ptr != nullptr);
    auto* header = reinterpret_cast<StackHeader*>(static_cast<u8*>(ptr) - sizeof(StackHeader));
    GE_ASSERT(header->prev_offset < offset_);
    offset_ = header->prev_offset;
}

void StackAlloc::reset() {
    offset_ = 0;
}

namespace {
    void* stack_alloc_fn(void* ctx, usize size, usize align) {
        return static_cast<StackAlloc*>(ctx)->alloc(size, align);
    }
    void stack_free_fn(void* ctx, void* ptr, usize) {
        static_cast<StackAlloc*>(ctx)->free(ptr);
    }
}

Allocator StackAlloc::allocator() {
    return { stack_alloc_fn, stack_free_fn, this };
}

} // namespace ge
