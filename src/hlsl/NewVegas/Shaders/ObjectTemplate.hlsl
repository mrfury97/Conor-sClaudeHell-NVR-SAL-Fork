// Template for object lighting shaders.
//
// VS
//
// AD
// SLS2028 - LIGHTS = 2 (ONLY_LIGHT)
// SLS2029 - LIGHTS = 2, SKIN (ONLY_LIGHT)
// SLS2030 - LIGHTS = 2, PROJ_SHADOW (ONLY_LIGHT)
// SLS2031 - LIGHTS = 2, PROJ_SHADOW, SKIN (ONLY_LIGHT)
// SLS2032 - LIGHTS = 3 (ONLY_LIGHT)
// SLS2033 - LIGHTS = 3, SKIN (ONLY_LIGHT)
// SLS2034 - LIGHTS = 3, PROJ_SHADOW (ONLY_LIGHT)
// SLS2035 - LIGHTS = 3, PROJ_SHADOW, SKIN (ONLY_LIGHT)
//
// ADTS
// SLS2000 - Base
// SLS2001 - LOD
// SLS2003 - SKIN
// SLS2004 - PROJ_SHADOW
// SLS2006 - PROJ_SHADOW, SKIN
// SLS2007 - STBB
// SLS2008 - LIGHTS = 2
// SLS2009 - LIGHTS = 2, SKIN
// SLS2010 - LIGHTS = 2, PROJ_SHADOW
// SLS2011 - LIGHTS = 2, PROJ_SHADOW, SKIN
// SLS2012 - SPECULAR
// SLS2013 - SPECULAR, SKIN
// SLS2014 - SPECULAR, PROJ_SHADOW
// SLS2015 - SPECULAR, PROJ_SHADOW, SKIN
// SLS2016 - SPECULAR, LIGHTS = 2
// SLS2017 - SPECULAR, LIGHTS = 2, SKIN
// SLS2018 - SPECULAR, LIGHTS = 2, PROJ_SHADOW
// SLS2019 - SPECULAR, LIGHTS = 2, PROJ_SHADOW, SKIN
// (ignored) SLS2102 - LIGHTS = 0
//
// ADTS10
// SLS2020 - LIGHTS = 9
// SLS2021 - LIGHTS = 9, SKIN
// SLS2022 - LIGHTS = 4
// SLS2023 - LIGHTS = 4, OPT
// SLS2024 - LIGHTS = 4, SKIN
// SLS2025 - LIGHTS = 4, SPECULAR
// SLS2026 - LIGHTS = 4, SPECULAR, OPT
// SLS2027 - LIGHTS = 4, SPECULAR, SKIN
//
// DiffusePt
// SLS2036 - LIGHTS = 2 (DIFFUSE)
// SLS2037 - LIGHTS = 2, SKIN (DIFFUSE)
// SLS2038 - LIGHTS = 3 (DIFFUSE)
// SLS2039 - LIGHTS = 3, SKIN (DIFFUSE)
//
// Specular
// SLS2040 - Base (ONLY_SPECULAR)
// SLS2041 - SKIN (ONLY_SPECULAR)
// SLS2042 - PROJ_SHADOW (ONLY_SPECULAR)
// SLS2043 - PROJ_SHADOW, SKIN (ONLY_SPECULAR)
// SLS2044 - POINT (ONLY_SPECULAR)
// SLS2045 - POINT, SKIN (ONLY_SPECULAR)
// SLS2046 - POINT, NUM_PT_LIGHTS = 2 (ONLY_SPECULAR)
// SLS2047 - POINT, NUM_PT_LIGHTS = 2, SKIN (ONLY_SPECULAR)
// SLS2048 - POINT, NUM_PT_LIGHTS = 3 (ONLY_SPECULAR)
// SLS2049 - POINT, NUM_PT_LIGHTS = 3, SKIN (ONLY_SPECULAR)
//
// PS
//
// AD
// SLS2037 - LIGHTS = 2 (ONLY_LIGHT, OPT)
// SLS2038 - LIGHTS = 2, SI (ONLY_LIGHT, OPT)
// SLS2039 - LIGHTS = 2, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2040 - LIGHTS = 2, SI, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2041 - LIGHTS = 3 (ONLY_LIGHT, OPT)
// SLS2042 - LIGHTS = 3, SI (ONLY_LIGHT, OPT)
// SLS2043 - LIGHTS = 3, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2044 - LIGHTS = 3, SI, PROJ_SHADOW (ONLY_LIGHT, OPT)
//
// ADTS
// SLS2000 - Default
// SLS2001 - OPT
// SLS2002 - OPT, LOD
// SLS2004 - SI 
// SLS2005 - PROJ_SHADOW
// SLS2007 - SI, PROJ_SHADOW 
// SLS2008 - STBB
// SLS2009 - HAIR
// SLS2010 - HAIR, PROJ_SHADOW
// SLS2011 - LIGHTS = 2
// SLS2012 - LIGHTS = 2, SI
// SLS2013 - LIGHTS = 2, HAIR
// SLS2014 - LIGHTS = 2, PROJ_SHADOW
// SLS2015 - LIGHTS = 2, SI, PROJ_SHADOW
// SLS2016 - LIGHTS = 2, HAIR, PROJ_SHADOW
// SLS2017 - SPECULAR
// SLS2018 - SPECULAR, SI
// SLS2019 - SPECULAR, HAIR
// SLS2020 - SPECULAR, PROJ_SHADOW
// SLS2021 - SPECULAR, SI, PROJ_SHADOW
// SLS2022 - SPECULAR, HAIR, PROJ_SHADOW
// SLS2023 - SPECULAR, LIGHTS = 2
// SLS2024 - SPECULAR, LIGHTS = 2, SI
// SLS2026 - SPECULAR, LIGHTS = 2, PROJ_SHADOW
// SLS2027 - SPECULAR, LIGHTS = 2, SI, PROJ_SHADOW
// (ignored) SLS2151 - LIGHTS = 0, SILHOUETTE
//
// ADTS10
// SLS2029 - LIGHTS = 9
// SLS2030 - LIGHTS = 9, SI
// SLS2031 - LIGHTS = 4
// SLS2032 - LIGHTS = 4, OPT 
// SLS2033 - LIGHTS = 4, SI
// SLS2034 - LIGHTS = 4, SPECULAR
// SLS2035 - LIGHTS = 4, SPECULAR, OPT
// SLS2036 - LIGHTS = 4, SPECULAR, SI
//
// DiffusePt
// SLS2045 - LIGHTS = 2 (DIFFUSE)
// SLS2046 - LIGHTS = 3 (DIFFUSE)
//
// Specular
// SLS2047 - Base (ONLY_SPECULAR)
// SLS2048 - HAIR (ONLY_SPECULAR)
// SLS2049 - PROJ_SHADOW (ONLY_SPECULAR)
// SLS2050 - PROJ_SHADOW, HAIR (ONLY_SPECULAR)
// SLS2051 - POINT (ONLY_SPECULAR)
// SLS2052 - POINT, HAIR (ONLY_SPECULAR)
// SLS2053 - NUM_PT_LIGHTS = 2, POINT (ONLY_SPECULAR)
// SLS2054 - NUM_PT_LIGHTS = 2, POINT, HAIR (ONLY_SPECULAR)
// SLS2055 - NUM_PT_LIGHTS = 3, POINT (ONLY_SPECULAR)
// SLS2056 - NUM_PT_LIGHTS = 3, POINT, HAIR (ONLY_SPECULAR)

