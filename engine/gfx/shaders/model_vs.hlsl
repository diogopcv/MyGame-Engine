cbuffer PerFrame : register(b0)
{
    row_major float4x4 view;
    row_major float4x4 proj;
    float3 camera_pos;
    float  _pad0;
    float3 sun_direction;
    float  _pad1;
    float3 sun_color;
    float  sun_intensity;
    float4 point_positions[4];
    float4 point_colors[4];
    int    point_light_count;
    float  shadow_enabled;
    float2 _pad2;
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

struct VSOutput
{
    float4 position  : SV_Position;
    float3 world_pos : WORLD_POS;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = mul(float4(input.position, 1.0), model);
    output.world_pos = world_pos.xyz;
    output.position  = mul(mul(world_pos, view), proj);
    output.normal    = normalize(mul(input.normal, (float3x3)normal_matrix));
    output.uv        = input.uv;
    return output;
}
