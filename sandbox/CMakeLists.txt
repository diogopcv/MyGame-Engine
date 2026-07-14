# The application / test bed. Top of the dependency stack.
# Links ge_core, ge_gfx, ge_input, ge_world, ge_physics, and ge_anim;
# transitively gets ge_platform. The app layer is the one place that owns
# both ge_anim poses and ge_gfx submission (the Phase 6 sibling-isolation
# contract: palettes cross as bare mat4*).
#
# We leave it as a Console subsystem app (the default for add_executable),
# so logging prints to a console during development. When you want a clean
# windowed launch later, switch to /SUBSYSTEM:WINDOWS + a WinMain entry point.

add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE ge_core ge_gfx ge_input ge_world ge_physics ge_anim)