#if defined(__INTELLISENSE__)
    #define VS
    #define DIFFUSE
    #define LIGHTS 2
#endif

#if defined(DIFFUSE)
    #define ONLY_LIGHT
    #define OPT
#endif

#if defined(ONLY_SPECULAR)
    #define ONLY_LIGHT
    #define SPECULAR
#endif

#ifdef ONLY_LIGHT
    #define NO_FOG
    #define NO_VERTEX_COLOR
#endif

// The light-only passes take the lamps of the mesh's additive passes (Includes/MergedLights.hlsl).
// They light those in world space, so the vertex shader sends the world normal and tangent in the
// two colour interpolators these passes leave free (no fog, no vertex colour).
#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR) && (!defined(LIGHTS) || LIGHTS < 4)
    #define MERGED_LIGHTS
#endif

// The passes with ambient (no ONLY_LIGHT) send the smooth world normal and tangent too, for the
// reflections of authored materials (Object.hlsl, getReflectionNormal): xyz the normal, w the
// tangent's x, in a free interpolator (TEXCOORD7, or TEXCOORD3 when PROJ_SHADOW takes 7: those
// variants have at most two lights); the tangent's y in lightDistSq.w, plus 4 for a mirrored frame
// (constant across a triangle, so it survives interpolation); its z is upTS.x. The light-only
// passes have the frame in their colour interpolators (MERGED_LIGHTS). The VS and PS of a pair
// agree on ONLY_LIGHT, PROJ_SHADOW and the light count, so both sides pick the same slot.
#if !defined(ONLY_LIGHT) && (!defined(LIGHTS) || LIGHTS < 4)
    #if !defined(PROJ_SHADOW)
        #define WORLD_FRAME_REG TEXCOORD7
    #elif !(LIGHTS > 2 || NUM_PT_LIGHTS > 2)
        #define WORLD_FRAME_REG TEXCOORD3
    #endif
#endif

#include "includes/Helpers.hlsl"
#include "includes/Object.hlsl"
// The camera matrices Shadow.hlsl rebuilds world positions with are moved out of c100-c107 in vertex
// shaders: the game's bone upload goes past Bones[54] (c97) and overwrote them in skinned ones, so actors
// read the sun shadow at the wrong place. c240-c247 is beyond any bone write. (Upstream PR #79.)
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#include "includes/Shadow.hlsl"
#ifdef MERGED_LIGHTS
    #include "includes/MergedLights.hlsl"
#endif
#ifdef PS
    // The merged lamps' loop runs at the pixel shader's register limit: their shadows are looked up
    // first and packed (Includes/PointShadow.hlsl, PointShadowPackMerged below).
    #ifdef MERGED_LIGHTS
        #define POINT_SHADOW_PACKED
    #endif
    #include "includes/PointShadow.hlsl"
#endif

// Forward sun shadows. Enabled at COMPILE TIME via FORWARD_SHADOWS in Includes/Shadow.hlsl,
// deliberately not via a runtime constant -- see the note there.

#ifdef SKIN
    #include "includes/SkinHelpers.hlsl"
#endif

// Toggles.
#define useVertexColor Toggles.x
#define useFog Toggles.y
#define glossPower Toggles.z
#define alphaTestRef Toggles.w

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

#if defined(VS) && LIGHTS < 4

struct VS_OUTPUT {
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0;          // zw: world up in tangent space, xy (viewDir.w: z), see Object.hlsl
    float4 lightDir : TEXCOORD1;

#if LIGHTS > 1 || NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD2;
#endif

#if LIGHTS > 2 || NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD3;
#endif

    float4 viewDir : TEXCOORD6;

    // Object-space squared distances for point-light attenuation (vanillaAttSq), bypassing
    // lightDir/light2Dir/light3Dir above -- those are tangent-space (TBN-transformed) and their
    // length is only correct if the TBN basis is orthonormal. .x = light0 (DIFFUSE/POINT only,
    // where light0 is itself a point light rather than the sun), .y = light2, .z = light3.
    float4 lightDistSq : TEXCOORD5;   // w: the world tangent's y (WORLD_FRAME_REG)

    // TEXCOORD4 is free at LIGHTS < 4. .w carries SHADOW_VS_SENTINEL.
    float4 shadowWorldPos : TEXCOORD4;

#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef MERGED_LIGHTS
    float4 worldNormal : COLOR0;    // xyz the world normal x 0.5 + 0.5, w 1 for a right-handed frame, 0 mirrored
    float4 worldTangent : COLOR1;   // xyz the world tangent x 0.5 + 0.5
#endif
#ifdef WORLD_FRAME_REG
    float4 worldFrame : WORLD_FRAME_REG;   // xyz the world normal, w the world tangent's x
#endif
};

#ifndef NO_FOG
    float3 FogColor : register(c15);
    float4 FogParam : register(c14);
#endif

float4 LightData[10] : register(c25);

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
#endif

float4 EyePosition : register(c16);

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif


VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    // Zeroed unconditionally: the DIFFUSE/POINT (.x) and LIGHTS>1/LIGHTS>2 (.y/.z) blocks below
    // only overwrite the components they actually use, and vs_3_0 requires every component of
    // OUT to be written before return -- a component that stays at this default is simply never
    // read by the PS either (same macro guards on both sides).
    OUT.lightDistSq = 0;

    float4 position = IN.position.xyzw;

    #ifndef SKIN
        float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);

        OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);

        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);

        OUT.sPosition.xyzw = mul(SkinModelViewProj, position.xyzw);
    #endif

    #if defined(DIFFUSE) || defined(POINT)
        float3 light = LightData[0].xyz - position.xyz;
        OUT.lightDistSq.x = dot(light, light);
    #else
        float3 light = LightData[0].xyz;
    #endif

    OUT.lightDir.w = LightData[0].w;
    OUT.lightDir.xyz = mul(tbn, light);

    OUT.viewDir.xyz = mul(tbn, EyePosition.xyz - position.xyz);

    // World up in the normal map's tangent space, through the engine's own frame: the world
    // images of the tangent, binormal and normal, whose z is how much each points up. For the
    // normal-mapped ambient (Object.hlsl, getAmbientNormal), in spare interpolator channels.
    #ifndef SKIN
        #define OBJECT_TO_CLIP ModelViewProj
    #else
        #define OBJECT_TO_CLIP SkinModelViewProj
    #endif
    float3 worldTangent = normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[0], 0.0f))));
    float3 worldBinormal = normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[1], 0.0f))));
    float3 worldNormal = normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[2], 0.0f))));
    float3 upTS = float3(worldTangent.z, worldBinormal.z, worldNormal.z);
    OUT.uv = float4(IN.uv.xy, upTS.xy);
    OUT.viewDir.w = upTS.z;
    #ifdef MERGED_LIGHTS
        OUT.worldNormal = float4(worldNormal * 0.5f + 0.5f, dot(cross(worldNormal, worldTangent), worldBinormal) >= 0.0f ? 1.0f : 0.0f);
        OUT.worldTangent = float4(worldTangent * 0.5f + 0.5f, 0.0f);
    #endif
    #ifdef WORLD_FRAME_REG
        OUT.worldFrame = float4(worldNormal, worldTangent.x);
        OUT.lightDistSq.w = worldTangent.y + (dot(cross(worldNormal, worldTangent), worldBinormal) >= 0.0f ? 0.0f : 4.0f);
    #endif

    #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
        light = LightData[1].xyz - position.xyz;
        OUT.light2Dir.w = LightData[1].w;
        OUT.light2Dir.xyz = mul(tbn, light);
        OUT.lightDistSq.y = dot(light, light);
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        light = LightData[2].xyz - position.xyz;
        OUT.light3Dir.w = LightData[2].w;
        OUT.light3Dir.xyz = mul(tbn, light);
        OUT.lightDistSq.z = dot(light, light);
    #endif

    #ifndef NO_VERTEX_COLOR
        OUT.vertexColor = clamp(IN.vertexColor, 0.0f, 1.0f);
    #endif

    #ifndef NO_FOG
        float3 fogPos = OUT.sPosition.xyz;

        #ifdef REVERSED_DEPTH
            fogPos.z = OUT.sPosition.w - fogPos.z;
        #endif

        float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
        fogStrength = log2(fogStrength);
        OUT.fogColor.a = exp2(fogStrength * FogParam.z);
        OUT.fogColor.rgb = FogColor.rgb;
    #endif

    #ifdef PROJ_SHADOW
        float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    // Model-space shaders: recover a camera-relative world position from the clip position.
    // Written unconditionally, or the interpolator is left undefined.
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.sPosition), SHADOW_VS_SENTINEL);

    return OUT;
};

#elif defined(VS) && LIGHTS >= 4

#if LIGHTS > 4
    #define MAX_LIGHTS 6
#elif LIGHTS > 3 && !defined(SPECULAR)
    #define MAX_LIGHTS 4
#else
    #define MAX_LIGHTS 3
#endif

// Camera-relative world position carrier. At MAX_LIGHTS 6 every TEXCOORD is taken, so it
// rides in light4/5/6's .w -- a light radius the PS never reads, since attenuation uses
// PSLightPosition[i].w. Below that, a free interpolator.
#if MAX_LIGHTS > 4
    // No fourth channel spare here, so the sentinel rides in lPosition.w. The vertex shader
    // normally puts LightData[0].w there and this pixel shader never reads it.
    #define SHADOW_WP_STORE(O, v) O.light4.w = (v).x; O.light5.w = (v).y; O.light6.w = (v).z; O.lPosition.w = SHADOW_VS_SENTINEL
    #define SHADOW_WP_LOAD(I)     float3((I).light4.w, (I).light5.w, (I).light6.w)
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).lPosition.w)
#else
    #define SHADOW_WP_DEDICATED
    #define SHADOW_WP_STORE(O, v) O.shadowWorldPos = float4((v), SHADOW_VS_SENTINEL)
    #define SHADOW_WP_LOAD(I)     (I).shadowWorldPos.xyz
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).shadowWorldPos.w)
#endif

struct VS_OUTPUT {
    float4 vertexColor : COLOR0;
    float4 fogColor : COLOR1;
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0;          // zw: world up in tangent space, xy; z in lPosition.w below MAX_LIGHTS 6
    float4 lPosition : TEXCOORD1;
    float4 lightDir : TEXCOORD2;  // .w = .x of viewDir
    float4 light2 : TEXCOORD3;   // .w = .y of viewDir
    float4 light3 : TEXCOORD4;  // .w = .z of viewDir
#if MAX_LIGHTS > 3
    float4 light4 : TEXCOORD5;
#endif
#if MAX_LIGHTS > 4
    float4 light5 : TEXCOORD6;
    float4 light6 : TEXCOORD7;
#endif
#ifdef SHADOW_WP_DEDICATED
    #if MAX_LIGHTS > 3
        float4 shadowWorldPos : TEXCOORD6;
    #else
        float4 shadowWorldPos : TEXCOORD5;
    #endif
#endif
};

float3 FogColor : register(c15);
float4 FogParam : register(c14);
float4 LightData[10] : register(c25);
#ifndef SKIN
row_major float4x4 ModelViewProj : register(c0);
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
#endif
float4 EyePosition : register(c16);
#ifndef OPT
    float4 fvars0 : register(c17);

    #define lightOffset 0
