# Layer 3: physics subsystem (hand-written rigid-body pipeline). Depends only
# on ge_core (math, containers, allocators) — deliberately NOT on ge_world or
# ge_gfx, so the simulation core stays reusable/testable in isolation and the
# ECS-facing glue (Milestone 5) stays a thin one-directional adapter.
# See docs/phases/05-physics.md.

add_library(ge_physics STATIC
    src/collision.cpp
    src/gjk.cpp
    src/broadphase.cpp
    src/manifold.cpp
    src/solver.cpp
    src/physics_world.cpp
)

target_include_directories(ge_physics PUBLIC include)
target_link_libraries(ge_physics PUBLIC ge_core)

add_subdirectory(tests)
