//  Template for terrain shaders for blending up to 7 textures and using up to 8 pointlights.

#if defined(__INTELLISENSE__)
    #define PS
    #define NUM_PT_LIGHTS 24
#endif

#define TERRAIN

#define REVERSED_DEPTH

#ifndef TEX_COUNT
    #define TEX_COUNT 1
#endif

#if defined(NUM_PT_LIGHTS) && (NUM_PT_LIGHTS > 24)
    #define NUM_PT_LIGHTS 24
#endif

#include "includes/Helpers.hlsl"
#include "includes/Terrain.hlsl"
#include "includes/Parallax.hlsl"

// BaseMap[7] holds s0-s6 and NormalMap[7] holds s7-s13, so the atlas cannot use the s9
// default. s14/s15 are the only free sampler slots in ps_3_0 here.
#define SHADOW_ATLAS_SAMPLER_REG s14
#include "includes/Shadow.hlsl"


struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float4 blend_0 : TEXCOORD1;
    float4 blend_1 : TEXCOORD2;
};

struct VS_OUTPUT {
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1;
    float3 lPosition : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
    float3 normal : TEXCOORD5;
    float4 projectionPosition : TEXCOORD6;
    float3 viewPosition : TEXCOORD7;
};

#ifdef VS

row_major float4x4 ModelViewProj : register(c0);

float4 FogParam : register(c14);
float3 FogColor : register(c15);
float4 EyePosition : register(c16);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 posPS = mul(float4x4(ModelViewProj[0].xyzw, ModelViewProj[1].xyzw, ModelViewProj[2].xyzw, ModelViewProj[3].xyzw), IN.position.xyzw);

    OUT.blend_0 = IN.blend_0;
    OUT.blend_1 = IN.blend_1;

    OUT.sPosition.xyzw = posPS;
    OUT.uv.xy = IN.uv.xy;
    OUT.vertex_color.xyz = clamp(IN.vertex_color.rgb, 0.0f, 1.0f);
    OUT.lPosition.xyz = IN.position.xyz;
    OUT.tangent.xyz = IN.tangent.xyz;
    OUT.binormal.xyz = IN.binormal.xyz;
    OUT.normal.xyz = IN.normal.xyz;
    OUT.projectionPosition.xyzw = posPS;
    OUT.viewPosition.xyz = EyePosition.xyz;

    return OUT;
};

#endif  // Vertex shader.