#else
    #define lightOffset 1
#endif

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = IN.position.xyzw;

    #ifndef SKIN
    float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);

    OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);

        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
        OUT.sPosition.xyzw = mul(SkinModelViewProj, position.xyzw);
    #endif

    float3 viewDir = mul(tbn, EyePosition.xyz - position.xyz);

    OUT.lPosition.xyz = position.xyz;

    // World up in tangent space, see the LIGHTS < 4 variant. lPosition.w used to carry
    // LightData[0].w, the specular distance fade, which the pixel shader now gets per draw instead
    // (ObjectMaterial.y); at MAX_LIGHTS 6 it holds the world position sentinel and z is rebuilt.
    #ifndef SKIN
        #define OBJECT_TO_CLIP ModelViewProj
    #else
        #define OBJECT_TO_CLIP SkinModelViewProj
    #endif
    float3 upTS = float3(normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[0], 0.0f)))).z,
                         normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[1], 0.0f)))).z,
                         normalize(GetShadowWorldDir(mul(OBJECT_TO_CLIP, float4(tbn[2], 0.0f)))).z);
    OUT.uv = float4(IN.uv.xy, upTS.xy);
    OUT.lPosition.w = upTS.z;

    #ifndef OPT
        float lights = min(MAX_LIGHTS, fvars0.z);
    #elif defined(SPECULAR)
        float lights = min(MAX_LIGHTS, EyePosition.w);
    #else
        float lights = min(MAX_LIGHTS, LightData[0].w);
    #endif
    float lightsFrac = frac(lights);
    float lightsThreshold = (lights < 0.0 ? (-lightsFrac < lightsFrac ? 1.0 : 0.0) : 0) + (lights - lightsFrac);
    float lightUsed;

    #ifndef OPT
        OUT.lightDir.w = viewDir.x;
        OUT.lightDir.xyz = mul(tbn, LightData[0].xyz);
    #else
        lightUsed = 0 < lightsThreshold ? 1.0 : 0.0;
        OUT.lightDir.xyz = lightUsed * mul(tbn, LightData[lightOffset + 0].xyz - position.xyz);
        OUT.lightDir.w = viewDir.x;
    #endif

    lightUsed = 1 < lightsThreshold ? 1.0 : 0.0;
    OUT.light2.xyz = lightUsed * mul(tbn, LightData[lightOffset + 1].xyz - position.xyz);
    OUT.light2.w = viewDir.y;

    lightUsed = 2 < lightsThreshold ? 1.0 : 0.0;
    OUT.light3.xyz = lightUsed * mul(tbn, LightData[lightOffset + 2].xyz - position.xyz);
    OUT.light3.w = viewDir.z;

    #if MAX_LIGHTS > 3
        lightUsed = 3 < lightsThreshold ? 1.0 : 0.0;
        OUT.light4.xyz = lightUsed * mul(tbn, LightData[lightOffset + 3].xyz - position.xyz);
        OUT.light4.w = lightUsed * LightData[lightOffset + 3].w;
    #endif

    #if MAX_LIGHTS > 4
        lightUsed = 4 < lightsThreshold ? 1.0 : 0.0;
        OUT.light5.xyz = lightUsed * mul(tbn, LightData[lightOffset + 4].xyz - position.xyz);
        OUT.light5.w = lightUsed * LightData[lightOffset + 4].w;

        lightUsed = 5 < lightsThreshold ? 1.0 : 0.0;
        OUT.light6.xyz = lightUsed * mul(tbn, LightData[lightOffset + 5].xyz - position.xyz);
        OUT.light6.w = lightUsed * LightData[lightOffset + 5].w;
    #endif

    OUT.vertexColor = clamp(IN.vertexColor, 0.0f, 1.0f);

    float3 fogPos = OUT.sPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = OUT.sPosition.w - fogPos.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    fogStrength = log2(fogStrength);
    OUT.fogColor.a = exp2(fogStrength * FogParam.z);
    OUT.fogColor.rgb = FogColor.rgb;

    // Through SHADOW_WP_STORE: at MAX_LIGHTS 6 this overwrites light4/5/6's .w, which the PS
    // ignores.
    float3 shadowWorldPos = GetShadowWorldPos(OUT.sPosition);
    SHADOW_WP_STORE(OUT, shadowWorldPos);

    return OUT;
};
#endif // Vertex shaders.

#if defined(PS) && (!defined(LIGHTS) || LIGHTS < 4)

struct PS_INPUT {
    float2 vpos : VPOS;   // the pixel's screen position (Includes/PointShadow.hlsl, POINT_SHADOW_PIXEL)
#ifndef NO_VERTEX_COLOR
    float3 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 uv : TEXCOORD0;
    float4 lightDir : TEXCOORD1_centroid;
#if LIGHTS > 1 || NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD2_centroid;
#endif
#if LIGHTS > 2 || NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD3_centroid;
#endif
    float4 viewDir : TEXCOORD6_centroid;   // w: world up in tangent space, z
    float4 lightDistSq : TEXCOORD5;
    float4 shadowWorldPos : TEXCOORD4;
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef MERGED_LIGHTS
    float4 worldNormal : COLOR0;
    float4 worldTangent : COLOR1;
#endif
#ifdef WORLD_FRAME_REG
    float4 worldFrame : WORLD_FRAME_REG;
#endif
};

#ifdef MERGED_LIGHTS
// The merged lamps, as the additive DIFFUSE passes would have lit them (getPointLightLightingAtt,
// vanilla's attenuation), in world space: the normal map's normal through the world frame the
// vertex shader sent.
// The merged lamps' shadows, value i for lamp i, first thing in main (POINT_SHADOW_PACKED above).
// normal: the smooth world normal the loop below lights with.
void PointShadowPackMerged(float3 worldPos, float3 normal, float valid) {
    pointShadowPackA = 0.0f;
    pointShadowPackB = 0.0f;
    [branch] if (TESR_PointShadowData.x > 0.0f && valid > 0.0f) {
        [loop] for (int i = 0; i < MERGED_MAX_LIGHTS; i++) {
            if (i >= TESR_MergedLightCount.x) break;
            // A lamp whose light does not reach the pixel (getMergedPointLights' own test) needs no shadow.
            float3 L = TESR_MergedLightPosition[i].xyz - worldPos;
            [branch] if (vanillaAttSq(dot(L, L), TESR_MergedLightPosition[i].w) > 0.0f)
                PointShadowPackValue(i, PointShadowVisibilitySoft(TESR_MergedLightColor[i].w, TESR_PointShadowMerged[i], worldPos, normal, valid));
        }
    }
}

