// Grass PS -- vanilla GRASS23x000TMS.pso plus forward sun shadow.
// Shared by GRASS23x002.vso (LFS) and GRASS23x003.vso (LVS).
//
// The VS hands the lighting over already split, so only the sun is shadowed:
//   TEXCOORD4.xyz ambient    TEXCOORD5.xyz sun    TEXCOORD5.w fade    COLOR0 fog (.w amount)

#include "includes/Shadow.hlsl"
#include "includes/PBRScale.hlsl"

sampler2D DiffuseMap : register(s0);

struct PS_INPUT {
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float3 ambient        : TEXCOORD4_centroid;
    float4 sun            : TEXCOORD5_centroid;   // .w = distance fade
    float4 fog            : COLOR0;               // .w = fog amount
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    float3 sun = IN.sun.xyz;

    // Outside the guard: the skylight needs this normal whether or not forward shadows
    // are compiled in, and ForwardShadows is a live setting that can switch them off.
    float3 shadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
#if FORWARD_SHADOWS
    // ddx/ddy must stay at top level, outside dynamic flow control.
    sun *= SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
         ? GetSunShadow(IN.shadowWorldPos.xyz, shadowNormal)
         : 1.0f;
#endif

    // Same split getSunLighting/getAmbientLighting apply on the object path.
    float3 lighting = PBRLight(sun) + PBRAmbient(IN.ambient.xyz) + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);

    float4 albedo = tex2D(DiffuseMap, IN.uv.xy);
    float3 litColor = lighting * albedo.rgb;

    OUT.color.rgb = lerp(litColor, IN.fog.rgb, IN.fog.w);
    OUT.color.a = saturate(albedo.a * 1.75f) * IN.sun.w;

    return OUT;
};
