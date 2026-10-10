// Template for PAR parallax shaders.
//
// VS
//
// AD
// PAR2008 - LIGHTS = 2, PARALLAX (AD)
// PAR2009 - LIGHTS = 2, PARALLAX, PROJ_SHADOW (AD)
// PAR2010 - LIGHTS = 3, PARALLAX (AD)
// PAR2011 - LIGHTS = 3, PARALLAX, PROJ_SHADOW (AD)
//
// ADTS
// PAR2000 - PARALLAX
// PAR2001 - PARALLAX, PROJ_SHADOW
// PAR2002 - PARALLAX, LIGHTS = 2
// PAR2003 - PARALLAX, LIGHTS = 2, PROJ_SHADOW
// PAR2004 - PARALLAX, SPECULAR
// PAR2005 - PARALLAX, SPECULAR, PROJ_SHADOW
// PAR2006 - PARALLAX, SPECULAR, LIGHTS = 2
// PAR2007 - PARALLAX, SPECULAR, LIGHTS = 2, PROJ_SHADOW
//
// Diffuse
// PAR2012 - LIGHTS = 2, PARALLAX (NO_FOG, DIFFUSE)
// PAR2013 - LIGHTS = 3, PARALLAX (NO_FOG, DIFFUSE)
//
// Specular
// PAR2015 - PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2016 - PROJ_SHADOW, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2017 - POINT, PARALLAX (NO_FOG. NO_VERTEX_COLOR, SPECULAR)
// PAR2018 - POINT, NUM_PT_LIGHTS = 2, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2019 - POINT, NUM_PT_LIGHTS = 3, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
//
// Texture
// PAR2014 - PARALLAX (NO_LIGHT, NO_FOG)
//
// PS
//
// AD
// PAR2013 - LIGHTS = 2, PARALLAX (AD)
// PAR2014 - LIGHTS = 2, PARALLAX, SI (AD)
// PAR2015 - LIGHTS = 2, PARALLAX, PROJ_SHADOW (AD)
// PAR2016 - LIGHTS = 2, PARALLAX, SI, PROJ_SHADOW (AD)
// PAR2017 - LIGHTS = 3, PARALLAX (AD)
// PAR2018 - LIGHTS = 3, PARALLAX, SI (AD)
// PAR2019 - LIGHTS = 3, PARALLAX, PROJ_SHADOW (AD)
// PAR2020 - LIGHTS = 3, PARALLAX, SI, PROJ_SHADOW (AD)
//
// ADTS
// PAR2000 - PARALLAX
// PAR2001 - PARALLAX, OPT
// PAR2002 - PARALLAX, SI
// PAR2003 - PARALLAX, PROJ_SHADOW
// PAR2004 - PARALLAX, SI, PROJ_SHADOW
// PAR2005 - PARALLAX, LIGHTS = 2
// PAR2006 - PARALLAX, LIGHTS = 2, SI
// PAR2007 - PARALLAX, LIGHTS = 2, PROJ_SHADOW
// PAR2008 - PARALLAX, LIGHTS = 2, SI, PROJ_SHADOW
// PAR2009 - PARALLAX, SPECULAR
// PAR2010 - PARALLAX, SPECULAR, SI
// PAR2011 - PARALLAX, SPECULAR, PROJ_SHADOW
// PAR2012 - PARALLAX, SPECULAR, SI, PROJ_SHADOW
// PAR2029 - PARALLAX, LIGHTS = 2, SPECULAR
// PAR2030 - PARALLAX, LIGHTS = 2, SPECULAR, SI
// PAR2031 - PARALLAX, LIGHTS = 2, SPECULAR, PROJ_SHADOW
// PAR2032 - PARALLAX, LIGHTS = 2, SPECULAR, SI, PROJ_SHADOW
//
// Diffuse
// PAR2021 - LIGHTS = 2, PARALLAX (NO_FOG, NO_VERTEX_COLOR, DIFFUSE, ONLY_LIGHT, OPT)
// PAR2022 - LIGHTS = 3, PARALLAX (NO_FOG, NO_VERTEX_COLOR, DIFFUSE, ONLY_LIGHT, OPT)
//
// Specular
// PAR2024 - PARALLAX (ONLY_SPECULAR)
// PAR2025 - PARALLAX, PROJ_SHADOW (ONLY_SPECULAR)
// PAR2026 - PARALLAX, POINT (ONLY_SPECULAR)
// PAR2027 - PARALLAX, NUM_PT_LIGHTS = 2, POINT (ONLY_SPECULAR)
// PAR2028 - PARALLAX, NUM_PT_LIGHTS = 3, POINT (ONLY_SPECULAR)
//
// Texture
// PAR2023 - PARALLAX (NO_LIGHT)