float3 getMergedPointLights(float4 encodedNormal, float4 encodedTangent, float3 normalTS, float3 worldPos, float worldPosValid, float3 albedo, float roughness) {
    float3 total = 0.0f;
    [branch] if (TESR_MergedLightCount.x > 0.0f) {
        float3 N = normalize(encodedNormal.xyz * 2.0f - 1.0f);
        float3 T = encodedTangent.xyz * 2.0f - 1.0f;
        T = normalize(T - N * dot(T, N));
        float3 B = cross(N, T) * (encodedNormal.w > 0.5f ? 1.0f : -1.0f);
        float3 n = normalize(normalTS.x * T + normalTS.y * B + normalTS.z * N);
        float3 V = -worldPos;

        [loop] for (int i = 0; i < MERGED_MAX_LIGHTS; i++) {
            if (i >= TESR_MergedLightCount.x) break;
            // Its shadow, looked up at the top of main (PointShadowPackMerged). A lamp wholly
            // shadowed here is not lit at all.
            float visibility = PointShadowPackedVisibility(i);
            float3 L = TESR_MergedLightPosition[i].xyz - worldPos;
            float att = vanillaAttSq(dot(L, L), TESR_MergedLightPosition[i].w);
            [branch] if (att > 0.0f && visibility > 0.0f)
                total += getPointLightLightingAtt(L, att, TESR_MergedLightColor[i].rgb, V, n, albedo, roughness, length(L), visibility);
        }
    }
    return total;
}
#endif

struct PS_OUTPUT {
    float4 color : COLOR0;
};

#if defined(DIFFUSE) || defined(ONLY_SPECULAR)
    sampler2D NormalMap : register(s0);
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
#endif

#if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    float4 AmbientColor : register(c1);
#endif

float4 PSLightColor[10] : register(c3);

#if (defined(SI) || defined(HAIR)) && !defined(ONLY_SPECULAR)
    #ifdef ONLY_LIGHT
        sampler2D GlowMap : register(s3);
    #else
        sampler2D GlowMap : register(s4);
    #endif
    float4 EmittanceColor : register(c2);
#endif

#ifdef PROJ_SHADOW
    #if defined(ONLY_SPECULAR)
        sampler2D ShadowMap : register(s4);
        sampler2D ShadowMaskMap : register(s5);
    #elif defined(ONLY_LIGHT)
        sampler2D ShadowMap : register(s5);
        sampler2D ShadowMaskMap : register(s6);
    #else
        sampler2D ShadowMap : register(s6);
        sampler2D ShadowMaskMap : register(s7);
    #endif
#endif

#ifndef OPT
    float4 Toggles : register(c27);
