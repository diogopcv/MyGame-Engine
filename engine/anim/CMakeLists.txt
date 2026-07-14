# Layer 3: animation subsystem (skeletal animation core). Depends only on
# ge_core (math, allocators) — deliberately NOT sideways on ge_gfx or ge_world
# (ADR-0001): bone palettes cross to the renderer as bare mat4* via the
# application layer. See docs/phases/06-animation.md.

add_library(ge_anim STATIC
    src/pose.cpp
    src/clip.cpp
    src/animation_loader.cpp
    src/state_machine.cpp
)

target_include_directories(ge_anim PUBLIC include)
# cgltf is PRIVATE: third-party stays wrapped behind animation_loader.h and
# never leaks into ge_anim's public API (docs/third-party-libraries.md).
target_link_libraries(ge_anim PUBLIC ge_core PRIVATE cgltf)

add_subdirectory(tests)
