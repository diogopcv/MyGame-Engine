#pragma once
#include "core/types.h"
#include "core/allocator.h"

/**
 * @file pool.h
 * @brief Fixed-size block pool allocator (Layer 1 — Core).
 *
 * Pre-allocates a contiguous array of equal-sized blocks and manages them
 * through an embedded free list.  Allocation and deallocation are both O(1).
 */
namespace ge {

/**
 * @brief Fixed-size block pool allocator.
 *
 * All blocks share the same size.  Free blocks are threaded into an intrusive
 * singly-linked list, so both alloc() and free() are O(1).
 */
class Pool {
public:
    /**
     * @brief Initialise the pool.
     * @param block_size  Size of each block in bytes (rounded up internally
     *                    to at least `sizeof(void*)` for the free-list link).
     * @param block_count Total number of blocks to pre-allocate.
     */
    void init(usize block_size, u32 block_count);

    /** @brief Release the backing memory and reset all state. */
    void shutdown();

    /**
     * @brief Allocate one block from the pool.
     * @return Pointer to the block, or triggers an assert if the pool is full.
     */
    void* alloc();

    /**
     * @brief Return a block to the pool.
     * @param ptr Pointer previously returned by alloc().
     */
    void  free(void* ptr);

    /** @brief Obtain a type-erased Allocator that dispatches to this Pool. */
    Allocator allocator();

    /** @brief Total number of blocks the pool was initialised with. */
    u32   total_blocks() const { return block_count_; }
    /** @brief Number of blocks currently in use. */
    u32   used_blocks()  const { return used_count_; }
    /** @brief Size of each block in bytes. */
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
