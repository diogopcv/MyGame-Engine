# Layer 3: input-mapping subsystem. Depends on core (StringId, types) and
# platform (KeyCode) — resolve() is templated over the key source so the
# mapping logic itself is testable without a real OS window.
# Header-only: no free-standing globals yet.

add_library(ge_input INTERFACE)

target_include_directories(ge_input INTERFACE include)
target_link_libraries(ge_input INTERFACE ge_core ge_platform)

add_subdirectory(tests)
