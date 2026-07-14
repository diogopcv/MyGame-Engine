add_executable(ge_anim_tests
    test_runner.cpp
    test_clip.cpp
    test_pose.cpp
    test_animation_loader.cpp
    test_state_machine.cpp
    test_root_motion.cpp
)

target_link_libraries(ge_anim_tests PRIVATE ge_anim doctest::doctest)

add_test(NAME ge_anim_tests COMMAND ge_anim_tests)