#if defined(__INTELLISENSE__)
    #define VS
    #define REVERSED_DEPTH
#endif

#if defined(AD)
    #define ONLY_LIGHT
    #define OPT
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

// The light-only (AD) passes take the lamps and highlights of the mesh's other passes, as in
// ObjectTemplate.hlsl (Includes/MergedLights.hlsl): the world normal and tangent ride in the two
// colour interpolators these passes leave free. With three lamps and the projected shadow nine of
// ps_3_0's ten input registers are already taken, so there the frame is packed (MERGED_PACKED):
// the normal and the tangent's x in COLOR0, the tangent's y and z in the light2Att and light3Att
// .w channels (constant 0.5 in vanilla, rebuilt in the pixel shader) and the handedness in
// lightDir.w (the sun's, unused by the light-only pass).
#if defined(AD)
    #define MERGED_LIGHTS
    #if LIGHTS > 2 && defined(PROJ_SHADOW)
        #define MERGED_PACKED
    #endif
#endif
// The passes with ambient send the smooth world normal and tangent too, for the reflections of
// authored materials (Object.hlsl, getReflectionNormal): the normal and the tangent's x in
// TEXCOORD5 (free there: those variants have at most two lights), the tangent's y in lightDir.w
// (the sun's, which these pixel shaders never read) plus 4 for a mirrored frame (constant across
// a triangle, so it survives interpolation), its z in uv.z (upTS.x). One layout for every such
// variant, so a vertex shader with more lights than its pixel shader still agrees with it. The
// AD passes have the frame already (MERGED_LIGHTS).
#if !defined(ONLY_LIGHT) && !defined(AD) && !defined(DIFFUSE) && !defined(NO_LIGHT) && !(LIGHTS > 2)
    #define WORLD_FRAME
#endif

#ifdef MERGED_PACKED
    #define ATTENUATION_UV2(a) float2((a).z, 0.5f)
#else
    #define ATTENUATION_UV2(a) (a).zw
#endif

#include "includes/Helpers.hlsl"
#include "includes/Parallax.hlsl"
#include "includes/Object.hlsl"
// The camera matrices Shadow.hlsl rebuilds world positions with are moved out of c100-c107 in vertex
// shaders: the game's bone upload goes past Bones[54] (c97) and overwrote them in skinned ones, so actors
// read the sun shadow at the wrong place. c240-c247 is beyond any bone write. (Upstream PR #79.) Not skinned,
// but kept with the object and skin shaders: MergedLights writes these registers for first person.
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#include "includes/Shadow.hlsl"
#ifdef MERGED_LIGHTS
    #include "includes/MergedLights.hlsl"
#endif
#ifdef PS
    // The merged lamps' shadows are looked up first and packed (Includes/PointShadow.hlsl,
    // PointShadowPackMerged below): their loop has no room for the full filter.
    #ifdef MERGED_LIGHTS
        #define POINT_SHADOW_PACKED
    #endif
    #include "includes/PointShadow.hlsl"
#endif

// Forward sun shadows -- see ObjectTemplate.hlsl. PAR shaders do full sun lighting but had
// no shadow term at all, so every parallax-material object rendered fully sunlit once the
// deferred sun was switched off.

struct VS_INPUT
{
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
#ifndef NO_VERTEX_COLOR
    float4 vertex_color : COLOR0;
#endif
};

struct VS_OUTPUT
{
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0;          // zw: world up in tangent space, xy (Object.hlsl, getAmbientNormal)
#ifndef NO_LIGHT
    float4 lightDir : TEXCOORD1;
    #ifdef DIFFUSE
        float4 lightAtt : TEXCOORD2;
    #endif
#endif
#if LIGHTS > 1
    float4 light2Dir : TEXCOORD3;
    float4 light2Att : TEXCOORD4;
#endif
#if LIGHTS > 2  // Only used for AD and diffuse, no need for specular check.
    float4 light3Dir : TEXCOORD5;
    float4 light3Att : TEXCOORD6;
#endif
#if NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD3;
#endif
#if NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD5;
#endif
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD8;
#endif
    float4 viewDir : TEXCOORD7;

