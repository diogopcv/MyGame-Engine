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

# --- Third-party: doctest (header-only test framework) -----------------------
# Allow third-party CMake projects that still declare cmake_minimum_required < 3.5
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)

# --- Testing -----------------------------------------------------------------
enable_testing()

# --- Third-party: stb_image / stb_image_write (golden-image test) -----------
add_subdirectory(third_party/stb)

# --- Third-party: cgltf (glTF 2.0 parser) ----------------------------------
add_subdirectory(third_party/cgltf)

# --- Modules (built bottom-up; dependencies point downward only) -------------
add_subdirectory(engine/platform)   # Layer 0 : OS abstraction
add_subdirectory(engine/core)       # Layer 1 : foundation (depends on platform)
add_subdirectory(engine/gfx)        # Layer 3 : graphics subsystem (DX12 RHI)
add_subdirectory(sandbox)           # Layer 5 : the application (depends on core, gfx)
