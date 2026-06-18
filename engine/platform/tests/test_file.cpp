#include <doctest/doctest.h>
#include "platform/file.h"
#include <cstring>
#include <cstdio>

TEST_CASE("File: write and read round-trip") {
    const char* path = "test_file_roundtrip.tmp";
    const char data[] = "Hello, engine file I/O!";
    std::size_t data_size = sizeof(data) - 1;

    REQUIRE(ge::platform::file_write_all(path, data, data_size));
    REQUIRE(ge::platform::file_exists(path));
    REQUIRE(ge::platform::file_size(path) == data_size);

    auto fd = ge::platform::file_read_all(path);
    REQUIRE(fd.data != nullptr);
    REQUIRE(fd.size == data_size);
    REQUIRE(std::memcmp(fd.data, data, data_size) == 0);

    ge::platform::file_data_free(fd);
    std::remove(path);
}

TEST_CASE("File: file_exists returns false for missing file") {
    REQUIRE(!ge::platform::file_exists("nonexistent_file_12345.tmp"));
}

TEST_CASE("File: file_read_all returns empty for missing file") {
    auto fd = ge::platform::file_read_all("nonexistent_file_12345.tmp");
    REQUIRE(fd.data == nullptr);
    REQUIRE(fd.size == 0);
}

TEST_CASE("File: file_size returns 0 for missing file") {
    REQUIRE(ge::platform::file_size("nonexistent_file_12345.tmp") == 0);
}
