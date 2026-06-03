#pragma once

// High-resolution timing. Backed by QueryPerformanceCounter on Windows.
namespace ge::platform {

// Call once at startup. Caches the performance-counter frequency and the
// reference point that now_seconds() measures from.
void time_init();

// Seconds elapsed since time_init(), as a double for sub-microsecond precision.
double now_seconds();

} // namespace ge::platform
