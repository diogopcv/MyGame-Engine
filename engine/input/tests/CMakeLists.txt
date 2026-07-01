add_executable(ge_input_tests
    test_runner.cpp
    test_input_map.cpp
)

target_link_libraries(ge_input_tests PRIVATE ge_input doctest::doctest)

add_test(NAME ge_input_tests COMMAND ge_input_tests)
