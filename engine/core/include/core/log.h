#pragma once
#include <format>
#include <string_view>

/**
 * @file log.h
 * @brief Minimal leveled logger (Layer 1 — Core).
 *
 * Output goes to the console and, on Windows, to the Visual Studio "Output"
 * window via `OutputDebugString`.
 *
 * @par Usage
 * @code
 *   GE_INFO("Loaded {} meshes in {:.2f} ms", count, ms);
 * @endcode
 *
 * The format string is checked at compile time (`std::format_string`), so
 * mistakes like a missing argument are caught by the compiler.
 */
namespace ge::log {

/** @brief Severity levels for log messages. */
enum class Level {
    Trace, ///< Verbose diagnostic output.
    Info,  ///< General informational messages.
    Warn,  ///< Potential issues that do not prevent execution.
    Error  ///< Errors that may affect correctness.
};

/**
 * @brief Write a log message at the given severity level.
 * @param level Severity level.
 * @param file  Source file name (typically `__FILE__`).
 * @param line  Source line number (typically `__LINE__`).
 * @param msg   Pre-formatted message string.
 */
void write(Level level, const char* file, int line, std::string_view msg);

} // namespace ge::log

/** @brief Log a message at Trace level. Format string is compile-time checked. */
#define GE_TRACE(...) ::ge::log::write(::ge::log::Level::Trace, __FILE__, __LINE__, std::format(__VA_ARGS__))
/** @brief Log a message at Info level.  Format string is compile-time checked. */
#define GE_INFO(...)  ::ge::log::write(::ge::log::Level::Info,  __FILE__, __LINE__, std::format(__VA_ARGS__))
/** @brief Log a message at Warn level.  Format string is compile-time checked. */
#define GE_WARN(...)  ::ge::log::write(::ge::log::Level::Warn,  __FILE__, __LINE__, std::format(__VA_ARGS__))
/** @brief Log a message at Error level. Format string is compile-time checked. */
#define GE_ERROR(...) ::ge::log::write(::ge::log::Level::Error, __FILE__, __LINE__, std::format(__VA_ARGS__))