    // TEXCOORD0-8 are taken by this template; 9 is the first free slot.
    float4 shadowWorldPos : TEXCOORD9;
#ifdef MERGED_PACKED
    float4 worldNormal : COLOR0;    // xyz the world normal x 0.5 + 0.5, w the world tangent's x x 0.5 + 0.5
#elif defined(MERGED_LIGHTS)
    float4 worldNormal : COLOR0;    // xyz the world normal x 0.5 + 0.5, w 1 for a right-handed frame, 0 mirrored
    float4 worldTangent : COLOR1;   // xyz the world tangent x 0.5 + 0.5
#endif
#ifdef WORLD_FRAME
    float4 worldFrameNormal : TEXCOORD5;    // xyz the world normal, w the world tangent's x
#endif
};

#ifdef VS

float4 EyePosition : register(c16);
row_major float4x4 ModelViewProj : register(c0);

#ifndef NO_LIGHT
    float4 LightData[10] : register(c25);
#endif

#ifndef NO_FOG
    float3 FogColor : register(c15);
    float4 FogParam : register(c14);
#endif

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;

    float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);

    OUT.sPosition.xyzw = mul(ModelViewProj, IN.position.xyzw);

    // World up in the normal map's tangent space through the engine's own frame, for the
    // normal-mapped ambient; see ObjectTemplate.hlsl. No channel is spare for z: rebuilt in the PS.
    float3 worldTangent = normalize(GetShadowWorldDir(mul(ModelViewProj, float4(tbn[0], 0.0f))));
    float3 worldBinormal = normalize(GetShadowWorldDir(mul(ModelViewProj, float4(tbn[1], 0.0f))));
    float2 upTS = float2(worldTangent.z, worldBinormal.z);
    OUT.uv = float4(IN.uv.xy, upTS);
    #ifdef MERGED_LIGHTS
        float3 worldNormal = normalize(GetShadowWorldDir(mul(ModelViewProj, float4(tbn[2], 0.0f))));
        float handedness = dot(cross(worldNormal, worldTangent), worldBinormal) >= 0.0f ? 1.0f : 0.0f;
        #ifdef MERGED_PACKED
            OUT.worldNormal = float4(worldNormal * 0.5f + 0.5f, worldTangent.x * 0.5f + 0.5f);
        #else
            OUT.worldNormal = float4(worldNormal * 0.5f + 0.5f, handedness);
            OUT.worldTangent = float4(worldTangent * 0.5f + 0.5f, 0.0f);
        #endif
    #endif
    #ifdef WORLD_FRAME
        float3 frameNormal = normalize(GetShadowWorldDir(mul(ModelViewProj, float4(tbn[2], 0.0f))));
        float frameRightHanded = dot(cross(frameNormal, worldTangent), worldBinormal) >= 0.0f ? 1.0f : 0.0f;
        OUT.worldFrameNormal = float4(frameNormal, worldTangent.x);   // the tangent's y in lightDir.w below
    #endif

    float3 eye = EyePosition.xyz - IN.position.xyz;
    OUT.viewDir.xyz = mul(tbn, eye);
    OUT.viewDir.w = length(eye);

    #ifndef NO_VERTEX_COLOR
        OUT.vertexColor = clamp(IN.vertex_color, 0.0f, 1.0f);
    #endif

    #ifndef NO_LIGHT
        #ifndef POINT
            OUT.lightDir.w = LightData[0].w;
            #ifndef DIFFUSE
                OUT.lightDir.xyz = mul(tbn, LightData[0].xyz);
            #else
                float3 light = LightData[0].xyz - IN.position.xyz;
                OUT.lightDir.xyz = mul(tbn, light);
                OUT.lightAtt.w = 0.5;
                OUT.lightAtt.xyz = compress(light / LightData[0].w);
            #endif
            #if LIGHTS > 1
                float3 light2 = LightData[1].xyz - IN.position.xyz;
                OUT.light2Dir.w = LightData[1].w;
                OUT.light2Dir.xyz = mul(tbn, light2);
                OUT.light2Att.w = 0.5;
                OUT.light2Att.xyz = compress(light2 / LightData[1].w);
            #endif
            #if LIGHTS > 2
                float3 light3 = LightData[2].xyz - IN.position.xyz;
                OUT.light3Dir.w = LightData[2].w;
                OUT.light3Dir.xyz = mul(tbn, light3);
                OUT.light3Att.w = 0.5;
                OUT.light3Att.xyz = compress(light3 / LightData[2].w);
            #endif
        #else
            OUT.lightDir.w = LightData[0].w;
            OUT.lightDir.xyz = mul(tbn, LightData[0].xyz - IN.position.xyz);
            #if NUM_PT_LIGHTS > 1
                OUT.light2Dir.w = LightData[1].w;
                OUT.light2Dir.xyz = mul(tbn, LightData[1].xyz - IN.position.xyz);
            #endif
            #if NUM_PT_LIGHTS > 2
                OUT.light3Dir.w = LightData[2].w;
                OUT.light3Dir.xyz = mul(tbn, LightData[2].xyz - IN.position.xyz);
            #endif
        #endif
    #endif

    #ifdef MERGED_PACKED
        OUT.light2Att.w = worldTangent.y;
        OUT.light3Att.w = worldTangent.z;
        OUT.lightDir.w = handedness;
    #endif
    #ifdef WORLD_FRAME
        OUT.lightDir.w = worldTangent.y + (frameRightHanded > 0.5f ? 0.0f : 4.0f);
    #endif

    #ifndef NO_FOG
        float3 fogPos = OUT.sPosition.xyz;
        #ifdef REVERSED_DEPTH
            fogPos.z = OUT.sPosition.w - fogPos.z;
        #endif
        float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
        fogStrength = log2(fogStrength);  // Unclear.
        OUT.fogColor.a = exp2(fogStrength * FogParam.z);
        OUT.fogColor.rgb = FogColor.rgb;
    #endif

    #ifdef PROJ_SHADOW
        float shadowParam = dot(ShadowProj[3].xyzw, IN.position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, IN.position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, IN.position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.sPosition), SHADOW_VS_SENTINEL);

    return OUT;
};

