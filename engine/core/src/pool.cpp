#include "core/pool.h"
#include "core/assert.h"
#include "platform/memory.h"

namespace ge {

void Pool::init(usize block_size, u32 block_count) {
    GE_ASSERT(block_count > 0);
    block_size_ = align_up(block_size < sizeof(void*) ? sizeof(void*) : block_size,
                           alignof(std::max_align_t));
    block_count_ = block_count;
    used_count_  = 0;
    total_size_  = block_size_ * block_count_;

    base_ = static_cast<u8*>(platform::mem_alloc(total_size_));
    GE_ASSERT(base_ != nullptr);

    free_head_ = base_;
    for (u32 i = 0; i < block_count_ - 1; ++i) {
        void* current = base_ + i * block_size_;
        void* next    = base_ + (i + 1) * block_size_;
        *reinterpret_cast<void**>(current) = next;
    }
    *reinterpret_cast<void**>(base_ + (block_count_ - 1) * block_size_) = nullptr;
}

void Pool::shutdown() {
    GE_ASSERT(used_count_ == 0);
    if (base_) {
        platform::mem_free(base_, total_size_);
        base_       = nullptr;
        free_head_  = nullptr;
        block_size_ = 0;
        block_count_= 0;
        total_size_ = 0;
    }
}

void* Pool::alloc() {
    GE_ASSERT(free_head_ != nullptr);
    void* block = free_head_;
    free_head_ = *reinterpret_cast<void**>(block);
    ++used_count_;
    return block;
}

void Pool::free(void* ptr) {
    GE_ASSERT(ptr != nullptr);
    GE_ASSERT(ptr >= base_ && ptr < base_ + total_size_);
    *reinterpret_cast<void**>(ptr) = free_head_;
    free_head_ = ptr;
    --used_count_;
}

namespace {
    void* pool_alloc_fn(void* ctx, usize, usize) {
        return static_cast<Pool*>(ctx)->alloc();
    }
    void pool_free_fn(void* ctx, void* ptr, usize) {
        static_cast<Pool*>(ctx)->free(ptr);
    }
}

Allocator Pool::allocator() {
    return { pool_alloc_fn, pool_free_fn, this };
}

} // namespace ge
