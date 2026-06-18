add_executable(ge_core_tests
    test_runner.cpp
    test_math.cpp
    test_arena.cpp
    test_allocators.cpp
    test_containers.cpp
    test_string_id.cpp
    test_reflect.cpp
    test_serialize.cpp
    test_job_system.cpp
    test_handle.cpp
    test_profiler.cpp
    test_cvar.cpp
)

target_link_libraries(ge_core_tests PRIVATE ge_core doctest::doctest)

add_test(NAME ge_core_tests COMMAND ge_core_tests)