#endif


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;
    POINT_SHADOW_PIXEL(IN.vpos);

    #ifdef POINT_SHADOW_PACKED
        // First, while nothing else is held: the merged lamps' shadows, with the normal they light with.
        PointShadowPackMerged(IN.shadowWorldPos.xyz, normalize(IN.worldNormal.xyz * 2.0f - 1.0f), SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
    #endif

    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 baseColor = tex2D(BaseMap, IN.uv.xy);
        float3 adAlbedo = baseColor.rgb;   // see "Light-only passes" in Object.hlsl

        #if defined(ONLY_LIGHT)
            baseColor.rgb = 1;
        #endif
    #else
        float4 baseColor = 1;
    #endif

    #if !defined(OPT) && !defined(ONLY_SPECULAR)
        clip(AmbientColor.a >= 1 ? 0 : (baseColor.a - alphaTestRef));
    #endif

    float4 normal = tex2D(NormalMap, IN.uv.xy);
    normal.xyz = normalize(expand(normal.xyz));

    // Material (Object.hlsl): vanilla's specular mask (the normal map alpha) and glossiness, and
    // the authored _rmaos map where there is one. OPT variants carry no Toggles: the engine's
    // default glossiness of 30. roughness is only the debug view's.
    #if !defined(OPT)
        float shine = glossPower;
    #else
        float shine = 30.0f;
    #endif
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float3 materialAlbedo = adAlbedo;
    #else
        float3 materialAlbedo = 1.0f;
    #endif
    setupMaterial(normal.a, shine, normal.xyz, IN.uv.xy, materialAlbedo);
    float roughness = getMaterialRoughness(shine);
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        setupADCompensation(adAlbedo);
    #endif
    #ifdef MERGED_LIGHTS
        // The highlight passes merged in (Includes/MergedLights.hlsl): only flagged materials have them.
        mergedSpecular = TESR_MergedLightCount.y > 0.0f && ObjectMaterial.x >= 0.5f;
    #endif

    //if (TESR_DebugVar.y > 0.0) {
    //    OUT.color.a = 1;
    //    if (TESR_DebugVar.y > 0.1)
    //        OUT.color.rgb = roughness.xxx;
    //    else
    //        OUT.color.rgb = normal.aaa;
    //    return OUT;
    //}

    #ifndef NO_VERTEX_COLOR
        #if defined(HAIR)
            float4 glow = tex2D(GlowMap, IN.uv.xy);
            baseColor.rgb = (2 * ((IN.vertexColor.g * (EmittanceColor.rgb - 0.5)) + 0.5)) * lerp(baseColor.rgb, glow.rgb, glow.a);
        #elif !defined(OPT)
            baseColor.rgb = useVertexColor <= 0 ? baseColor.rgb : (baseColor.rgb * IN.vertexColor.rgb);
        #else
            baseColor.rgb = baseColor.rgb * IN.vertexColor.rgb;
        #endif
    #endif

    // Linear lighting decodes the albedo here, once; see "Lighting space" in PBR.hlsl.
    baseColor.rgb = decodeColor(baseColor.rgb);

    // Where the vertex shader sent a world position (a vanilla one does not): the lamps' shadows
    // (Includes/PointShadow.hlsl) need it.
    float pointShadowValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;

    // Vanilla shadows.
    float3 shadowMultiplier = 1.0;
    #if defined(STBB)
        shadowMultiplier = 0.85;
    #elif defined(PROJ_SHADOW)
        float3 shadow = tex2D(ShadowMap, IN.shadowUVs.xy).xyz;
        float shadowMask = tex2D(ShadowMaskMap, IN.shadowUVs.zw).x;
        shadowMultiplier = lerp(1, shadow, shadowMask);
    #endif

    // Applied to PSLightColor[0], the sun, only: ambient, emittance and point lights are
    // untouched. ddx/ddy must stay at top level, outside any dynamic branch.
    //
    // Computed once here and reused by the ambient block further down (that block's condition,
    // !DIFFUSE && !ONLY_SPECULAR, is a strict subset of this one, since POINT implies
    // ONLY_SPECULAR) -- two separate ddx/ddy evaluations of the identical
    // GetShadowGeometricNormal(IN.shadowWorldPos.xyz) call previously coexisted in the same
    // pixel shader whenever FORWARD_SHADOWS was compiled in, which is fragile enough on its own
    // to corrupt unrelated interpolator reads placed nearby.
    #if !defined(DIFFUSE) && !defined(POINT)
        float3 shadowGeometricNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        sunShadow = SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
                  ? GetSunShadow(IN.shadowWorldPos.xyz, shadowGeometricNormal)
                  : 1.0f;
        #endif
        // Not folded into shadowMultiplier: that one holds the vanilla GAMMA-space factors and
        // goes inside decodeColor, while this is a real visibility, applied to linear light.

        // Normal-mapped ambient and reflections (Object.hlsl, getAmbientNormal).
        float shadowVSValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        float3 ambientNormal = getAmbientNormal(normal.xyz, float3(IN.uv.zw, IN.viewDir.w), shadowGeometricNormal, shadowVSValid);

        // Reflections want the real normal-mapped world normal; the ambient normal's heading is
        // the flat triangle's, which a detailed environment shows as facets.
        float3 reflectionGeometricNormal = shadowGeometricNormal;
        float3 reflectionNormal = ambientNormal;
        #if defined(WORLD_FRAME_REG)
            [branch] if (shadowVSValid > 0.0f) {
                float tangentY = IN.lightDistSq.w;
                float mirrored = tangentY > 2.0f ? 1.0f : 0.0f;
                reflectionGeometricNormal = normalize(IN.worldFrame.xyz);
                reflectionNormal = getReflectionNormal(reflectionGeometricNormal, float3(IN.worldFrame.w, tangentY - 4.0f * mirrored, IN.uv.z), mirrored, normal.xyz);
            }
        #elif defined(MERGED_LIGHTS)
            [branch] if (shadowVSValid > 0.0f) {
                reflectionGeometricNormal = normalize(IN.worldNormal.xyz * 2.0f - 1.0f);
                reflectionNormal = getReflectionNormal(reflectionGeometricNormal, IN.worldTangent.xyz * 2.0f - 1.0f, IN.worldNormal.w > 0.5f ? 0.0f : 1.0f, normal.xyz);
            }
        #endif
    #endif

    // The lamps' shadows' normal offset (Includes/PointShadow.hlsl): the geometric normal, once.
    // ddx/ddy, so at top level.
    #if !defined(DIFFUSE) && !defined(POINT)
        float3 pointShadowNormal = shadowGeometricNormal;
    #else
        float3 pointShadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
    #endif

    #if !defined(DIFFUSE) && !defined(POINT)
        float3 lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sunShadow);
    #else
        // Pointlights only. Attenuate from the object-space lightDistSq.x carried from the VS,
        // not length(IN.lightDir.xyz) -- that vector is tangent-space (TBN-transformed) and its
        // length is only correct if the TBN basis is orthonormal.
        float att0 = vanillaAttSq(IN.lightDistSq.x, IN.lightDir.w);
        float3 lighting = getPointLightLightingAtt(IN.lightDir.xyz, att0, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sqrt(IN.lightDistSq.x),
            PointShadowBase(0, IN.shadowWorldPos.xyz, pointShadowNormal, pointShadowValid));
    #endif

    // Self emmitance.
    #ifdef SI
        float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
        lighting += baseColor.rgb * decodeColor(glow.rgb * EmittanceColor.rgb);
    #endif

    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        // Reuses shadowGeometricNormal computed above -- see the comment there. Always in scope
        // here: POINT implies ONLY_SPECULAR, so !ONLY_SPECULAR implies !POINT.
        // Reflection first: it sets the share of the ambient it takes (skyReflectedFraction).
        lighting += getObjectSkyReflection(IN.shadowWorldPos.xyz, reflectionGeometricNormal, reflectionNormal, roughness, shadowVSValid);
        lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, ambientNormal, shadowVSValid);
    #endif

    // Other light sources. Same object-space attenuation fix as light0 above. Each with its own
    // shadow (Includes/PointShadow.hlsl): the pass's light j is PSLightColor[j].
    #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
        float att2 = vanillaAttSq(IN.lightDistSq.y, IN.light2Dir.w);
        lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sqrt(IN.lightDistSq.y),
            PointShadowBase(1, IN.shadowWorldPos.xyz, pointShadowNormal, pointShadowValid));
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        float att3 = vanillaAttSq(IN.lightDistSq.z, IN.light3Dir.w);
        lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sqrt(IN.lightDistSq.z),
            PointShadowBase(2, IN.shadowWorldPos.xyz, pointShadowNormal, pointShadowValid));
    #endif

    // The lamps of this mesh's additive passes (MERGED_LIGHTS above); those passes are muted.
    #ifdef MERGED_LIGHTS
        lighting += getMergedPointLights(IN.worldNormal, IN.worldTangent, normal.xyz, IN.shadowWorldPos.xyz, pointShadowValid, baseColor.rgb, roughness);
    #endif

    float3 finalColor = encodeColor(lighting.rgb);   // back to the game's gamma space, before fog

    // Fog.
    #ifndef NO_FOG
        #ifndef OPT
            finalColor.rgb = (useFog <= 0.0 ? finalColor.rgb : lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a));
        #else
            finalColor.rgb = lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a);
        #endif
    #endif


    #if !defined(DIFFUSE) && !defined(POINT) && !defined(ONLY_SPECULAR)
        [branch] if (TESR_PBRDebugData.x > 0.0f)
            finalColor.rgb = getMaterialDebug(TESR_PBRDebugData.x, roughness, ambientNormal);
    #endif

