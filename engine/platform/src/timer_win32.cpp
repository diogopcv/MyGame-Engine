#include "platform/timer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ge::platform {

namespace {
    long long g_freq  = 1; // counts per second
    long long g_start = 0; // reference counter captured at init
}

void time_init() {
    LARGE_INTEGER f, c;
    ::QueryPerformanceFrequency(&f);
    ::QueryPerformanceCounter(&c);
    g_freq  = f.QuadPart;
    g_start = c.QuadPart;
}

double now_seconds() {
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart - g_start) / static_cast<double>(g_freq);
}

} // namespace ge::platform
