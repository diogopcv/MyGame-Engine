// Skinned variant of shadow_vs.hlsl — without it, characters would cast
// rest-pose shadows. Same palette contents as pbr_skinned_vs (the renderer
// reuses the identical upload-ring allocation), bound here at b2 since the
// depth-only root signature has no PerMaterial slot.

#define MAX_PALETTE_JOINTS 256

cbuffer PerFrame : register(b0)
{
    row_major float4x4 light_view_proj;
};

cbuffer PerObject : register(b1)
{
    row_major float4x4 model;
    row_major float4x4 normal_matrix;
};

cbuffer BonePalette : register(b2)
{
    row_major float4x4 bones[MAX_PALETTE_JOINTS];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    uint4  joints   : JOINTS;
    float4 weights  : WEIGHTS;
};

float4 main(VSInput input) : SV_Position
{
    row_major float4x4 skin =
        bones[input.joints.x] * input.weights.x +
        bones[input.joints.y] * input.weights.y +
        bones[input.joints.z] * input.weights.z +
        bones[input.joints.w] * input.weights.w;

    float4 pos_model = mul(float4(input.position, 1.0), skin);
    float4 world_pos = mul(pos_model, model);
    return mul(world_pos, light_view_proj);
}
