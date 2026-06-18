#include "platform/file.h"
#include "platform/memory.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ge::platform {

FileData file_read_all(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li)) { CloseHandle(h); return {}; }

    auto sz = static_cast<std::size_t>(li.QuadPart);
    if (sz == 0) { CloseHandle(h); return {}; }

    void* buf = mem_alloc(sz);
    if (!buf) { CloseHandle(h); return {}; }

    DWORD read = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(sz), &read, nullptr) || read != sz) {
        mem_free(buf, sz);
        CloseHandle(h);
        return {};
    }

    CloseHandle(h);
    return { buf, sz };
}

void file_data_free(FileData& fd) {
    if (fd.data) {
        mem_free(fd.data, fd.size);
        fd.data = nullptr;
        fd.size = 0;
    }
}

bool file_write_all(const char* path, const void* data, std::size_t size) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr)
              && written == static_cast<DWORD>(size);
    CloseHandle(h);
    return ok;
}

bool file_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::size_t file_size(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info))
        return 0;
    LARGE_INTEGER li;
    li.HighPart = static_cast<LONG>(info.nFileSizeHigh);
    li.LowPart  = info.nFileSizeLow;
    return static_cast<std::size_t>(li.QuadPart);
}

} // namespace ge::platform
