#include "core/log.h"

#include <cstdio>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace ge::log {

namespace {
    const char* level_str(Level l) {
        switch (l) {
            case Level::Trace: return "TRACE";
            case Level::Info:  return "INFO ";
            case Level::Warn:  return "WARN ";
            case Level::Error: return "ERROR";
        }
        return "?????";
    }
}

void write(Level level, const char* file, int line, std::string_view msg) {
    // Trim the path down to just the file name.
    const char* name = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }

    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "[%s] %s(%d): %.*s\n",
                  level_str(level), name, line,
                  static_cast<int>(msg.size()), msg.data());

    std::fputs(buffer, level == Level::Error ? stderr : stdout);

#if defined(_WIN32)
    ::OutputDebugStringA(buffer); // shows up in the VS Output window
#endif
}

} // namespace ge::log