#endif // Vertex shader.

struct PS_INPUT
{
    float2 vpos : VPOS;   // the pixel's screen position (Includes/PointShadow.hlsl, POINT_SHADOW_PIXEL)
#ifndef NO_VERTEX_COLOR
    float3 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 uv : TEXCOORD0;
#ifndef NO_LIGHT
    float4 lightDir : TEXCOORD1_centroid;
#endif
#ifdef DIFFUSE
    float4 lightAtt : TEXCOORD2;
#endif
#if LIGHTS > 1
    float4 light2Dir : TEXCOORD3_centroid;
    float4 light2Att : TEXCOORD4;
#endif
#if LIGHTS > 2
    float4 light3Dir : TEXCOORD5_centroid;
    float4 light3Att : TEXCOORD6;
#endif
#if NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD3_centroid;
#endif
#if NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD5_centroid;
#endif
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD8;
#endif
    float4 viewDir : TEXCOORD7_centroid;
    float4 shadowWorldPos : TEXCOORD9;
#ifdef MERGED_PACKED
    float4 worldNormal : COLOR0;
#elif defined(MERGED_LIGHTS)
    float4 worldNormal : COLOR0;
    float4 worldTangent : COLOR1;
#endif
#ifdef WORLD_FRAME
    float4 worldFrameNormal : TEXCOORD5;
#endif
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

#ifdef PS


#if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    #if !defined(NO_LIGHT)
        float4 AmbientColor : register(c1);
    #endif
    sampler2D BaseMap : register(s0);
#endif
#if !defined(NO_LIGHT)
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        sampler2D NormalMap : register(s1);
    #else
        sampler2D NormalMap : register(s0);
    #endif
#endif
#if !defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(NO_LIGHT)
    sampler2D HeightMap : register(s3);
#else
    sampler2D HeightMap : register(s2);
#endif
#ifdef SI
float4 EmittanceColor : register(c2);
    #ifndef ONLY_LIGHT
        sampler2D GlowMap : register(s4);
    #else
        sampler2D GlowMap : register(s3);
    #endif
#endif
#if LIGHTS > 1
    #ifdef DIFFUSE
        sampler2D AttenuationMap : register(s3);
    #elif defined(ONLY_LIGHT)
        sampler2D AttenuationMap : register(s4);
    #else
        sampler2D AttenuationMap : register(s5);
    #endif
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
#if !defined(NO_LIGHT)
    float4 PSLightColor[10] : register(c3);
#endif
#ifndef OPT
    float4 Toggles : register(c27);

