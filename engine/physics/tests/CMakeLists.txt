add_executable(ge_physics_tests
    test_runner.cpp
    test_collision.cpp
    test_gjk.cpp
    test_broadphase.cpp
    test_manifold.cpp
    test_solver.cpp
    test_physics_world.cpp
)

target_link_libraries(ge_physics_tests PRIVATE ge_physics doctest::doctest)

add_test(NAME ge_physics_tests COMMAND ge_physics_tests)
