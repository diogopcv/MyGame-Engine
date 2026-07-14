// Skinned variant of pbr_vs.hlsl: linear blend skinning from a bone-matrix
// palette (b3), then the identical world/view/proj path. The pixel shader
// (pbr_ps.hlsl) is shared — skinning only changes where vertices end up.
//
// Palette matrices are model-space x inverse-bind (built by the app from
// ge_anim's pose_to_palette); the PerObject model matrix still applies the
// entity's world transform on top, so skinned and static draws share the
// same world-transform path. Joint indices follow the glTF skin's joints
// array order (the ge_anim/ge_gfx sibling contract).

#define MAX_PALETTE_JOINTS 256

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

// b2 is PerMaterial (pixel-shader only); the palette lives at b3.
cbuffer BonePalette : register(b3)
{
    row_major float4x4 bones[MAX_PALETTE_JOINTS];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float4 color    : COLOR;
    float4 tangent  : TANGENT;  // xyz = tangent direction, w = bitangent sign
    uint4  joints   : JOINTS;   // palette indices (u8 in the vertex buffer)
    float4 weights  : WEIGHTS;  // sum to 1
};

struct VSOutput
{
    float4 sv_pos    : SV_Position;
    float3 world_pos : WORLD_POS;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD0;
    float2 uv1       : TEXCOORD1;
    float4 color     : COLOR;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
};

VSOutput main(VSInput input)
{
    row_major float4x4 skin =
        bones[input.joints.x] * input.weights.x +
        bones[input.joints.y] * input.weights.y +
        bones[input.joints.z] * input.weights.z +
        bones[input.joints.w] * input.weights.w;

    float4 pos_model = mul(float4(input.position, 1.0), skin);
    // Rotation part of the skin matrix for directions (LBS approximation:
    // exact for rigid bone transforms, standard for the general case).
    float3x3 skin3 = (float3x3)skin;
    float3 nrm_model = mul(input.normal,      skin3);
    float3 tan_model = mul(input.tangent.xyz, skin3);

    VSOutput o;

    float4 world_pos = mul(pos_model, model);
    o.world_pos = world_pos.xyz;
    o.sv_pos    = mul(mul(world_pos, view), proj);
    o.uv        = input.uv;
    o.uv1       = input.uv1;
    o.color     = input.color;

    float3x3 nm3 = (float3x3)normal_matrix;
    o.normal    = normalize(mul(nrm_model, nm3));
    o.tangent   = normalize(mul(tan_model, nm3));
    // Reconstruct bitangent using the sign stored in tangent.w
    o.bitangent = cross(o.normal, o.tangent) * input.tangent.w;

    return o;
}
