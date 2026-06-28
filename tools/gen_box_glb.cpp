// Standalone tool: generates assets/models/box.glb — a unit cube with normals and UVs.
// Compile: cl /EHsc /std:c++20 gen_box_glb.cpp /Fe:gen_box_glb.exe
// Run:     gen_box_glb.exe ../assets/models/box.glb

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

struct Vertex {
    Vec3 pos;
    Vec3 nrm;
    Vec2 uv;
};

int main(int argc, char* argv[]) {
    const char* out_path = (argc > 1) ? argv[1] : "box.glb";

    // 24 vertices (4 per face), 36 indices
    Vertex verts[24];
    uint32_t indices[36];

    auto face = [&](int fi, Vec3 n,
                    Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3) {
        int b = fi * 4;
        verts[b+0] = {p0, n, {0,1}};
        verts[b+1] = {p1, n, {1,1}};
        verts[b+2] = {p2, n, {1,0}};
        verts[b+3] = {p3, n, {0,0}};
        int bi = fi * 6;
        indices[bi+0] = b+0; indices[bi+1] = b+1; indices[bi+2] = b+2;
        indices[bi+3] = b+0; indices[bi+4] = b+2; indices[bi+5] = b+3;
    };

    float k = 0.5f;
    face(0, { 0, 0,-1}, {-k,-k,-k},{ k,-k,-k},{ k, k,-k},{-k, k,-k}); // front (-Z)
    face(1, { 0, 0, 1}, { k,-k, k},{-k,-k, k},{-k, k, k},{ k, k, k}); // back (+Z)
    face(2, {-1, 0, 0}, {-k,-k, k},{-k,-k,-k},{-k, k,-k},{-k, k, k}); // left (-X)
    face(3, { 1, 0, 0}, { k,-k,-k},{ k,-k, k},{ k, k, k},{ k, k,-k}); // right (+X)
    face(4, { 0, 1, 0}, {-k, k,-k},{ k, k,-k},{ k, k, k},{-k, k, k}); // top (+Y)
    face(5, { 0,-1, 0}, {-k,-k, k},{ k,-k, k},{ k,-k,-k},{-k,-k,-k}); // bottom (-Y)

    // Binary buffer: positions | normals | UVs | indices
    uint32_t pos_bytes = 24 * sizeof(Vec3);
    uint32_t nrm_bytes = 24 * sizeof(Vec3);
    uint32_t uv_bytes  = 24 * sizeof(Vec2);
    uint32_t idx_bytes = 36 * sizeof(uint32_t);
    uint32_t bin_size  = pos_bytes + nrm_bytes + uv_bytes + idx_bytes;

    std::vector<uint8_t> bin(bin_size);
    uint32_t off = 0;
    for (int i = 0; i < 24; ++i) { memcpy(&bin[off], &verts[i].pos, sizeof(Vec3)); off += sizeof(Vec3); }
    for (int i = 0; i < 24; ++i) { memcpy(&bin[off], &verts[i].nrm, sizeof(Vec3)); off += sizeof(Vec3); }
    for (int i = 0; i < 24; ++i) { memcpy(&bin[off], &verts[i].uv,  sizeof(Vec2)); off += sizeof(Vec2); }
    memcpy(&bin[off], indices, idx_bytes);

    // Compute min/max for position accessor
    Vec3 pmin = verts[0].pos, pmax = verts[0].pos;
    for (int i = 1; i < 24; ++i) {
        auto& p = verts[i].pos;
        if (p.x < pmin.x) pmin.x = p.x; if (p.x > pmax.x) pmax.x = p.x;
        if (p.y < pmin.y) pmin.y = p.y; if (p.y > pmax.y) pmax.y = p.y;
        if (p.z < pmin.z) pmin.z = p.z; if (p.z > pmax.z) pmax.z = p.z;
    }

    uint32_t nrm_off = pos_bytes;
    uint32_t uv_off  = nrm_off + nrm_bytes;
    uint32_t idx_off = uv_off + uv_bytes;

    // JSON chunk
    std::string json = R"({
  "asset":{"version":"2.0","generator":"gen_box_glb"},
  "scene":0,
  "scenes":[{"nodes":[0]}],
  "nodes":[{"mesh":0}],
  "meshes":[{"primitives":[{
    "attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
    "indices":3,
    "material":0
  }]}],
  "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.8,0.6,0.4,1.0]}}],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":24,"type":"VEC3",
     "min":[)" + std::to_string(pmin.x) + "," + std::to_string(pmin.y) + "," + std::to_string(pmin.z) + R"(],
     "max":[)" + std::to_string(pmax.x) + "," + std::to_string(pmax.y) + "," + std::to_string(pmax.z) + R"(]},
    {"bufferView":1,"componentType":5126,"count":24,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":24,"type":"VEC2"},
    {"bufferView":3,"componentType":5125,"count":36,"type":"SCALAR"}
  ],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":)" + std::to_string(pos_bytes) + R"(,"target":34962},
    {"buffer":0,"byteOffset":)" + std::to_string(nrm_off) + R"(,"byteLength":)" + std::to_string(nrm_bytes) + R"(,"target":34962},
    {"buffer":0,"byteOffset":)" + std::to_string(uv_off) + R"(,"byteLength":)" + std::to_string(uv_bytes) + R"(,"target":34962},
    {"buffer":0,"byteOffset":)" + std::to_string(idx_off) + R"(,"byteLength":)" + std::to_string(idx_bytes) + R"(,"target":34963}
  ],
  "buffers":[{"byteLength":)" + std::to_string(bin_size) + R"(}]
})";

    // Pad JSON to 4-byte boundary with spaces
    while (json.size() % 4 != 0) json += ' ';

    // GLB structure
    uint32_t json_len = (uint32_t)json.size();
    uint32_t bin_pad  = (4 - (bin_size % 4)) % 4;
    uint32_t total    = 12 + 8 + json_len + 8 + bin_size + bin_pad;

    std::ofstream f(out_path, std::ios::binary);
    if (!f) { fprintf(stderr, "Cannot open %s\n", out_path); return 1; }

    // Header
    uint32_t magic   = 0x46546C67; // "glTF"
    uint32_t version = 2;
    f.write((char*)&magic, 4);
    f.write((char*)&version, 4);
    f.write((char*)&total, 4);

    // JSON chunk
    uint32_t json_type = 0x4E4F534A; // "JSON"
    f.write((char*)&json_len, 4);
    f.write((char*)&json_type, 4);
    f.write(json.data(), json_len);

    // BIN chunk
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
