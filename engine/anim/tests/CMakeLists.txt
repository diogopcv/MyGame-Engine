add_executable(ge_anim_tests
    test_runner.cpp
    test_clip.cpp
    test_pose.cpp
)

target_link_libraries(ge_anim_tests PRIVATE ge_anim doctest::doctest)

add_test(NAME ge_anim_tests COMMAND ge_anim_tests)
