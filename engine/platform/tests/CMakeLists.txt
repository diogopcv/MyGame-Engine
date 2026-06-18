add_executable(ge_platform_tests
    test_runner.cpp
    test_file.cpp
)

target_link_libraries(ge_platform_tests PRIVATE ge_platform doctest::doctest)

add_test(NAME ge_platform_tests COMMAND ge_platform_tests)
