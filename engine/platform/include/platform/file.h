#pragma once
#include <cstddef>

namespace ge::platform {

struct FileData {
    void*       data = nullptr;
    std::size_t size = 0;
};

FileData file_read_all(const char* path);

void file_data_free(FileData& fd);

bool file_write_all(const char* path, const void* data, std::size_t size);

bool file_exists(const char* path);

std::size_t file_size(const char* path);

} // namespace ge::platform
