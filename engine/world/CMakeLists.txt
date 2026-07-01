# Layer 4: the frameworks layer (ECS/world). Depends only on core (and
# transitively platform, via ge_core's PUBLIC link) — deliberately NOT on
# ge_gfx, so the ECS stays reusable by any future rendering/physics backend.
# See docs/phases/04-world-gameplay.md.
#
# entity/component-pool/world/components are header-only templates; scene.cpp
# is the one non-template translation unit (save/load).

add_library(ge_world STATIC
    src/scene.cpp
)

target_include_directories(ge_world PUBLIC include)
target_link_libraries(ge_world PUBLIC ge_core)

add_subdirectory(tests)
