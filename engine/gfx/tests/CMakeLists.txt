add_executable(ge_gfx_tests
    test_runner.cpp
    test_smoke.cpp
    test_golden.cpp
)

target_link_libraries(ge_gfx_tests PRIVATE ge_gfx ge_platform doctest::doctest stb)

# Absolute path to the checked-in golden-image baselines (see test_golden.cpp).
target_compile_definitions(ge_gfx_tests PRIVATE
    GE_GFX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_test(NAME ge_gfx_tests COMMAND ge_gfx_tests)
