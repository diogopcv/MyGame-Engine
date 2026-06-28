cbuffer PerFrame : register(b0)
{
    row_major float4x4 light_view_proj;
};

cbuffer PerObject : register(b1)
{
    row_major float4x4 model;
    row_major float4x4 normal_matrix;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

float4 main(VSInput input) : SV_Position
{
    float4 world_pos = mul(float4(input.position, 1.0), model);
    return mul(world_pos, light_view_proj);
}
