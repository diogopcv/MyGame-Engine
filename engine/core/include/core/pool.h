#pragma once
#include "core/types.h"
#include "core/allocator.h"

namespace ge {

class Pool {
public:
    void init(usize block_size, u32 block_count);
    void shutdown();

    void* alloc();
    void  free(void* ptr);

    Allocator allocator();

    u32   total_blocks() const { return block_count_; }
    u32   used_blocks()  const { return used_count_; }
    usize block_size()   const { return block_size_; }

private:
    u8*   base_        = nullptr;
    usize block_size_  = 0;
    u32   block_count_ = 0;
    u32   used_count_  = 0;
    void* free_head_   = nullptr;
    usize total_size_  = 0;
};

} // namespace ge