    #define useVertexColor Toggles.x
    #define useFog Toggles.y
    #define glossPower Toggles.z
    #define alphaTestRef Toggles.w
#else
    #define glossPower 1  // OPT is never used in combination with specular in PAR.
#endif

#define	uvtile(w)		(((w) * 0.04) - 0.02)

#ifdef MERGED_LIGHTS
// The merged lamps (MERGED_LIGHTS above), as the additive DIFFUSE passes would have lit them, in
// world space: the normal map's normal (at the parallax offset) through the world frame the vertex
// shader sent. The vanilla fallback (TESR_ParallaxData.y off) lights them the vanilla way, as the
// passes it replaces did.
// The merged lamps' shadows, value i for lamp i, first thing in main (POINT_SHADOW_PACKED above).
// normal: the smooth world normal the loop below lights with. Only with PBR, the only lighting
// that applies them.
void PointShadowPackMerged(float3 worldPos, float3 normal, float valid) {
    pointShadowPackA = 0.0f;
    pointShadowPackB = 0.0f;
    [branch] if (TESR_ParallaxData.y && TESR_PointShadowData.x > 0.0f && valid > 0.0f) {
        [loop] for (int i = 0; i < MERGED_MAX_LIGHTS; i++) {
            if (i >= TESR_MergedLightCount.x) break;
            // A lamp whose light does not reach the pixel (getMergedPointLights' own test) needs no shadow.
            float3 L = TESR_MergedLightPosition[i].xyz - worldPos;
            [branch] if (vanillaAttSq(dot(L, L), TESR_MergedLightPosition[i].w) > 0.0f)
                PointShadowPackValue(i, PointShadowVisibilitySoft(TESR_MergedLightColor[i].w, TESR_PointShadowMerged[i], worldPos, normal, valid));
        }
    }
}

float3 getMergedPointLights(float3 worldNormal, float3 worldTangent, float handedness, float4 normalTS, float3 worldPos, float worldPosValid, float3 albedo, float roughness) {
    float3 total = 0.0f;
    [branch] if (TESR_MergedLightCount.x > 0.0f) {
        float3 N = normalize(worldNormal);
        float3 T = normalize(worldTangent - N * dot(worldTangent, N));
        float3 B = cross(N, T) * (handedness > 0.5f ? 1.0f : -1.0f);
        float3 n = normalize(normalTS.x * T + normalTS.y * B + normalTS.z * N);
        float3 V = -worldPos;

        [loop] for (int i = 0; i < MERGED_MAX_LIGHTS; i++) {
            if (i >= TESR_MergedLightCount.x) break;
            float3 L = TESR_MergedLightPosition[i].xyz - worldPos;
            float att = vanillaAttSq(dot(L, L), TESR_MergedLightPosition[i].w);
            [branch] if (att > 0.0f) {
                if (TESR_ParallaxData.y)
                    total += getPointLightLightingAtt(L, att, TESR_MergedLightColor[i].rgb, V, n, albedo, roughness, length(L),
                        PointShadowPackedVisibility(i));   // looked up at the top of main (PointShadowPackMerged)
                else
                    total += getVanillaLightingAtt(L, att, TESR_MergedLightColor[i].rgb, V, n, albedo, normalTS.a, glossPower);
            }
        }
    }
    return total;
}
#endif

PS_OUTPUT main(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    POINT_SHADOW_PIXEL(IN.vpos);

    #ifdef POINT_SHADOW_PACKED
        // First, while nothing else is held: the merged lamps' shadows, with the normal they light with.
        PointShadowPackMerged(IN.shadowWorldPos.xyz, normalize(IN.worldNormal.xyz * 2.0f - 1.0f), SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
    #endif

    #if !defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(NO_LIGHT)
        float alpha = tex2D(BaseMap, IN.uv.xy).a;

        #ifndef OPT
            clip(AmbientColor.a >= 1 ? 0 : (alpha - alphaTestRef));
        #endif
    #endif

    // Parallax.
    float3 viewDir = normalize(IN.viewDir.xyz);
    float distance = IN.viewDir.w;

    float2 dx, dy;
    dx = ddx(IN.uv.xy);
    dy = ddy(IN.uv.xy);

    float2 offsetUV = getParallaxCoords(distance, IN.uv.xy, dx, dy, viewDir.xyz, HeightMap);

    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 baseColor = tex2D(BaseMap, offsetUV.xy);
        float3 adAlbedo = baseColor.rgb;   // see "Light-only passes" in Object.hlsl

        #if defined(ONLY_LIGHT)
            baseColor.rgb = 1.f;
        #endif
    #else
        float4 baseColor = 1.f;
    #endif

    // Vertex color.
    #ifndef NO_VERTEX_COLOR
        #ifndef OPT
            // Apply vertex color if toggled.
            baseColor.xyz = (useVertexColor <= 0.0 ? baseColor.xyz : (baseColor.xyz * IN.vertexColor.rgb));
        #else
            baseColor.xyz = baseColor.xyz * IN.vertexColor.rgb;
        #endif
    #endif

    // Shadows.
    float3 shadowMultiplier = 1.0;
    #ifdef PROJ_SHADOW
        float3 shadow = tex2D(ShadowMap, IN.shadowUVs.xy).xyz;
        float shadowMask = tex2D(ShadowMaskMap, IN.shadowUVs.zw).x;
        shadowMultiplier = lerp(1, shadow, shadowMask);
    #endif

    // Visibilities, applied to linear light (see "Lighting space" in PBR.hlsl), unlike the vanilla
    // projected shadow above, which is a gamma-space factor and stays in shadowMultiplier.
    float sunVisibility = 1.0f;
    #ifndef NO_LIGHT
        sunVisibility = getParallaxShadowMultipler(distance, offsetUV, dx, dy, normalize(IN.lightDir.xyz), HeightMap);
    #endif

    // Forward sun shadows, folded into shadowMultiplier -- which scales PSLightColor[0]
    // (the sun) only, leaving ambient untouched. Skipped for DIFFUSE/POINT passes, which
    // carry a point light in slot 0 rather than the sun.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    // Hoisted out of the shadow block below: the ambient term also needs it, and that runs for
    // DIFFUSE/POINT passes which the shadow block skips. ddx/ddy must stay at top level.
    float3 sunShadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
    float shadowWorldPosValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;

    #if !defined(NO_LIGHT) && !defined(DIFFUSE) && !defined(POINT)
        #if FORWARD_SHADOWS
        sunVisibility *= shadowWorldPosValid
                       ? GetSunShadow(IN.shadowWorldPos.xyz, sunShadowNormal)
                       : 1.0f;
        #endif
    #endif

    // Lighting.
    float3 lighting;
    float finalAtt;

    #ifdef NO_LIGHT
        lighting = baseColor.rgb;
    #else
        float4 normal = tex2D(NormalMap, offsetUV.xy);
        normal.xyz = normalize(expand(normal.xyz));

        // Material, as in ObjectTemplate.hlsl (Object.hlsl setupMaterial), at the parallax offset.
        // OPT variants carry no Toggles (glossPower is a dummy 1 there), so they take the engine's
        // default glossiness. roughness is only the debug view's.
        #ifndef OPT
            float shine = glossPower;
        #else
            float shine = 30.0f;
        #endif
        #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
            float3 materialAlbedo = adAlbedo;
        #else
            float3 materialAlbedo = 1.0f;
        #endif
        setupMaterial(normal.a, shine, normal.xyz, offsetUV.xy, materialAlbedo);
        float roughness = getMaterialRoughness(shine);
        // The vanilla lighting fallback stays in gamma space, exactly as before.
        if (!TESR_ParallaxData.y) linearLighting = false;
        #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
            setupADCompensation(adAlbedo);
        #endif
        #ifdef MERGED_LIGHTS
            // The highlight passes merged in (Includes/MergedLights.hlsl): only flagged materials have them.
            mergedSpecular = TESR_MergedLightCount.y > 0.0f && ObjectMaterial.x >= 0.5f;
        #endif
        baseColor.rgb = decodeColor(baseColor.rgb);

        // Normal-mapped ambient and reflections (Object.hlsl, getAmbientNormal).
        float3 ambientNormal = getAmbientNormal(normal.xyz, rebuildUpTS(IN.uv.zw, sunShadowNormal), sunShadowNormal, shadowWorldPosValid);

        // Reflections want the real normal-mapped world normal; the ambient normal's heading is
        // the flat triangle's, which a detailed environment shows as facets.
        float3 reflectionGeometricNormal = sunShadowNormal;
        float3 reflectionNormal = ambientNormal;
        #if defined(WORLD_FRAME)
            [branch] if (shadowWorldPosValid > 0.0f) {
                float tangentY = IN.lightDir.w;
                float mirrored = tangentY > 2.0f ? 1.0f : 0.0f;
                reflectionGeometricNormal = normalize(IN.worldFrameNormal.xyz);
                reflectionNormal = getReflectionNormal(reflectionGeometricNormal, float3(IN.worldFrameNormal.w, tangentY - 4.0f * mirrored, IN.uv.z), mirrored, normal.xyz);
            }
        #elif defined(MERGED_PACKED)
            [branch] if (shadowWorldPosValid > 0.0f) {
                reflectionGeometricNormal = normalize(IN.worldNormal.xyz * 2.0f - 1.0f);
                reflectionNormal = getReflectionNormal(reflectionGeometricNormal, float3(IN.worldNormal.w * 2.0f - 1.0f, IN.light2Att.w, IN.light3Att.w), IN.lightDir.w > 0.5f ? 0.0f : 1.0f, normal.xyz);
            }
        #elif defined(MERGED_LIGHTS)
            [branch] if (shadowWorldPosValid > 0.0f) {
                reflectionGeometricNormal = normalize(IN.worldNormal.xyz * 2.0f - 1.0f);
                reflectionNormal = getReflectionNormal(reflectionGeometricNormal, IN.worldTangent.xyz * 2.0f - 1.0f, IN.worldNormal.w > 0.5f ? 0.0f : 1.0f, normal.xyz);
            }
        #endif

        #if !defined(DIFFUSE) && !defined(POINT)
            if (TESR_ParallaxData.y)
                lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, sunVisibility);
            else
                lighting = getVanillaLightingAtt(IN.lightDir.xyz, 1.f, PSLightColor[0].rgb * shadowMultiplier * sunVisibility, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #elif defined(DIFFUSE)
            // Pointlight vanilla att.
            if (TESR_ParallaxData.y)
                lighting = getPointLightLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness,
                    PointShadowBase(0, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else {
                finalAtt = lampFalloff(tex2D(AttenuationMap, IN.lightAtt.xy).x + tex2D(AttenuationMap, IN.lightAtt.zw).x, IN.lightDir.w);   // the maps give (d / r)^2
                lighting = getVanillaLightingAtt(IN.lightDir.xyz, finalAtt, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
            }
        #else
            if (TESR_ParallaxData.y)
                lighting = getPointLightLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness,
                    PointShadowBase(0, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else
                lighting = getVanillaLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif

        // Self emmitance.
        #ifdef SI
            float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
            lighting += baseColor.rgb * decodeColor(glow.rgb * EmittanceColor.rgb);
        #endif

        #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
            if (TESR_ParallaxData.y) {
                // Reflection first: it sets the share of the ambient it takes (skyReflectedFraction).
                lighting += getObjectSkyReflection(IN.shadowWorldPos.xyz, reflectionGeometricNormal, reflectionNormal, roughness, shadowWorldPosValid);
                probeWorldPos = IN.shadowWorldPos.xyz;   // for the bounce lighting (Includes/BounceLighting.hlsl)
                probeWorldPosValid = shadowWorldPosValid;
                lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, ambientNormal, shadowWorldPosValid);
            }
            else
                lighting += baseColor.rgb * AmbientColor.rgb;
        #endif

        // Other light sources.
        #if LIGHTS > 1
            finalAtt = lampFalloff(tex2D(AttenuationMap, IN.light2Att.xy).x + tex2D(AttenuationMap, ATTENUATION_UV2(IN.light2Att)).x, IN.light2Dir.w);

            if (TESR_ParallaxData.y)
                lighting += getPointLightLightingAtt(IN.light2Dir.xyz, finalAtt, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, 0.0f,
                    PointShadowBase(1, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else
                lighting += getVanillaLightingAtt(IN.light2Dir.xyz, finalAtt, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif

        #if LIGHTS > 2
            finalAtt = lampFalloff(tex2D(AttenuationMap, IN.light3Att.xy).x + tex2D(AttenuationMap, ATTENUATION_UV2(IN.light3Att)).x, IN.light3Dir.w);

            if (TESR_ParallaxData.y)
                lighting += getPointLightLightingAtt(IN.light3Dir.xyz, finalAtt, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness, 0.0f,
                    PointShadowBase(2, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else
                lighting += getVanillaLightingAtt(IN.light3Dir.xyz, finalAtt, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif

        #if NUM_PT_LIGHTS > 1
            if (TESR_ParallaxData.y)
                lighting += getPointLightLighting(IN.light2Dir.xyz, IN.light2Dir.w, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness,
                    PointShadowBase(1, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else
                lighting += getVanillaLighting(IN.light2Dir.xyz, IN.light2Dir.w, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif

        #if NUM_PT_LIGHTS > 2
            if (TESR_ParallaxData.y)
                lighting += getPointLightLighting(IN.light3Dir.xyz, IN.light3Dir.w, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness,
                    PointShadowBase(2, IN.shadowWorldPos.xyz, sunShadowNormal, shadowWorldPosValid));
            else
                lighting += getVanillaLighting(IN.light3Dir.xyz, IN.light3Dir.w, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif

        // The lamps of this mesh's additive passes (MERGED_LIGHTS above); those passes are muted.
        #ifdef MERGED_PACKED
            lighting += getMergedPointLights(IN.worldNormal.xyz * 2.0f - 1.0f, float3(IN.worldNormal.w * 2.0f - 1.0f, IN.light2Att.w, IN.light3Att.w), IN.lightDir.w, normal, IN.shadowWorldPos.xyz, shadowWorldPosValid, baseColor.rgb, roughness);
        #elif defined(MERGED_LIGHTS)
            lighting += getMergedPointLights(IN.worldNormal.xyz * 2.0f - 1.0f, IN.worldTangent.xyz * 2.0f - 1.0f, IN.worldNormal.w, normal, IN.shadowWorldPos.xyz, shadowWorldPosValid, baseColor.rgb, roughness);
        #endif
    #endif

    // Fog.
    #ifndef NO_LIGHT
        lighting = encodeColor(lighting);   // back to the game's gamma space, before fog (no-op for vanilla lighting)

        #if !defined(ONLY_LIGHT)
            [branch] if (TESR_ParallaxData.y && TESR_PBRDebugData.x > 0.0f)
                lighting = getMaterialDebug(TESR_PBRDebugData.x, roughness, ambientNormal);
        #endif
    #endif

    #ifndef NO_FOG
        #ifndef OPT
            lighting.rgb = (useFog <= 0.0 ? lighting.rgb : lerp(lighting.rgb, IN.fogColor.rgb, IN.fogColor.a));
        #else
            lighting.rgb = lerp(lighting.rgb, IN.fogColor.rgb, IN.fogColor.a);
        #endif
    #endif

    OUT.color.rgb = lighting.rgb;

    #if defined(DIFFUSE) || defined(NO_LIGHT)
        OUT.color.a = 1;
    #elif defined(ONLY_SPECULAR)
        if (!TESR_ParallaxData.y)
            OUT.color.rgb = saturate(OUT.color.rgb);
        OUT.color.a = weight(lighting.rgb);
    #elif defined(ONLY_LIGHT)
        OUT.color.a = baseColor.a;
    #else
        OUT.color.a = alpha * AmbientColor.a;
    #endif

    // Bounce lighting's debug view: the light the probes give, alone. The additive light passes add
    // nothing; the others keep what they draw (a multipass mesh's texture pass multiplies the debug
    // colour by its texture, as single passes show it: written black, it blacked the whole floor).
    // Magenta (view 2): the ambient worked out without a world position (a vanilla vertex shader).
    if (TESR_ProbeLighting.z > 0.5f && TESR_ProbeLighting.x > 0.0f) {
        if (probeDebugSet > 0.5f) OUT.color.rgb = encodeColor(probeDebugLight);
        else if (probeAmbientRan > 0.5f && TESR_ProbeLighting.z > 1.5f) OUT.color.rgb = float3(1.0f, 0.0f, 1.0f);
        #if defined(DIFFUSE) || defined(POINT) || defined(ONLY_SPECULAR)
        else OUT.color.rgb = 0.0f;
        #endif
    }

    return OUT;
};

#endif // Pixel shader.