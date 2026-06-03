# Layer 1: the core/foundation layer.
# Builds on top of the platform layer (logging uses OS debug output, the
# arena reserves memory through platform::mem_alloc). The link below is the
# ONLY allowed direction: core -> platform. Platform must never link core.

add_library(ge_core STATIC
    src/log.cpp
    src/arena.cpp
)

target_include_directories(ge_core PUBLIC include)

# PUBLIC so anyone linking ge_core also transitively gets ge_platform.
target_link_libraries(ge_core PUBLIC ge_platform)
