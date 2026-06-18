#pragma once
#include "core/log.h"

/**
 * @file assert.h
 * @brief Debug assertion macros (Layer 1 — Core).
 */

/** @brief Trigger a debugger break-point on the current platform. */
#if defined(_MSC_VER)
    #define GE_DEBUGBREAK() __debugbreak()
#else
    #define GE_DEBUGBREAK() __builtin_trap()
#endif

/**
 * @brief Assert that @p cond is true; log and break into the debugger on failure.
 *
 * In a later pass this will be compiled out of shipping builds via a
 * `GE_RELEASE` flag.
 *
 * @param cond Boolean expression to evaluate.
 */
#define GE_ASSERT(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::ge::log::write(::ge::log::Level::Error, __FILE__, __LINE__,       \
                             "Assertion failed: " #cond);                      \
            GE_DEBUGBREAK();                                                   \
        }                                                                      \
    } while (0)
