static const float PI = 3.14159265358979323846f;

// --- Texture bindings (t0-t5) ------------------------------------------------
Texture2D         base_color_tex : register(t0); // sRGB base colour
Texture2D         mr_tex         : register(t1); // G=roughness, B=metallic (linear)
Texture2D         normal_tex     : register(t2); // tangent-space normals
Texture2D         occlusion_tex  : register(t3); // R = AO factor (linear)
Texture2D         emissive_tex   : register(t4); // sRGB emissive colour
Texture2D<float>  shadow_map     : register(t5); // shadow depth (comparison)

SamplerState           samp        : register(s0);
SamplerComparisonState shadow_samp : register(s1);

// --- Constant buffers --------------------------------------------------------
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
    float4 point_positions[4];   // xyz=pos, w=radius
    float4 point_colors[4];      // xyz=color, w=intensity
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

cbuffer PerMaterial : register(b2)
{
    float4 base_color_factor;    // RGBA multiplier
    float  metallic_factor;
    float  roughness_factor;
    float2 _pad_mat0;
    float3 emissive_factor;      // linear emissive scale
    float  has_normal_tex;       // bool as float
    float  has_mr_tex;
    float  has_occlusion_tex;
    float  has_emissive_tex;
    float  has_base_color_tex;
};

// --- Vertex shader output (pixel shader input) --------------------------------
struct PSInput
{
    float4 sv_pos    : SV_Position;
    float3 world_pos : WORLD_POS;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BITANGENT;
};

// --- Utility ------------------------------------------------------------------
float3 srgb_to_linear(float3 c)
{
    return pow(max(c, 0.0f), 2.2f);
}

// GGX/Trowbridge-Reitz normal distribution
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-7f);
}

// Smith's height-correlated GGX masking-shadowing (Schlick approximation)
float G_Smith_Schlick(float NdotV, float NdotL, float roughness)
{
    float r  = roughness + 1.0f;
    float k  = (r * r) / 8.0f;
    float gv = NdotV / (NdotV * (1.0f - k) + k);
    float gl = NdotL / (NdotL * (1.0f - k) + k);
    return gv * gl;
}

// Schlick Fresnel
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

// 3x3 PCF shadow lookup
float calc_shadow(float3 world_pos)
{
    float4 ls        = mul(float4(world_pos, 1.0f), light_view_proj);
    float3 ndc       = ls.xyz / ls.w;
    float2 shadow_uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    float  depth     = ndc.z;

    if (any(shadow_uv < 0.0f) || any(shadow_uv > 1.0f))
        return 1.0f;

    float shadow = 0.0f;
    float texel  = 1.0f / 2048.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            shadow += shadow_map.SampleCmpLevelZero(
                shadow_samp, shadow_uv + float2(x, y) * texel, depth);
        }
    }
    return shadow / 9.0f;
}

// Single-light Cook-Torrance BRDF evaluation
float3 eval_pbr(float3 N, float3 V, float3 L,
                float3 albedo, float metallic, float roughness,
                float3 radiance)
{
    float3 H     = normalize(V + L);
    float NdotL  = saturate(dot(N, L));
    float NdotV  = saturate(dot(N, V)) + 1e-5f;
    float NdotH  = saturate(dot(N, H));
    float HdotV  = saturate(dot(H, V));

    float3 F0      = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float  D       = D_GGX(NdotH, roughness);
    float3 F       = F_Schlick(HdotV, F0);
    float  G       = G_Smith_Schlick(NdotV, NdotL, roughness);

    float3 specular = (D * F * G) / max(4.0f * NdotV * NdotL, 1e-5f);
    float3 kD       = (1.0f - F) * (1.0f - metallic);
    float3 diffuse  = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

// --- Main --------------------------------------------------------------------
float4 main(PSInput input) : SV_Target
{
    // Build tangent-space basis
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);

    if (has_normal_tex > 0.5f)
    {
        float3 n_ts = normal_tex.Sample(samp, input.uv).xyz * 2.0f - 1.0f;
        N = normalize(T * n_ts.x + B * n_ts.y + N * n_ts.z);
    }

    // Base colour (sRGB → linear)
    float4 base;
    if (has_base_color_tex > 0.5f)
    {
        float4 s = base_color_tex.Sample(samp, input.uv);
        base     = float4(srgb_to_linear(s.rgb), s.a);
    }
    else
    {
        base = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    base *= base_color_factor;

    // Metallic / roughness
    float metallic  = metallic_factor;
    float roughness = roughness_factor;
    if (has_mr_tex > 0.5f)
    {
        float4 mr = mr_tex.Sample(samp, input.uv);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic  = saturate(metallic);

    // Ambient occlusion
    float ao = 1.0f;
    if (has_occlusion_tex > 0.5f)
        ao = occlusion_tex.Sample(samp, input.uv).r;

    float3 V = normalize(camera_pos - input.world_pos);

    // Indirect ambient (no IBL — simple approximation)
    float3 F0_approx = lerp(float3(0.04f, 0.04f, 0.04f), base.rgb, metallic);
    float3 ambient   = float3(0.03f, 0.03f, 0.03f) * base.rgb * ao;
    float3 result    = ambient;

    // Directional light (sun)
    float shadow = (shadow_enabled > 0.5f) ? calc_shadow(input.world_pos) : 1.0f;
    result += eval_pbr(N, V, normalize(sun_direction),
                       base.rgb, metallic, roughness,
                       sun_color * sun_intensity) * shadow;

    // Point lights
    for (int i = 0; i < point_light_count && i < 4; ++i)
    {
        float3 lp      = point_positions[i].xyz;
        float  radius  = point_positions[i].w;
        float3 lc      = point_colors[i].xyz;
        float  li      = point_colors[i].w;

        float3 L_vec   = lp - input.world_pos;
        float  dist    = length(L_vec);
        float3 L       = L_vec / max(dist, 1e-4f);
        float  atten   = saturate(1.0f - dist / radius);
        atten *= atten;

        result += eval_pbr(N, V, L,
                           base.rgb, metallic, roughness,
                           lc * li * atten);
    }

    // Emissive (sRGB → linear, then multiply by factor)
    float3 emissive = emissive_factor;
    if (has_emissive_tex > 0.5f)
        emissive *= srgb_to_linear(emissive_tex.Sample(samp, input.uv).rgb);
    result += emissive;

    // Reinhard tone mapping
    result = result / (result + 1.0f);

    // Gamma correction for sRGB display (render target is UNORM, not UNORM_SRGB)
    result = pow(max(result, 0.0f), 1.0f / 2.2f);

    return float4(result, base.a);
}
