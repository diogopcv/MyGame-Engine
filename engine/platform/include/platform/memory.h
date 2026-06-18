#pragma once
#include <cstddef>

/**
 * @file memory.h
 * @brief Raw virtual-memory services (Layer 0 — Platform).
 *
 * Uses only standard types (`std::size_t`) so the platform layer has zero
 * dependency on core.  Backed by `VirtualAlloc` / `VirtualFree` on Windows.
 */
namespace ge::platform {

/**
 * @brief Reserve and commit a block of zero-initialized virtual memory.
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated block, or `nullptr` on failure.
 */
void* mem_alloc(std::size_t size);

/**
 * @brief Release a block previously returned by mem_alloc().
 * @param ptr  Pointer returned by mem_alloc().
 * @param size Size passed to the original mem_alloc() call.
 *             Currently unused on Windows but kept for portability.
 */
void  mem_free(void* ptr, std::size_t size);

/**
 * @brief Query the OS page size in bytes.
 * @return Page size (e.g. 4096 on most x86-64 systems).
 */
std::size_t page_size();

} // namespace ge::platform
