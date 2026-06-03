#include "platform/memory.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ge::platform {

void* mem_alloc(std::size_t size) {
    // MEM_COMMIT|MEM_RESERVE gives us zeroed, ready-to-use pages.
    // A later refinement: MEM_RESERVE a large range and commit on demand,
    // which lets arenas grow without ever copying or moving memory.
    return ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void mem_free(void* ptr, std::size_t /*size*/) {
    if (ptr) {
        ::VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

std::size_t page_size() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return static_cast<std::size_t>(info.dwPageSize);
}

} // namespace ge::platform
