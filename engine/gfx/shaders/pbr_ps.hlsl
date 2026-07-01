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

// Per-texture KHR_texture_transform + UV-set selector (2 float4 = 32 bytes).
struct TexXform
{
    float4 offset_scale; // xy = offset, zw = scale
    float4 rot_uvset;    // x = rotation (rad), y = uv_set (0/1)
};

cbuffer PerMaterial : register(b2)
{
    float4   base_color_factor;  // RGBA multiplier
    float4   mr_params;          // x=metallic y=roughness z=normal_scale w=occlusion_strength
    float4   emissive_alpha;     // xyz=emissive factor, w=alpha_cutoff
    float4   flags0;             // x=alpha_mode(0/1/2) y=has_base z=has_mr w=has_normal
    float4   flags1;             // x=has_occlusion y=has_emissive
    TexXform xform_base;
    TexXform xform_mr;
    TexXform xform_normal;
    TexXform xform_occlusion;
    TexXform xform_emissive;
};

// --- Vertex shader output (pixel shader input) --------------------------------
struct PSInput
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

// --- Utility ------------------------------------------------------------------
float3 srgb_to_linear(float3 c)
{
    return pow(max(c, 0.0f), 2.2f);
}

// Apply a glTF KHR_texture_transform and select the UV set.
float2 apply_xform(float2 uv0, float2 uv1, TexXform x)
{
    float2 uv = (x.rot_uvset.y > 0.5f) ? uv1 : uv0;
    float  s  = sin(x.rot_uvset.x);
    float  c  = cos(x.rot_uvset.x);
    float2 r  = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c); // rotate
    return r * x.offset_scale.zw + x.offset_scale.xy;             // scale, then offset
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

// --- Analytic environment IBL (procedural sky, no cubemap) --------------------
static const float3 kSkyColor    = float3(0.40f, 0.50f, 0.65f);
static const float3 kGroundColor = float3(0.18f, 0.16f, 0.14f);

float3 env_sample(float3 dir)
{
    return lerp(kGroundColor, kSkyColor, saturate(dir.y * 0.5f + 0.5f));
}

// Karis' split-sum analytic environment BRDF approximation (no LUT).
float3 env_brdf_approx(float3 F0, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4( 1.0f,  0.0425f,  1.040f, -0.04f);
    float4 r    = roughness * c0 + c1;
    float  a004 = min(r.x * r.x, exp2(-9.28f * NoV)) * r.x + r.y;
    float2 AB   = float2(-1.04f, 1.04f) * a004 + r.zw;
    return F0 * AB.x + AB.y;
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
    const float alpha_mode        = flags0.x; // 0=opaque 1=mask 2=blend
    const float has_base_color_tex = flags0.y;
    const float has_mr_tex         = flags0.z;
    const float has_normal_tex     = flags0.w;
    const float has_occlusion_tex  = flags1.x;
    const float has_emissive_tex   = flags1.y;

    // Transformed UVs per map.
    float2 uv_base = apply_xform(input.uv, input.uv1, xform_base);
    float2 uv_mr   = apply_xform(input.uv, input.uv1, xform_mr);
    float2 uv_nrm  = apply_xform(input.uv, input.uv1, xform_normal);
    float2 uv_ao   = apply_xform(input.uv, input.uv1, xform_occlusion);
    float2 uv_em   = apply_xform(input.uv, input.uv1, xform_emissive);

    // Base colour (sRGB → linear), modulated by vertex colour.
    float4 base;
    if (has_base_color_tex > 0.5f)
    {
        float4 s = base_color_tex.Sample(samp, uv_base);
        base     = float4(srgb_to_linear(s.rgb), s.a);
    }
    else
    {
        base = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    base *= base_color_factor;
    base *= input.color;

    // Alpha test for MASK materials.
    float alpha_cutoff = emissive_alpha.w;
    if (alpha_mode > 0.5f && alpha_mode < 1.5f)
        clip(base.a - alpha_cutoff);

    // Build tangent-space basis
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);

    if (has_normal_tex > 0.5f)
    {
        float3 n_ts = normal_tex.Sample(samp, uv_nrm).xyz * 2.0f - 1.0f;
        n_ts.xy *= mr_params.z; // normal_scale
        N = normalize(T * n_ts.x + B * n_ts.y + N * n_ts.z);
    }

    // Metallic / roughness
    float metallic  = mr_params.x;
    float roughness = mr_params.y;
    if (has_mr_tex > 0.5f)
    {
        float4 mr = mr_tex.Sample(samp, uv_mr);
        roughness *= mr.g;
        metallic  *= mr.b;
    }
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic  = saturate(metallic);

    // Ambient occlusion (occlusion_strength lerps between 1 and the sampled value)
    float ao = 1.0f;
    if (has_occlusion_tex > 0.5f)
    {
        float occ = occlusion_tex.Sample(samp, uv_ao).r;
        ao = lerp(1.0f, occ, mr_params.w); // mr_params.w = occlusion_strength
    }

    float3 V    = normalize(camera_pos - input.world_pos);
    float  NdotV = saturate(dot(N, V));

    // Indirect (analytic environment IBL)
    float3 F0_approx   = lerp(float3(0.04f, 0.04f, 0.04f), base.rgb, metallic);
    float3 R           = reflect(-V, N);
    float3 irradiance  = env_sample(N);
    float3 diffuse_ibl = irradiance * base.rgb * (1.0f - metallic);
    float3 specular_ibl = env_sample(R) * env_brdf_approx(F0_approx, roughness, NdotV);
    float3 ambient     = (diffuse_ibl + specular_ibl) * ao;
    float3 result      = ambient;

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
    float3 emissive = emissive_alpha.xyz;
    if (has_emissive_tex > 0.5f)
        emissive *= srgb_to_linear(emissive_tex.Sample(samp, uv_em).rgb);
    result += emissive;

    // Reinhard tone mapping
    result = result / (result + 1.0f);

    // Gamma correction for sRGB display (render target is UNORM, not UNORM_SRGB)
    result = pow(max(result, 0.0f), 1.0f / 2.2f);

    return float4(result, base.a);
}
