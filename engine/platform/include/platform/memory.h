#pragma once
#include <cstddef>

// Raw virtual-memory services. Note we use only standard types here
// (std::size_t) so the platform layer has zero dependency on core.
namespace ge::platform {

// Reserve + commit a block of zero-initialized memory.
void* mem_alloc(std::size_t size);

// Release a block previously returned by mem_alloc. `size` is currently
// unused on Windows but kept in the signature for portability.
void  mem_free(void* ptr, std::size_t size);

// OS page size in bytes (useful for arenas that commit on demand later).
std::size_t page_size();

} // namespace ge::platform
