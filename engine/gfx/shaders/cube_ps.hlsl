// Phase 1, Milestone 4: samples the bound texture with the bound sampler and
// outputs the result unchanged.

Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    return g_texture.Sample(g_sampler, input.uv);
}
