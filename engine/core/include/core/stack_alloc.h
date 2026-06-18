#pragma once
#include "core/types.h"
#include "core/allocator.h"

/**
 * @file stack_alloc.h
 * @brief Stack (LIFO) allocator (Layer 1 — Core).
 *
 * Allocations grow upward from a base pointer.  Individual frees must happen
 * in reverse allocation order (LIFO).  Alternatively, reset() frees
 * everything at once.
 */
namespace ge {

/**
 * @brief Stack-based (LIFO) allocator.
 *
 * Supports ordered frees in reverse allocation order and bulk reset().
 * Useful for temporary, scope-scoped allocations.
 */
class StackAlloc {
public:
    /**
     * @brief Reserve a contiguous virtual-memory block.
     * @param size Number of bytes to reserve.
     */
    void init(usize size);

    /** @brief Release the backing virtual-memory block. */
    void shutdown();

    /**
     * @brief Allocate @p size bytes from the top of the stack.
     * @param size  Number of bytes.
     * @param align Alignment requirement (default 16).
     * @return Pointer to the allocated memory, or `nullptr` if out of space.
     */
    void* alloc(usize size, usize align = 16);

    /**
     * @brief Free the most-recently allocated block (LIFO order).
     * @param ptr Pointer returned by the most recent alloc().
     */
    void  free(void* ptr);

    /** @brief Reset the offset to zero, logically freeing all allocations. */
    void  reset();

    /** @brief Obtain a type-erased Allocator that dispatches to this StackAlloc. */
    Allocator allocator();

    /** @brief Number of bytes currently in use. */
    usize used()     const { return offset_; }
    /** @brief Total reserved capacity in bytes. */
    usize capacity() const { return size_; }

private:
    u8*   base_   = nullptr;
    usize size_   = 0;
    usize offset_ = 0;
};

} // namespace ge
