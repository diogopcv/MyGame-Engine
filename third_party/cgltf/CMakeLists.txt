add_library(cgltf STATIC cgltf_impl.cpp)
target_include_directories(cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    target_compile_options(cgltf PRIVATE /W0)
endif()
