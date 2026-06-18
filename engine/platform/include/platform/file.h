#pragma once
#include <cstddef>

/**
 * @file file.h
 * @brief Platform file I/O utilities (Layer 0 — Platform).
 *
 * Provides whole-file read/write and basic file queries.  Memory returned by
 * file_read_all() must be freed with file_data_free().
 */
namespace ge::platform {

/**
 * @brief Result of a whole-file read operation.
 */
struct FileData {
    void*       data = nullptr; ///< Pointer to file contents (owned; free with file_data_free()).
    std::size_t size = 0;       ///< Number of bytes read.
};

/**
 * @brief Read an entire file into memory.
 * @param path Null-terminated file path.
 * @return FileData with the contents, or `{nullptr, 0}` on failure.
 */
FileData file_read_all(const char* path);

/**
 * @brief Free memory allocated by file_read_all().
 * @param fd FileData to release.  Fields are zeroed after the call.
 */
void file_data_free(FileData& fd);

/**
 * @brief Write a buffer to a file, creating or overwriting it.
 * @param path Null-terminated file path.
 * @param data Pointer to the bytes to write.
 * @param size Number of bytes to write.
 * @return `true` on success.
 */
bool file_write_all(const char* path, const void* data, std::size_t size);

/**
 * @brief Check whether a file exists on disk.
 * @param path Null-terminated file path.
 * @return `true` if the file exists.
 */
bool file_exists(const char* path);

/**
 * @brief Query the size of a file in bytes.
 * @param path Null-terminated file path.
 * @return File size, or 0 on failure.
 */
std::size_t file_size(const char* path);

} // namespace ge::platform
