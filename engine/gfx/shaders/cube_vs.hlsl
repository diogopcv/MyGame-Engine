// Phase 1, Milestone 4: transforms cube vertices by the MVP matrix from the
// per-frame constant buffer (root CBV at b0).

cbuffer CubeCB : register(b0) {
    row_major float4x4 mvp;
};

struct VSInput {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput main(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), mvp);
    output.uv       = input.uv;
    return output;
}
