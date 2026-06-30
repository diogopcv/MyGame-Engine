cbuffer PerFrame : register(b0)
{
    row_major float4x4 view;
    row_major float4x4 proj;
    float3 camera_pos;
    float  _pad0;
    // remaining PerFrame fields (lights, shadow) are not used in VS
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
    float4 tangent  : TANGENT;  // xyz = tangent direction, w = bitangent sign
};

struct VSOutput
{
    float4 sv_pos    : SV_Position;
    float3 world_pos : WORLD_POS;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 world_pos = mul(float4(input.position, 1.0), model);
    o.world_pos = world_pos.xyz;
    o.sv_pos    = mul(mul(world_pos, view), proj);
    o.uv        = input.uv;

    float3x3 nm3 = (float3x3)normal_matrix;
    o.normal    = normalize(mul(input.normal,      nm3));
    o.tangent   = normalize(mul(input.tangent.xyz, nm3));
    // Reconstruct bitangent using the sign stored in tangent.w
    o.bitangent = cross(o.normal, o.tangent) * input.tangent.w;

    return o;
}