struct PS_INPUT
{
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1_centroid;
    float3 lPosition : TEXCOORD2_centroid;
    float3 tangent : TEXCOORD3_centroid;
    float3 binormal : TEXCOORD4_centroid;
    float3 normal : TEXCOORD5_centroid;
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 projectionPosition : TEXCOORD6_centroid;
    float3 viewPosition : TEXCOORD7_centroid;
    float4 sPosition : POSITION1;
    float2 screenPos : VPOS;
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#ifdef PS

sampler2D BaseMap[7]:register(s0);
sampler2D NormalMap[7]:register(s7);

float4 AmbientColor : register(c1);
float4 SunColor : register(c3);
float4 SunDir : register(c18);

float4 LandSpec[2] : register(c32);
float4 LandHeight[2] : register(c34);
float4 FogParam : register(c36);
float4 FogColor : register(c37);

#ifdef NUM_PT_LIGHTS
float4 PointLightColor[NUM_PT_LIGHTS] : register(c39);
float4 PointLightPosition[NUM_PT_LIGHTS] : register(c63);
float PointLightCount : register(c88);
#endif

// Terrain blending ([Shaders.Terrain.Blending]), after Community Shaders' Terrain Blending. The world's depth as
// the land started drawing holds the objects drawn before it; where the land covers one of them by less than the
// blend distance it fades out, so the object's buried part shows through and sinks into the ground over that
// depth instead of ending in a hard line. Faded with a moving dither (no blending in this pass); TAA smooths it.
float4 TESR_TerrainBlendData;   // x: on for this draw, y: blend distance, z: max distance, w: debug view
float4 TESR_TerrainBlendFrame;  // x: frame index
float4 TESR_CameraData;         // x: near, y: far
float4 TESR_DepthConstants;     // z: 1 for reversed depth
float4 TESR_ReciprocalResolution;
sampler2D TESR_TerrainBlendDepth : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

// Distance along the view axis from a depth buffer value.
float terrainBlendLinearDepth(float depth) {
    float nearZ = TESR_CameraData.x;
    float farZ = TESR_CameraData.y;
    float d = TESR_DepthConstants.z > 0.5f ? depth : 1.0f - depth;
    return nearZ * farZ / (nearZ + d * (farZ - nearZ));
}

// How much of the land to keep at this pixel: 1 opaque, 0 faded away; -1 when blending doesn't apply.
float getTerrainBlend(float4 projectionPosition, float2 screenPos) {
    float terrainDepth = terrainBlendLinearDepth(projectionPosition.z / projectionPosition.w);
    float maxDistance = TESR_TerrainBlendData.z;
    if (terrainDepth > maxDistance) return -1.0f;

    float2 uv = (screenPos + 0.5f) * TESR_ReciprocalResolution.xy;
    float sceneDepth = terrainBlendLinearDepth(tex2Dlod(TESR_TerrainBlendDepth, float4(uv, 0.0f, 0.0f)).x);
    float covered = sceneDepth - terrainDepth;  // how far behind the land the object is
    if (covered <= 0.0f) return -1.0f;          // the object is in front: the depth test handles it

    float keep = saturate(covered / TESR_TerrainBlendData.y);
    // Back to a hard edge over the last quarter before the max distance, so there's no visible line.
    return lerp(keep, 1.0f, saturate((terrainDepth / maxDistance - 0.75f) * 4.0f));
}

// Interleaved gradient noise, moved every frame.
float terrainBlendNoise(float2 screenPos) {
    float2 p = screenPos + 5.588238f * TESR_TerrainBlendFrame.x;
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    int texCount = TEX_COUNT;  // Macro.
    float3 tangent = normalize(IN.tangent.xyz);
    float3 binormal = normalize(IN.binormal.xyz);
    float3 normal = normalize(IN.normal.xyz);
    float3x3 tbn = float3x3(tangent, binormal, normal);
    float3 eyeDir = normalize(mul(tbn, IN.viewPosition.xyz - IN.lPosition.xyz));
    float dist = length(IN.viewPosition.xyz - IN.lPosition.xyz);

    float2 dx, dy;
    dx = ddx(IN.uv.xy);
    dy = ddy(IN.uv.xy);

    float weights[7] = { 0, 0, 0, 0, 0, 0, 0 };
    float blends[7] = { IN.blend_0.x, IN.blend_0.y, IN.blend_0.z, IN.blend_0.w, IN.blend_1.x, IN.blend_1.y, IN.blend_1.z };
    float spec[7] = { LandSpec[0].x, LandSpec[0].y, LandSpec[0].z, LandSpec[0].w, LandSpec[1].x, LandSpec[1].y, LandSpec[1].z };
    float heightStatus[7] = { LandHeight[0].x, LandHeight[0].y, LandHeight[0].z, LandHeight[0].w, LandHeight[1].x, LandHeight[1].y, LandHeight[1].z };

    // Parallax (Includes/Parallax.hlsl, TERRAIN path); weights come back height-blended.
    float2 offsetUV = getParallaxCoords(dist, IN.uv.xy, dx, dy, eyeDir, texCount, BaseMap, blends, heightStatus, weights);

    float gloss = 0.0f;
    float specExponent = 0.0f;
    float3 baseColor = blendDiffuseMaps(IN.vertex_color, offsetUV, texCount, BaseMap, weights);
    float3 combinedNormal = blendNormalMaps(offsetUV, texCount, NormalMap, weights, spec, gloss, specExponent);

    // Linear lighting ([Shaders.PBR.Main] LinearLighting), PBR path only; see "Lighting space"
    // in PBR.hlsl. The albedo is decoded once here and the result encoded before fog.
    linearLighting = TESR_TerrainSkyData.z > 0.0f && TESR_TerrainExtraData.x;
    baseColor = decodeColor(baseColor);

    float3 lightTS = mul(tbn, SunDir.xyz);
    // Parallax self-shadowing has its own switch ([Shaders.Terrain.Parallax] Shadows) and works with
    // parallax itself off.
    float parallaxShadowMultiplier = getParallaxShadowMultipler(dist, offsetUV, dx, dy, lightTS, texCount, blends, heightStatus, BaseMap);

    // Forward sun shadows. Folded into parallaxShadowMultiplier, which getSunLighting
    // applies to the sun colour only -- ambient is added afterwards and stays untouched.
    //
    // No extra interpolator needed: projectionPosition is already the clip-space position,
    // and clip position is affine in object space, so it interpolates exactly.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    float3 shadowWorldPos = GetShadowWorldPos(IN.projectionPosition);
    float3 shadowNormal = GetShadowGeometricNormal(shadowWorldPos);
    #if FORWARD_SHADOWS
    parallaxShadowMultiplier *= GetSunShadow(shadowWorldPos, shadowNormal);
    #endif

    // World normal with the normal map, for the sky light and reflections. Land is never rotated,
    // so its object-space frame is world-aligned and the interpolated tangent frame is exact.
    float3 terrainWorldNormal = normalize(combinedNormal.x * tangent + combinedNormal.y * binormal + combinedNormal.z * normal);
    float3 ambientNormal = getTerrainAmbientNormal(terrainWorldNormal, shadowNormal);

    // Reflection first: it sets the share of the ambient it takes (skyReflectedFraction).
    float terrainRoughness = getTerrainRoughness(specExponent);
    float3 skyReflection = 0.0f;
    [branch] if (TESR_TerrainExtraData.x)
        skyReflection = getTerrainSkyReflection(shadowWorldPos, shadowNormal, ambientNormal, terrainRoughness, gloss);

    float3 lighting = getSunLighting(lightTS, SunColor.rgb, eyeDir, combinedNormal, AmbientColor.rgb, baseColor, gloss, specExponent, 1.0, parallaxShadowMultiplier, ambientNormal);
    lighting += skyReflection;

    #if defined(NUM_PT_LIGHTS)
        [loop] for (int i = 0; i < PointLightCount; i++) {
            float3 pointlightDir = PointLightPosition[i].xyz - IN.lPosition.xyz;
            float att = vanillaAtt(pointlightDir, PointLightPosition[i].w);

            [branch]
            if (att > 0.001) {
                pointlightDir = mul(tbn, pointlightDir);
                lighting += getPointLightLighting(pointlightDir, att, PointLightColor[i].rgb, eyeDir, combinedNormal, baseColor, gloss, specExponent);
            }
        }
    #endif

    // Per pixel fog.
    float4 fog;
    float3 fogPos = IN.projectionPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = IN.projectionPosition.w - IN.projectionPosition.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    fog.rgb = FogColor.rgb;
    fog.a = pow(fogStrength, FogParam.z);

    float3 finalColor = encodeColor(lighting);
    finalColor = lerp(finalColor, fog.rgb, fog.a); // Apply fog.

    [branch] if (TESR_TerrainPBRData.w > 0.0f)
        finalColor = getTerrainDebug(TESR_TerrainPBRData.w, terrainRoughness, getTerrainSpecularScale(gloss, terrainRoughness), ambientNormal);

    [branch] if (TESR_TerrainBlendData.x > 0.0f) {
        float keep = getTerrainBlend(IN.projectionPosition, IN.screenPos);
        if (keep >= 0.0f) {
            if (TESR_TerrainBlendData.w > 0.0f)
                finalColor = lerp(float3(1.0f, 0.0f, 1.0f), finalColor, keep);  // debug: the blend band in magenta
            else if (keep < 1.0f)
                clip(keep - terrainBlendNoise(IN.screenPos));
        }
    }

    OUT.color_0.a = 1;
    OUT.color_0.rgb = finalColor;

    return OUT;
};

#endif  // Pixel shader.
