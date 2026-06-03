#pragma once
#include <format>
#include <string_view>

// Minimal leveled logger. Output goes to the console and, on Windows, to the
// Visual Studio "Output" window via OutputDebugString.
//
// Usage:
//   GE_INFO("Loaded {} meshes in {:.2f} ms", count, ms);
//
// Note: this Phase 0 logger is intentionally single-threaded. A proper
// lock-free / per-thread-buffered version comes once the job system exists.
namespace ge::log {

enum class Level { Trace, Info, Warn, Error };

void write(Level level, const char* file, int line, std::string_view msg);

} // namespace ge::log

// The format string is checked at compile time (std::format_string), so
// mistakes like a missing argument are caught by the compiler.
#define GE_TRACE(...) ::ge::log::write(::ge::log::Level::Trace, __FILE__, __LINE__, std::format(__VA_ARGS__))
#define GE_INFO(...)  ::ge::log::write(::ge::log::Level::Info,  __FILE__, __LINE__, std::format(__VA_ARGS__))
#define GE_WARN(...)  ::ge::log::write(::ge::log::Level::Warn,  __FILE__, __LINE__, std::format(__VA_ARGS__))
#define GE_ERROR(...) ::ge::log::write(::ge::log::Level::Error, __FILE__, __LINE__, std::format(__VA_ARGS__))
