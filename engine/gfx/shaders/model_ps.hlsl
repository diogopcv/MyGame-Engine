Texture2D         albedo_tex  : register(t0);
Texture2D<float>  shadow_map  : register(t1);
SamplerState      samp        : register(s0);
SamplerComparisonState shadow_samp : register(s1);

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

struct PSInput
{
    float4 position  : SV_Position;
    float3 world_pos : WORLD_POS;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
};

float calc_shadow(float3 world_pos)
{
    float4 light_space = mul(float4(world_pos, 1.0), light_view_proj);
    float3 proj_coords = light_space.xyz / light_space.w;

    // NDC to [0,1] UV (left-handed, [0,1] depth already in range)
    float2 shadow_uv = float2(proj_coords.x * 0.5 + 0.5,
                               -proj_coords.y * 0.5 + 0.5);
    float  depth = proj_coords.z;

    // Outside shadow map bounds → fully lit
    if (shadow_uv.x < 0 || shadow_uv.x > 1 || shadow_uv.y < 0 || shadow_uv.y > 1)
        return 1.0;

    // 3x3 PCF
    float shadow = 0.0;
    float texel = 1.0 / 2048.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            shadow += shadow_map.SampleCmpLevelZero(shadow_samp,
                shadow_uv + float2(x, y) * texel, depth);
        }
    }
    return shadow / 9.0;
}

float4 main(PSInput input) : SV_Target
{
    float4 albedo = albedo_tex.Sample(samp, input.uv);
    float3 N = normalize(input.normal);
    float3 V = normalize(camera_pos - input.world_pos);

    float3 ambient = 0.08;
    float3 result  = albedo.rgb * ambient;

    // Directional light (Blinn-Phong) with shadow
    {
        float3 L = normalize(sun_direction);
        float  NdotL = saturate(dot(N, L));
        float3 H = normalize(L + V);
        float  NdotH = saturate(dot(N, H));
        float  spec = pow(NdotH, 64.0);

        float shadow = (shadow_enabled > 0.5) ? calc_shadow(input.world_pos) : 1.0;

        result += albedo.rgb * sun_color * sun_intensity * NdotL * shadow;
        result += sun_color * sun_intensity * spec * 0.3 * shadow;
    }

    // Point lights (unaffected by shadow map)
    for (int i = 0; i < point_light_count && i < 4; ++i)
    {
        float3 light_pos = point_positions[i].xyz;
        float  radius    = point_positions[i].w;
        float3 light_col = point_colors[i].xyz;
        float  intensity = point_colors[i].w;

        float3 L_vec = light_pos - input.world_pos;
        float  dist  = length(L_vec);
        float3 L     = L_vec / max(dist, 0.001);

        float attenuation = saturate(1.0 - dist / radius);
        attenuation *= attenuation;

        float  NdotL = saturate(dot(N, L));
        float3 H = normalize(L + V);
        float  NdotH = saturate(dot(N, H));
        float  spec = pow(NdotH, 64.0);

        result += albedo.rgb * light_col * intensity * NdotL * attenuation;
        result += light_col * intensity * spec * 0.3 * attenuation;
    }

    return float4(result, albedo.a);
}
