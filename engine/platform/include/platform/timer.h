#pragma once

/**
 * @file timer.h
 * @brief High-resolution timing (Layer 0 — Platform).
 *
 * Backed by `QueryPerformanceCounter` on Windows.
 */
namespace ge::platform {

/**
 * @brief Initialise the high-resolution timer.
 *
 * Caches the performance-counter frequency and the reference point that
 * now_seconds() measures from.  Must be called once at startup before any
 * call to now_seconds().
 */
void time_init();

/**
 * @brief Seconds elapsed since time_init().
 * @return Elapsed time as a double for sub-microsecond precision.
 */
double now_seconds();

} // namespace ge::platform
