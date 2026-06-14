# The application / test bed. Top of the dependency stack.
# Links ge_core and ge_gfx, and transitively gets ge_platform.
#
# We leave it as a Console subsystem app (the default for add_executable),
# so logging prints to a console during development. When you want a clean
# windowed launch later, switch to /SUBSYSTEM:WINDOWS + a WinMain entry point.

add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE ge_core ge_gfx)
