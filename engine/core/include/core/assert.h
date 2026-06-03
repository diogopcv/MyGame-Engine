#pragma once
#include "core/log.h"

#if defined(_MSC_VER)
    #define GE_DEBUGBREAK() __debugbreak()
#else
    #define GE_DEBUGBREAK() __builtin_trap()
#endif

// Logs the failed expression and breaks into the debugger. In a later pass
// you'll typically compile this out of shipping builds via a GE_RELEASE flag.
#define GE_ASSERT(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::ge::log::write(::ge::log::Level::Error, __FILE__, __LINE__,       \
                             "Assertion failed: " #cond);                      \
            GE_DEBUGBREAK();                                                   \
        }                                                                      \
    } while (0)
