# Application-layer integration tests. This is the one place a test links
# sibling subsystems together (ge_anim driving ge_physics) — exactly the glue
# the sandbox itself owns per the Phase 6 sibling-isolation contract, and
# what these baselines verify (docs/phases/06-animation.md, Milestone 5).
add_executable(sandbox_tests
    test_runner.cpp
    test_root_motion_baseline.cpp
)

target_link_libraries(sandbox_tests PRIVATE ge_anim ge_physics doctest::doctest)

add_test(NAME sandbox_tests COMMAND sandbox_tests)
