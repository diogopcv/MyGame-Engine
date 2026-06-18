# Layer 0: the platform abstraction layer.
# It depends on NOTHING above it (no core, no engine) — only on the OS.
# Everything Win32-specific lives in the *_win32.cpp files; the public
# headers stay free of <windows.h> so the rest of the engine is OS-clean.

add_library(ge_platform STATIC
    src/window_win32.cpp
    src/timer_win32.cpp
    src/memory_win32.cpp
    src/file_win32.cpp
)

target_include_directories(ge_platform PUBLIC include)

if (WIN32)
    target_link_libraries(ge_platform PUBLIC user32 gdi32)
endif()

add_subdirectory(tests)
