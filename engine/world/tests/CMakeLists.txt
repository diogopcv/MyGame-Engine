add_executable(ge_world_tests
    test_runner.cpp
    test_entity.cpp
    test_component_pool.cpp
    test_query.cpp
    test_scene.cpp
)

target_link_libraries(ge_world_tests PRIVATE ge_world doctest::doctest)

add_test(NAME ge_world_tests COMMAND ge_world_tests)
