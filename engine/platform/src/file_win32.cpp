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

std::size_t executable_dir(char* out, std::size_t capacity) {
    if (!out || capacity == 0) return 0;

    wchar_t wide[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, wide, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return 0;

    // Strip the executable filename: cut at the last path separator.
    int dir_len = static_cast<int>(len);
    while (dir_len > 0 && wide[dir_len - 1] != L'\\' && wide[dir_len - 1] != L'/')
        --dir_len;
    // Drop the trailing separator, unless it terminates a drive root ("C:\").
    if (dir_len > 1 && !(dir_len == 3 && wide[1] == L':')) --dir_len;
    if (dir_len <= 0) return 0;

    const int written = WideCharToMultiByte(CP_UTF8, 0, wide, dir_len,
                                            out, static_cast<int>(capacity) - 1,
                                            nullptr, nullptr);
    if (written <= 0) return 0;

    out[written] = '\0';
    return static_cast<std::size_t>(written);
}

} // namespace ge::platform
