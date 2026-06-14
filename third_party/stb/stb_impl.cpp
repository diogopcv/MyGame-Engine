// Single translation unit providing the stb_image / stb_image_write
// implementations. Vendored for the gfx golden-image test (decode the
// baseline PNG, encode the captured frame) per CLAUDE.md's third-party policy
// (image decode/encode is a solved, non-core problem).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