#if SHADOW_FORCE_MARKER
    finalColor.rgb = float3(1.0f, 0.0f, 1.0f);   // unconditional: proves this shader ran
#endif

    OUT.color.rgb = finalColor.rgb;

    #if defined(DIFFUSE)
        OUT.color.a = 1;
    #elif defined(ONLY_SPECULAR)
        #if !defined(DIFFUSE) && !defined(POINT)
            // Alpha here is a blend WEIGHT (this pass's own brightness, used to fade
            // its additive specular/sheen layer smoothly), not a darkness value --
            // unlike the colour output, it must not shrink with sun shadow, or the
            // whole layer fades toward invisible in shadow instead of just going
            // dark. That read as "alpha blending broken" under Forward Shadows
            // specifically, because sunShadow is the one new factor it added to
            // shadowMultiplier here (STBB/PROJ_SHADOW's own contribution predates
            // this and is left alone). Dividing it back out recovers the weight
            // this pass would have produced without Forward Shadows; harmless and
            // exact when Forward Shadows is compiled out, since sunShadow is then
            // fixed at 1.0.
            // Under linear lighting the encoded output scales as sunShadow ^ (1 / lightingGamma).
            OUT.color.a = weight(finalColor.rgb) / max(linearLighting ? pow(max(sunShadow, 0.0f), 1.0f / lightingGamma) : sunShadow, 0.05f);
        #else
            OUT.color.a = weight(finalColor.rgb);
        #endif
    #elif defined(ONLY_LIGHT)
        OUT.color.a = baseColor.a;
    #else
        OUT.color.a = baseColor.a * AmbientColor.a;
    #endif

    return OUT;
}

#elif defined(PS) && LIGHTS >= 4

#if LIGHTS > 4
    #define MAX_LIGHTS 6
#elif LIGHTS > 3 && !defined(SPECULAR)
    #define MAX_LIGHTS 4
#else
    #define MAX_LIGHTS 3
#endif

// Camera-relative world position carrier. At MAX_LIGHTS 6 every TEXCOORD is taken, so it
// rides in light4/5/6's .w -- a light radius the PS never reads, since attenuation uses
// PSLightPosition[i].w. Below that, a free interpolator.
#if MAX_LIGHTS > 4
    // No fourth channel spare here, so the sentinel rides in lPosition.w. The vertex shader
    // normally puts LightData[0].w there and this pixel shader never reads it.
    #define SHADOW_WP_STORE(O, v) O.light4.w = (v).x; O.light5.w = (v).y; O.light6.w = (v).z; O.lPosition.w = SHADOW_VS_SENTINEL
    #define SHADOW_WP_LOAD(I)     float3((I).light4.w, (I).light5.w, (I).light6.w)
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).lPosition.w)
#else
    #define SHADOW_WP_DEDICATED
    #define SHADOW_WP_STORE(O, v) O.shadowWorldPos = float4((v), SHADOW_VS_SENTINEL)
    #define SHADOW_WP_LOAD(I)     (I).shadowWorldPos.xyz
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).shadowWorldPos.w)
#endif

struct PS_INPUT {
    float2 vpos : VPOS;   // the pixel's screen position (Includes/PointShadow.hlsl, POINT_SHADOW_PIXEL)
    float4 vertexColor : COLOR0;
    float4 fogColor : COLOR1;
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0;
    float4 lPosition : TEXCOORD1;
    float4 lightDir : TEXCOORD2_centroid;  // .w = .x of viewDir
    float4 light2 : TEXCOORD3_centroid; // .w = .y of viewDir
    float4 light3 : TEXCOORD4_centroid; // .w = .z of viewDir
#if MAX_LIGHTS > 3
    float4 light4 : TEXCOORD5_centroid;
#endif
#if MAX_LIGHTS > 4
    float4 light5 : TEXCOORD6_centroid;
    float4 light6 : TEXCOORD7_centroid;
#endif
#ifdef SHADOW_WP_DEDICATED
    #if MAX_LIGHTS > 3
        float4 shadowWorldPos : TEXCOORD6;
    #else
        float4 shadowWorldPos : TEXCOORD5;
    #endif
#endif
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

sampler2D BaseMap : register(s0);
sampler2D NormalMap : register(s1);

float4 AmbientColor : register(c1);

float4 PSLightColor[10] : register(c3);
float4 PSLightPosition[8] : register(c19);


#ifndef OPT
    float4 EmittanceColor : register(c2);

    float4 Toggles : register(c27);

    #define lightsUsed EmittanceColor.a
    #define lightOffset 0
    #define glossPow Toggles.z
#else
    #define lightsUsed PSLightColor[0].a
    #define lightOffset 1
    #define glossPow PSLightColor[1].w
#endif

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;
    POINT_SHADOW_PIXEL(IN.vpos);

    float4 baseColor = tex2D(BaseMap, IN.uv.xy);

    #ifndef OPT
        clip(AmbientColor.a >= 1 ? 0 : (baseColor.a - alphaTestRef));
    #endif

    #ifndef OPT
        baseColor.rgb = useVertexColor <= 0 ? baseColor.rgb : (baseColor.rgb * IN.vertexColor.rgb);
    #else
        baseColor.rgb = baseColor.rgb * IN.vertexColor.rgb;
    #endif

    float4 normal = tex2D(NormalMap, IN.uv.xy);
    normal.xyz = normalize(expand(normal.xyz));

    // Material, see the LIGHTS < 4 variant. It had no specular AA here before. The specular OPT
    // variants have no Toggles but get the glossiness in PSLightColor[1].w (glossPow above:
    // ShadowLightShader::SetupGeometryOpt_LightsSpecular writes m_fShine there); the other OPT
    // variants get the light count there instead, so they take the engine's default of 30.
    #if !defined(OPT)
        float shine = glossPower;
    #elif defined(SPECULAR)
        float shine = glossPow;
    #else
        float shine = 30.0f;
    #endif
    setupMaterial(normal.a, shine, normal.xyz, IN.uv.xy, baseColor.rgb);
    float roughness = getMaterialRoughness(shine);
    baseColor.rgb = decodeColor(baseColor.rgb);

    // Lighting.
    float3 viewDir = { IN.lightDir.w, IN.light2.w, IN.light3.w };

    float att;

