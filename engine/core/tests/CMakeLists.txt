add_executable(ge_core_tests
    test_runner.cpp
    test_math.cpp
    test_arena.cpp
)

target_link_libraries(ge_core_tests PRIVATE ge_core doctest::doctest)

add_test(NAME ge_core_tests COMMAND ge_core_tests)
