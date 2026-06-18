#include "core/reflect.h"
#include <mutex>
#include <unordered_map>

namespace ge {

namespace {
    std::mutex& registry_mutex() {
        static std::mutex m;
        return m;
    }
    std::unordered_map<u64, const TypeInfo*>& registry() {
        static std::unordered_map<u64, const TypeInfo*> r;
        return r;
    }
}

void type_register(const TypeInfo* info) {
    std::lock_guard lock(registry_mutex());
    registry()[info->name.hash] = info;
}

const TypeInfo* type_find(StringId name) {
    std::lock_guard lock(registry_mutex());
    auto it = registry().find(name.hash);
    return it != registry().end() ? it->second : nullptr;
}

} // namespace ge
