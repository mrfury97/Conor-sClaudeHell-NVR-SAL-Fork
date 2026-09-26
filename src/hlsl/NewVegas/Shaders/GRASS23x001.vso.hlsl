// Grass VS -- vanilla GRASS23x001.vso (BSSM_GRASS_DIRONLY_LV), plus shadowWorldPos.
// GRASS23x000 except the sun term uses the raw vertex normal. lightScale comes from frac of
// inst.w alone.
//
// REGISTER HAZARD
// As 002: InstanceData spans c20..c247, shadow matrices relocated to c248/c252.
#define SHADOW_INVPROJ_REG c248
#define SHADOW_INVVIEW_REG c252
#include "includes/Shadow.hlsl"

float3 DiffuseDir   : register(c0);
float3 DiffuseColor : register(c1);
float3 ScaleMask    : register(c2);
float4 WindData     : register(c4);
float4 AlphaParam   : register(c5);
float4 AmbientColor : register(c6);
float4 AddlParams   : register(c7);
row_major float4x4 ModelViewProj : register(c9);
float4 FogColor     : register(c14);
float4 FogParam     : register(c15);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

struct VS_INPUT {
    float4 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
    float4 instance : TEXCOORD1;   // .x = instance index
};

struct VS_OUTPUT {
    float4 position       : POSITION;
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float4 ambient        : TEXCOORD4;
    float4 sun            : TEXCOORD5;   // .w = distance fade
    float4 fog            : COLOR0;      // .w = fog amount
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    int idx = (int)(IN.instance.x - frac(IN.instance.x));
    float4 inst = InstanceData[idx];

    // Both distances measure (x, y, w - z), not xyz.
    float4 instClip = mul(ModelViewProj, float4(inst.xyz, 1.0f));
    float instDist = length(float4(instClip.xy, instClip.w - instClip.z, instClip.w));
    OUT.sun.w = 1.0f - saturate((instDist - AlphaParam.z) / AlphaParam.w);

    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float sway = sin(phase) * WindData.z * (IN.color.w * IN.color.w);

    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float3 pos = IN.position.xyz * scale + float3(sway * WindData.xy, 0.0f);

    float4 worldPos = float4(pos + inst.xyz, 1.0f);
    OUT.position = mul(ModelViewProj, worldPos);

    float lightScale = frac(inst.w) * 0.75f + 0.25f;
    float NdotL = saturate(dot(DiffuseDir, IN.normal));

    OUT.ambient = lightScale * AmbientColor;
    OUT.sun.xyz = ((lightScale * IN.color.rgb) * DiffuseColor) * NdotL * AddlParams.x;

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    OUT.uv = IN.uv;
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.position), SHADOW_VS_SENTINEL);

    return OUT;
};
