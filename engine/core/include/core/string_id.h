#pragma once
#include "core/types.h"

/**
 * @file string_id.h
 * @brief Compile-time hashed string identifiers (Layer 1 — Core).
 *
 * StringId stores a 64-bit FNV-1a hash of a string literal.  In debug builds
 * the original string is also registered so it can be recovered for logging.
 */
namespace ge {

/**
 * @brief Hashed string identifier (64-bit FNV-1a).
 *
 * Compact, comparison-friendly identifier intended to replace raw C-string
 * keys in performance-sensitive lookup tables.
 */
struct StringId {
    u64 hash = 0; ///< FNV-1a hash of the source string.

    constexpr bool operator==(StringId o) const { return hash == o.hash; }
    constexpr bool operator!=(StringId o) const { return hash != o.hash; }
    constexpr bool operator<(StringId o)  const { return hash < o.hash; }
};

/**
 * @brief Compute a StringId from a null-terminated string at compile time.
 * @param str Null-terminated source string.
 * @return StringId containing the FNV-1a hash.
 */
constexpr StringId string_id(const char* str) {
    u64 h = 14695981039346656037ull;
    for (const char* p = str; *p != '\0'; ++p) {
        h ^= static_cast<u64>(static_cast<u8>(*p));
        h *= 1099511628211ull;
    }
    return { h };
}

#ifndef NDEBUG
/**
 * @brief Register a StringId ↔ string association (debug builds only).
 * @param id  Hashed identifier.
 * @param str Original string literal.
 */
void string_id_register(StringId id, const char* str);

/**
 * @brief Retrieve the original string for a registered StringId (debug builds only).
 * @param id Hashed identifier.
 * @return Original string, or `nullptr` if not registered.
 */
const char* string_id_str(StringId id);
#endif

} // namespace ge

/**
 * @brief Create a StringId from a string literal with debug-build registration.
 * @param lit String literal to hash.
 */
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
