// Standalone tool: generates assets/models/skinned_strip.glb — the Phase 6
// deterministic unit-test asset (docs/phases/06-animation.md, Milestone 2).
//
// A 2-bone vertical strip:
//   joint 0 "root_joint" at the origin; joint 1 "tip_joint" one unit up (+Y).
//   6 vertices in 3 rows: y=0 fully joint 0, y=1 blended 0.5/0.5, y=2 fully
//   joint 1 (via JOINTS_0/WEIGHTS_0). Inverse bind matrices: identity for
//   joint 0, translation(0,-1,0) for joint 1.
// Two LINEAR animations, 1 second each:
//   "bend": joint 1 rotates 0 -> 90 degrees about Z  (rotation channel)
//   "move": joint 0 translates (0,0,0) -> (2,0,0)    (root-motion test clip)
// The mesh node carries a translation(5,0,0) deliberately: per glTF a skinned
// node's transform must be IGNORED, so a correct importer yields vertices at
// their authored bind positions — tests assert exactly that.
//
// Compile: cl /EHsc /std:c++20 gen_skinned_glb.cpp /Fe:gen_skinned_glb.exe
// Run:     gen_skinned_glb.exe ../assets/models/skinned_strip.glb

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    const char* out_path = (argc > 1) ? argv[1] : "skinned_strip.glb";

    // --- Vertex data: 3 rows (y = 0, 1, 2) x 2 columns (x = -0.5, +0.5) ---
    const float positions[6][3] = {
        {-0.5f, 0.0f, 0.0f}, { 0.5f, 0.0f, 0.0f},
        {-0.5f, 1.0f, 0.0f}, { 0.5f, 1.0f, 0.0f},
        {-0.5f, 2.0f, 0.0f}, { 0.5f, 2.0f, 0.0f},
    };
    const float normals[6][3] = {
        {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
    };
    const uint8_t joints[6][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 0},   // bottom row: joint 0
        {0, 1, 0, 0}, {0, 1, 0, 0},   // middle row: joints 0+1
        {1, 0, 0, 0}, {1, 0, 0, 0},   // top row:    joint 1
    };
    const float weights[6][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
    };
    const uint32_t indices[12] = {
        0, 2, 3,  0, 3, 1,   // bottom quad
        2, 4, 5,  2, 5, 3,   // top quad
    };

    // --- Skin: inverse bind matrices (glTF column-major float[16]) ---
    const float ibm[2][16] = {
        // joint 0: identity
        {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1},
        // joint 1: translation(0,-1,0) — inverse of its global bind (0,1,0)
        {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1},
    };

    // --- Animations ---
    const float bend_times[2] = {0.0f, 1.0f};
    const float bend_rots[2][4] = {
        {0, 0, 0, 1},                              // identity
        {0, 0, 0.70710678f, 0.70710678f},          // 90 degrees about Z
    };
    const float move_times[2] = {0.0f, 1.0f};
    const float move_trans[2][3] = {
        {0, 0, 0},
        {2, 0, 0},
    };

    // --- Pack the binary chunk (everything is 4-byte aligned by size) ---
    struct Span { uint32_t offset, size; };
    std::vector<uint8_t> bin;
    auto append = [&bin](const void* data, uint32_t size) -> Span {
        Span s{(uint32_t)bin.size(), size};
        bin.resize(bin.size() + size);
        memcpy(bin.data() + s.offset, data, size);
        return s;
    };

    Span sp_pos     = append(positions,  sizeof(positions));
    Span sp_nrm     = append(normals,    sizeof(normals));
    Span sp_joints  = append(joints,     sizeof(joints));
    Span sp_weights = append(weights,    sizeof(weights));
    Span sp_idx     = append(indices,    sizeof(indices));
    Span sp_ibm     = append(ibm,        sizeof(ibm));
    Span sp_bt      = append(bend_times, sizeof(bend_times));
    Span sp_br      = append(bend_rots,  sizeof(bend_rots));
    Span sp_mt      = append(move_times, sizeof(move_times));
    Span sp_mv      = append(move_trans, sizeof(move_trans));
    uint32_t bin_size = (uint32_t)bin.size();

    auto view = [](Span s, const char* target) {
        std::string v = "{\"buffer\":0,\"byteOffset\":" + std::to_string(s.offset)
                      + ",\"byteLength\":" + std::to_string(s.size);
        if (target[0]) v += std::string(",\"target\":") + target;
        return v + "}";
    };

    std::string json = std::string(R"({
  "asset":{"version":"2.0","generator":"gen_skinned_glb"},
  "scene":0,
  "scenes":[{"nodes":[0,2]}],
  "nodes":[
    {"name":"root_joint","children":[1]},
    {"name":"tip_joint","translation":[0,1,0]},
    {"name":"strip","mesh":0,"skin":0,"translation":[5,0,0]}
  ],
  "skins":[{"skeleton":0,"joints":[0,1],"inverseBindMatrices":5}],
  "meshes":[{"primitives":[{
    "attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3},
    "indices":4
  }]}],
  "animations":[
    {"name":"bend",
     "samplers":[{"input":6,"output":7,"interpolation":"LINEAR"}],
     "channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}]},
    {"name":"move",
     "samplers":[{"input":8,"output":9,"interpolation":"LINEAR"}],
     "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[-0.5,0,0],"max":[0.5,2,0]},
    {"bufferView":1,"componentType":5126,"count":6,"type":"VEC3"},
    {"bufferView":2,"componentType":5121,"count":6,"type":"VEC4"},
    {"bufferView":3,"componentType":5126,"count":6,"type":"VEC4"},
    {"bufferView":4,"componentType":5125,"count":12,"type":"SCALAR"},
    {"bufferView":5,"componentType":5126,"count":2,"type":"MAT4"},
    {"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
    {"bufferView":7,"componentType":5126,"count":2,"type":"VEC4"},
    {"bufferView":8,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
    {"bufferView":9,"componentType":5126,"count":2,"type":"VEC3"}
  ],
  "bufferViews":[)")
    + "\n    " + view(sp_pos, "34962") + ",\n    " + view(sp_nrm, "34962")
    + ",\n    " + view(sp_joints, "34962") + ",\n    " + view(sp_weights, "34962")
    + ",\n    " + view(sp_idx, "34963") + ",\n    " + view(sp_ibm, "")
    + ",\n    " + view(sp_bt, "") + ",\n    " + view(sp_br, "")
    + ",\n    " + view(sp_mt, "") + ",\n    " + view(sp_mv, "")
    + R"(
  ],
  "buffers":[{"byteLength":)" + std::to_string(bin_size) + R"(}]
})";

    // Pad JSON to a 4-byte boundary with spaces.
    while (json.size() % 4 != 0) json += ' ';

    uint32_t json_len = (uint32_t)json.size();
    uint32_t bin_pad  = (4 - (bin_size % 4)) % 4;
    uint32_t total    = 12 + 8 + json_len + 8 + bin_size + bin_pad;

    std::ofstream f(out_path, std::ios::binary);
    if (!f) { fprintf(stderr, "Cannot open %s\n", out_path); return 1; }

    uint32_t magic = 0x46546C67, version = 2; // "glTF"
    f.write((char*)&magic, 4);
    f.write((char*)&version, 4);
    f.write((char*)&total, 4);

    uint32_t json_type = 0x4E4F534A; // "JSON"
    f.write((char*)&json_len, 4);
    f.write((char*)&json_type, 4);
    f.write(json.data(), json_len);

    uint32_t bin_chunk_len = bin_size + bin_pad;
    uint32_t bin_type = 0x004E4942; // "BIN\0"
    f.write((char*)&bin_chunk_len, 4);
    f.write((char*)&bin_type, 4);
    f.write((char*)bin.data(), bin_size);
    uint8_t pad[4] = {};
    if (bin_pad > 0) f.write((char*)pad, bin_pad);

    f.close();
    printf("Wrote %s (%u bytes)\n", out_path, total);
    return 0;
}
