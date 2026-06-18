#pragma once
#include "core/types.h"

namespace ge {

struct StringId {
    u64 hash = 0;

    constexpr bool operator==(StringId o) const { return hash == o.hash; }
    constexpr bool operator!=(StringId o) const { return hash != o.hash; }
    constexpr bool operator<(StringId o)  const { return hash < o.hash; }
};

constexpr StringId string_id(const char* str) {
    u64 h = 14695981039346656037ull;
    for (const char* p = str; *p != '\0'; ++p) {
        h ^= static_cast<u64>(static_cast<u8>(*p));
        h *= 1099511628211ull;
    }
    return { h };
}

#ifndef NDEBUG
void string_id_register(StringId id, const char* str);
const char* string_id_str(StringId id);
#endif

} // namespace ge

#ifndef NDEBUG
#define GE_SID(lit) ([]() -> ::ge::StringId {                              \
    static const ::ge::StringId id_ = ::ge::string_id(lit);               \
    static const bool reg_ = (::ge::string_id_register(id_, lit), true);  \
    (void)reg_;                                                            \
    return id_;                                                            \
}())
#else
#define GE_SID(lit) (::ge::string_id(lit))
#endif
