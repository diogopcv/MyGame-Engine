# Layer 3: graphics subsystem (DX12 RHI). Depends only on ge_core (and
# transitively ge_platform). All <d3d12.h>/<dxgi1_6.h>/<windows.h> usage stays
# inside src/rhi_dx12.cpp; the renderer, render graph, and model loader are
# backend-agnostic and the public headers are platform-clean.

add_library(ge_gfx STATIC
    src/rhi_dx12.cpp
    src/render_graph.cpp
    src/model_loader.cpp
    src/renderer.cpp
)

target_include_directories(ge_gfx PUBLIC include)

target_link_libraries(ge_gfx PUBLIC ge_core)
target_link_libraries(ge_gfx PRIVATE cgltf stb)

if (WIN32)
    target_link_libraries(ge_gfx PRIVATE d3d12 dxgi dxguid)
endif()

# --- Offline HLSL -> DXIL compilation (DXC) -----------------------------------
# Source committed under shaders/; compiled .cso output goes next to the
# executables (build/bin/<config>/shaders/) and is git-ignored. The renderer
# locates it relative to the running executable's path.
find_program(DXC_EXECUTABLE NAMES dxc
    HINTS "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x64")
if (NOT DXC_EXECUTABLE)
    file(GLOB _ge_gfx_dxc_candidates
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe")
    if (_ge_gfx_dxc_candidates)
        list(SORT _ge_gfx_dxc_candidates)
        list(REVERSE _ge_gfx_dxc_candidates)
        list(GET _ge_gfx_dxc_candidates 0 DXC_EXECUTABLE)
    endif()
endif()
if (NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "ge_gfx: dxc.exe not found (Windows SDK required to build shaders)")
endif()

set(GFX_SHADER_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders")

function(ge_gfx_compile_shader name profile)
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.hlsl")
    set(out "${GFX_SHADER_OUT_DIR}/${name}.cso")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GFX_SHADER_OUT_DIR}"
        COMMAND "${DXC_EXECUTABLE}" -T ${profile} -E main -Fo "${out}" "${src}"
        DEPENDS "${src}"
        COMMENT "Compiling ${name}.hlsl (${profile})"
    )
    list(APPEND GE_GFX_SHADER_OUTPUTS "${out}")
    set(GE_GFX_SHADER_OUTPUTS "${GE_GFX_SHADER_OUTPUTS}" PARENT_SCOPE)
endfunction()

ge_gfx_compile_shader(cube_vs vs_6_0)
ge_gfx_compile_shader(cube_ps ps_6_0)
ge_gfx_compile_shader(shadow_vs vs_6_0)
ge_gfx_compile_shader(shadow_skinned_vs vs_6_0)
ge_gfx_compile_shader(pbr_vs vs_6_0)
ge_gfx_compile_shader(pbr_ps ps_6_0)
ge_gfx_compile_shader(pbr_skinned_vs vs_6_0)

add_custom_target(ge_gfx_shaders DEPENDS ${GE_GFX_SHADER_OUTPUTS})
add_dependencies(ge_gfx ge_gfx_shaders)

add_subdirectory(tests)
