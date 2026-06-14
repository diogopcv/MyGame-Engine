# Vendored stb_image / stb_image_write (public domain, single-header).
# Built as a tiny static library so the implementation is compiled exactly
# once; consumers just link against `stb` and #include "stb_image.h" /
# "stb_image_write.h".

add_library(stb STATIC stb_impl.cpp)
target_include_directories(stb PUBLIC .)

if (MSVC)
    # Third-party code: don't hold it to the engine's /W4 warning level.
    target_compile_options(stb PRIVATE /W0)
endif()
