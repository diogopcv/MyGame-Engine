cmake_minimum_required(VERSION 3.20)
project(MyEngine LANGUAGES CXX)

# --- Global build settings ---------------------------------------------------
set(CMAKE_CXX_STANDARD 20)            # we rely on <format>, designated inits, etc.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All executables land in one bin/ folder so the working dir is predictable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (MSVC)
    # /W4        : high warning level
    # /permissive-: strict, standards-conforming mode
    # /MP        : parallel compilation
    add_compile_options(/W4 /permissive- /MP)
endif()

# --- Modules (built bottom-up; dependencies point downward only) -------------
add_subdirectory(engine/platform)   # Layer 0 : OS abstraction
add_subdirectory(engine/core)       # Layer 1 : foundation (depends on platform)
add_subdirectory(sandbox)           # Layer 5 : the application (depends on core)
