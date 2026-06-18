#include "core/string_id.h"

#ifndef NDEBUG
#include <mutex>
#include <unordered_map>
#include <string>

namespace ge {

namespace {
    std::mutex& debug_mutex() {
        static std::mutex m;
        return m;
    }
    std::unordered_map<u64, std::string>& debug_table() {
        static std::unordered_map<u64, std::string> t;
        return t;
    }
}

void string_id_register(StringId id, const char* str) {
    std::lock_guard lock(debug_mutex());
    debug_table().emplace(id.hash, str);
}

const char* string_id_str(StringId id) {
    std::lock_guard lock(debug_mutex());
    auto it = debug_table().find(id.hash);
    return it != debug_table().end() ? it->second.c_str() : "<unknown>";
}

} // namespace ge
#endif