    // Each lamp's shadow (Includes/PointShadow.hlsl): PSLightColor[k] is shadowed as light k of the
    // per-draw lamp list (ForwardPointShadows in Hooks/Shaders.cpp maps the OPT passes, whose
    // PSLightColor[k] is the game's pass light k + 1).
    float3 pointShadowPos = SHADOW_WP_LOAD(IN);
    float pointShadowValid = SHADOW_WP_VALID(IN) ? 1.0f : 0.0f;
    // The geometric normal, once, for the sun's shadow, the lamps' (their normal offset) and the
    // ambient below. ddx/ddy, so at top level.
    float3 pointShadowNormal = GetShadowGeometricNormal(pointShadowPos);

    // Forward sun shadows -- see the LIGHTS < 4 variant. Only the OPT-off path has a sun
    // term; with OPT the first slot is a point light and must not be shadowed by the sun.
    #ifndef OPT
        float3 sunShadowWorldPos = pointShadowPos;
        float3 sunShadowNormal = pointShadowNormal;
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        sunShadow = SHADOW_WP_VALID(IN)
                  ? GetSunShadow(sunShadowWorldPos, sunShadowNormal)
                  : 1.0f;
        #endif
        float3 lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sunShadow);
    #else
        att = vanillaAtt(PSLightPosition[0].xyz - IN.lPosition.xyz, PSLightPosition[0].w);
        float3 lighting = getPointLightLightingAtt(IN.lightDir.xyz, att, PSLightColor[0].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[0].xyz - IN.lPosition.xyz),
            PointShadowBase(0, pointShadowPos, pointShadowNormal, pointShadowValid));
    #endif

    // Slot k (PSLightColor[k]) holds a light only while k < lightsUsed. The game uploads just the
    // registers of the lights the pass has (ShadowLightShader::SetupGeometryOpt_Lights /
    // _LightsSpecular, 0xB7CB00: m_uiRegisterCount = ucNumLights - 1), so a slot past them keeps
    // whatever an earlier draw left there. Slots 2-5 used to test k > lightsUsed, which also lit the
    // first slot past the pass's lights: a stale lamp from another mesh, so lights and highlights
    // jumped as a mesh's lamp count changed. In the OPT variants every position is one slot up
    // (lightOffset), lamps 5 and 6 included.
    att = vanillaAtt(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 0].w);
    [branch] if (1 < lightsUsed) lighting += getPointLightLightingAtt(IN.light2.xyz, att, PSLightColor[1].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[lightOffset + 0].xyz - IN.lPosition.xyz),
        PointShadowBase(1, pointShadowPos, pointShadowNormal, pointShadowValid));

    att = vanillaAtt(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 1].w);
    [branch] if (2 < lightsUsed) lighting += getPointLightLightingAtt(IN.light3.xyz, att, PSLightColor[2].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[lightOffset + 1].xyz - IN.lPosition.xyz),
        PointShadowBase(2, pointShadowPos, pointShadowNormal, pointShadowValid));

    #if MAX_LIGHTS > 3
        att = vanillaAtt(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 2].w);
        [branch] if (3 < lightsUsed) lighting += getPointLightLightingAtt(IN.light4.xyz, att, PSLightColor[3].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[lightOffset + 2].xyz - IN.lPosition.xyz),
            PointShadowBase(3, pointShadowPos, pointShadowNormal, pointShadowValid));
    #endif

    #if MAX_LIGHTS > 4
        att = vanillaAtt(PSLightPosition[lightOffset + 3].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 3].w);
        [branch] if (4 < lightsUsed) lighting += getPointLightLightingAtt(IN.light5.xyz, att, PSLightColor[4].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[lightOffset + 3].xyz - IN.lPosition.xyz),
            PointShadowBase(4, pointShadowPos, pointShadowNormal, pointShadowValid));

        att = vanillaAtt(PSLightPosition[lightOffset + 4].xyz - IN.lPosition.xyz, PSLightPosition[lightOffset + 4].w);
        [branch] if (5 < lightsUsed) lighting += getPointLightLightingAtt(IN.light6.xyz, att, PSLightColor[5].rgb, viewDir.xyz, normal.xyz, baseColor.rgb, roughness, length(PSLightPosition[lightOffset + 4].xyz - IN.lPosition.xyz),
            PointShadowBase(5, pointShadowPos, pointShadowNormal, pointShadowValid));
    #endif

    // ddx/ddy must stay at pixel-shader top level.
    float3 ambNormal = pointShadowNormal;
    float shadowVSValid = SHADOW_WP_VALID(IN) ? 1.0f : 0.0f;

    // Normal-mapped ambient, see the LIGHTS < 4 variant. At MAX_LIGHTS 6 up's z has no channel
    // and is rebuilt.
    #if MAX_LIGHTS > 4
        float3 upTS = rebuildUpTS(IN.uv.zw, ambNormal);
    #else
        float3 upTS = float3(IN.uv.zw, IN.lPosition.w);
    #endif
    float3 ambientNormal = getAmbientNormal(normal.xyz, upTS, ambNormal, shadowVSValid);

    // Reflection first: it sets the share of the ambient it takes (skyReflectedFraction).
    lighting += getObjectSkyReflection(SHADOW_WP_LOAD(IN), ambNormal, ambientNormal, roughness, shadowVSValid);
    lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, ambientNormal, shadowVSValid);

    // Vanilla attenuates the full specular term by LightData[0].w (IN.lPosition.w): the engine's specular
    // distance fade. It arrives per draw as ObjectMaterial.y instead (setupMaterial), since lPosition.w
    // carries the world position sentinel here.
    float3 finalColor = encodeColor(lighting);   // back to the game's gamma space, before fog

    [branch] if (TESR_PBRDebugData.x > 0.0f)
        finalColor = getMaterialDebug(TESR_PBRDebugData.x, roughness, ambientNormal);

    #ifndef OPT
        finalColor.rgb = (useFog <= 0.0 ? finalColor.rgb : lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a));
    #else
        finalColor.rgb = lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a);
    #endif


#if SHADOW_FORCE_MARKER
    finalColor.rgb = float3(1.0f, 0.0f, 1.0f);   // unconditional: proves this shader ran
#endif

    OUT.color.rgb = finalColor.rgb;
    OUT.color.a = baseColor.a * AmbientColor.a;

    return OUT;
}

#endif // Pixel shaders.
