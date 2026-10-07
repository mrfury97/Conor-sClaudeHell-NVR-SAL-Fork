// NVR skin shader: every vanilla SKIN20xx vertex and pixel shader, built from this one file
// (see SkinShaders::Templates() in src/effects/Skin.h). Lighting is in Includes/SkinLighting.hlsl.
//
// Both halves are replaced, so the interface between them is NVR's own; only the engine's
// constants and the vertex declarations have to match vanilla's. Those were read off the vanilla
// shaders (shaderpackage010.sdp):
//
//   VS constants: ModelViewProj c0 / SkinModelViewProj c1, FogParam c14, FogColor c15,
//   ShadowProj c18-c23, LightData c25+ (OBJECT space: [0] the sun direction, then point light
//   position + radius; in the DIFFUSE family every entry is a point light), Bones c44.
//   PS constants: AmbientColor c1, EmittanceColor c2 (.a = light count, many-light family),
//   PSLightColor c3+, Toggles c27 (not in the AD and DIFFUSE families).
//
// Variants (defines):
//   SKIN         bone-skinned geometry
//   POINTS=n     point lights after the sun (DIFFUSE: point lights in all)
//   PROJ_SHADOW  vanilla's projected actor shadow (never with more than 2 point lights)
//   ONLY_LIGHT   AD pass: writes light only (ambient included), the engine multiplies by the
//                texture afterwards. No fog, vertex colour or Toggles.
//   DIFFUSE      point-light-only additive pass. No ambient, base texture, fog or Toggles.
//   MANY         the many-light family: EmittanceColor.a says how many lights are live.
//
// Direct lighting is in TANGENT space, as vanilla's is: the vertex shader takes the light and
// view vectors into the vertex's tangent frame, so nothing depends on the projection. That
// matters because the engine draws first-person arms with their own camera (FOV and near
// plane), while NVR's inverse projection constants are the world camera's: world vectors
// rebuilt from clip space are skewed there. They are only used for the sky terms, the sun
// shadow and the curvature, where a skew costs little.
//
// VS -> PS:
//   TEXCOORD0  uv.xy, sun.xy (tangent)                  TEXCOORD4  world position, view.z
//   TEXCOORD1  world tangent, sun.z                     TEXCOORD5  point light 1, light 4.x
//   TEXCOORD2  world binormal, view.x (tangent)         TEXCOORD6  point light 2, light 4.y
//   TEXCOORD3  world normal, view.y                     TEXCOORD7  point light 3, light 4.z --
//                                                                  or the PROJ_SHADOW uvs
//   COLOR0/1   vertex colour, fog (base and MANY families only)
// All ten of ps_3_0's input registers in the largest variants. A point light's tangent-space
// light vector divided by its radius gives both the direction and the attenuation,
// 1 - |v|^2, vanilla's own falloff.

#if defined(__INTELLISENSE__)
    #define PS
    #define POINTS 2
#endif

#ifndef POINTS
    #define POINTS 0
#endif

#if defined(DIFFUSE)
    #define ONLY_LIGHT
#endif

#if defined(ONLY_LIGHT)
    #define OPT
#else
    #define FOG_COLOR
#endif

// PROJ_SHADOW shares TEXCOORD7 with the third point light; vanilla never combines them.

#include "Includes/Helpers.hlsl"
// The camera matrices Shadow.hlsl rebuilds world positions with are moved out of c100-c107 in vertex
// shaders: the game's bone upload goes past Bones[54] (c97) and overwrote them in skinned ones, so actors
// read the sun shadow at the wrong place. c240-c247 is beyond any bone write. (Upstream PR #79.)
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#include "Includes/Shadow.hlsl"

#if defined(VS)

#include "Includes/SkinHelpers.hlsl"

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
#ifdef FOG_COLOR
    float4 color : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 shadowWorldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
    #define MVP ModelViewProj
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
    #define MVP SkinModelViewProj
#endif

#ifdef FOG_COLOR
    float4 FogParam : register(c14);
    float3 FogColor : register(c15);
#endif

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif

float4 EyePosition : register(c16);
float4 LightData[10] : register(c25);

// An object-space direction to (scaled) world space, through the same transform as the position.
// The w after the inverse projection is dropped: it is 0 for anything drawn with the world
// camera, and for first-person geometry (a different near plane) it would add the camera's
// position into a direction.
float3 ToWorld(float3 v) {
    float4 viewDir = mul(mul(MVP, float4(v, 0.0f)), TESR_InvProjectionTransform);
    viewDir.w = 0.0f;
    return mul(viewDir, TESR_InvViewTransform).xyz;
}

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

// Tangent-space light vector / radius (both in object units).
float3 PointLightVector(int i, float3 position, float3x3 tbn) {
    float4 light = LightData[FIRST_POINT + i];
    return mul(tbn, (light.xyz - position) / light.w);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = float4(IN.position.xyz, 1.0f);
    #ifndef SKIN
        float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    #endif

    OUT.position = mul(MVP, position);

    // World frame for the sky terms; tangent-space vectors for direct lighting (see the header).
    float3 viewTS = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.tangent = float4(normalize(ToWorld(tbn[0])), 0.0f);
    OUT.binormal = float4(normalize(ToWorld(tbn[1])), viewTS.x);
    OUT.normal = float4(normalize(ToWorld(tbn[2])), viewTS.y);
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.position), viewTS.z);

    #ifndef DIFFUSE
        float3 sunTS = normalize(mul(tbn, LightData[0].xyz));
    #else
        float3 sunTS = 0.0f;
    #endif
    OUT.uv = float4(IN.uv.xy, sunTS.xy);
    OUT.tangent.w = sunTS.z;

    float3 light4 = 0.0f;
    #if POINTS > 3
        light4 = PointLightVector(3, position.xyz, tbn);
    #endif
    #if POINTS > 0
        OUT.light1 = float4(PointLightVector(0, position.xyz, tbn), light4.x);
    #endif
    #if POINTS > 1
        OUT.light2 = float4(PointLightVector(1, position.xyz, tbn), light4.y);
    #endif
    #if POINTS > 2
        OUT.light3 = float4(PointLightVector(2, position.xyz, tbn), light4.z);
    #elif defined(PROJ_SHADOW)
        float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    #ifdef FOG_COLOR
        // Vanilla's fog, from the clip-space xyz distance.
        float fogStrength = saturate((FogParam.x - length(OUT.position.xyz)) / FogParam.y);
        OUT.fog = float4(FogColor, pow(1.0f - fogStrength, FogParam.z));
        OUT.color = IN.color;
    #endif

    return OUT;
}

#endif // VS

#if defined(PS)

#include "Includes/SkinLighting.hlsl"

struct PS_INPUT {
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 shadowWorldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

// Vanilla's sampler slots per family.
#if defined(DIFFUSE)
    sampler2D NormalMap : register(s0);
#elif defined(ONLY_LIGHT)
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);   // bound by the SkinShader hook on faces (SkinScreenSpaceScatter.y)
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s5);
        sampler2D ShadowMaskMap : register(s6);
    #endif
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s6);
        sampler2D ShadowMaskMap : register(s7);
    #endif
#endif

float4 AmbientColor : register(c1);
float4 EmittanceColor : register(c2);
float4 PSLightColor[10] : register(c3);
#ifndef OPT
    float4 Toggles : register(c27);   // x vertex colour, y fog, z shine (unused), w alpha test ref
#endif

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

// Point light i: d is its light vector / radius.
float3 PointLight(int i, float3 d, float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, out float3 specular) {
    float3 lightColor = PSLightColor[FIRST_POINT + i].rgb * (1.0f - saturate(dot(d, d)));
    #ifdef MANY
        // Unused slots (EmittanceColor.a lights are live) can hold anything, NaN included, so
        // they are branched around, not multiplied by 0. The branch also keeps the compiler
        // from interleaving all four lights, which overflows ps_3_0's 32 temporaries.
        float3 diffuse = 0.0f;
        specular = 0.0f;
        [branch] if ((FIRST_POINT + i) < EmittanceColor.a)
            diffuse = SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
        return diffuse;
    #else
        return SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
    #endif
}

// [Shaders.Skin.Debug] DebugView 1-7, replacing the colour this pass writes:
//   1 which pass drew the skin: green one full pass (the sun and up to one point light), cyan one
//     full pass with up to four point lights, yellow a light-only pass the engine multiplies by
//     the texture afterwards, red added for every extra point-light pass on top. Indoors, skin
//     usually goes the yellow and red route.
//   2 which settings are in use: blue Interiors, orange outdoors (Main + Scattering); bright where
//     the screen-space blur takes this skin, dark where per-pixel diffusion stands in
//   3 direct light (sun and lamps, with the scattering and translucency) on white skin
//   4 ambient and sky light on white skin
//   5 highlights alone: the lights' and the sky reflection
//   6 material: red roughness, green highlight strength (specular mask x SpecularStrength), blue
//     the VanillaMatchedHighlights boost (full at its 12x cap)
//   7 curvature: red the per-pixel diffusion (PerPixelWidth; 0 where the blur takes over), green
//     thin features, where Translucency can show
// The light-only passes are divided by the texture the engine multiplies them by afterwards, so
// every view reads true. The extra point-light passes add to the pass under them: in 3 and 5 they
// add their own light (the engine's texture pass tints it), elsewhere nothing, or red in 1.
float3 SkinDebugView(float view, float3 direct, float3 ambient, float3 highlights, float3 albedo, float3 adAlbedo) {
    #if defined(DIFFUSE)
        if (view < 1.5f) return float3(0.5f, 0.0f, 0.0f);
        if (view > 2.5f && view < 3.5f) return encodeColor(direct);
        if (view > 4.5f && view < 5.5f) return encodeColor(highlights);
        return 0.0f;
    #else
        float3 onWhite = 1.0f / max(albedo, 0.02f);   // light as it would fall on white skin
        float3 c;
        if (view < 1.5f) {
            #if defined(ONLY_LIGHT)
                c = float3(1.0f, 1.0f, 0.0f);
            #elif defined(MANY)
                c = float3(0.0f, 1.0f, 1.0f);
            #else
                c = float3(0.0f, 1.0f, 0.0f);
            #endif
        }
        else if (view < 2.5f) {
            c = TESR_SkinDebugData.y > 0.0f ? float3(0.2f, 0.45f, 1.0f) : float3(1.0f, 0.55f, 0.1f);
            c *= SkinScreenSpaceScatter.x > 0.0f ? 1.0f : 0.3f;
        }
        else if (view < 3.5f) c = encodeColor(direct * onWhite);
        else if (view < 4.5f) c = encodeColor(ambient * onWhite);
        else if (view < 5.5f) c = encodeColor(highlights);
        else if (view < 6.5f) c = float3(skinRoughness, saturate(skinSpecScale * 0.5f), saturate((skinVanillaMatch - 1.0f) / 11.0f));
        else c = float3(skinCurvature, skinThinness, 0.0f);
        #if defined(ONLY_LIGHT)
            c /= max(adAlbedo, 0.05f);
        #endif
        return c;
    #endif
}

// COLOR1 goes to the skin scattering effect's target, bound only during NVR's skin draws in the
// main scene (src/effects/SkinScattering.h); anywhere else it is discarded.
//   rgb  the highlights' share of COLOR0 (gamma, fogged), which the effect takes out before it
//        blurs the skin and puts back after
//   a    the camera distance, which the effect checks against the depth buffer
// COLOR2 goes to its albedo target:
//   rgb  the colour the light is multiplied by, divided out before the blur so only light diffuses
//        and the texture stays sharp
//   a    the luminance of the colour this draw leaves in the scene: anything drawn over the skin
//        afterwards changes it, which is how the effect tells covered skin (hair cards, decals)
//        from visible skin even where the covering writes no depth
struct PS_OUTPUT {
    float4 color : COLOR0;
    float4 scatter : COLOR1;
    float4 albedo : COLOR2;
};

PS_OUTPUT main(PS_INPUT IN) {
    // --- material
    #if defined(DIFFUSE)
        float4 baseColor = 1.0f;
        float3 adAlbedo = 1.0f;   // no base texture bound to divide by
    #elif defined(ONLY_LIGHT)
        float4 baseColor = tex2D(BaseMap, IN.uv.xy);
        // What the engine multiplies this pass by afterwards: the base texture, or on faces the
        // FaceGen blend its texture pass (SLS1005) applies, the same as the full pass's.
        float3 faceGenColor = 2.0f * ((expand(tex2D(FaceGenMap0, IN.uv.xy).rgb) + baseColor.rgb) * (2.0f * tex2D(FaceGenMap1, IN.uv.xy).rgb));
        float3 adAlbedo = SkinScreenSpaceScatter.y > 0.0f ? faceGenColor : baseColor.rgb;
        baseColor.rgb = 1.0f;
    #else
        float4 baseTexture = tex2D(BaseMap, IN.uv.xy);
        clip(AmbientColor.a >= 1 ? 0 : (baseTexture.a - Toggles.w));

        float3 faceGenVar = tex2D(FaceGenMap0, IN.uv.xy).rgb;
        float3 faceGenBlend = tex2D(FaceGenMap1, IN.uv.xy).rgb;
        float4 baseColor = float4(2.0f * ((expand(faceGenVar) + baseTexture.rgb) * (2.0f * faceGenBlend)), baseTexture.a);
        baseColor.rgb = Toggles.x <= 0.0f ? baseColor.rgb : baseColor.rgb * IN.color.rgb;
        float3 adAlbedo = 1.0f;
    #endif

    // --- normals: tangent space for direct light, world for the sky
    float4 normalTexel = tex2D(NormalMap, IN.uv.xy);
    float3 N = normalize(expand(normalTexel.xyz));
    float3 Nsoft = normalize(expand(tex2Dbias(NormalMap, float4(IN.uv.xy, 0.0f, 2.0f)).xyz));
    const float3 geometricNormal = float3(0.0f, 0.0f, 1.0f);
    float3 V = normalize(float3(IN.binormal.w, IN.normal.w, IN.shadowWorldPos.w));

    float3 worldPos = IN.shadowWorldPos.xyz;

    setupSkin(N, IN.normal.xyz, worldPos, normalTexel.a, adAlbedo);
    float3 albedo = decodeColor(baseColor.rgb);

    // --- shadows (top level: both take derivatives)
    #if !defined(DIFFUSE)
        #if FORWARD_SHADOWS
            float3 shadowNormal = GetShadowGeometricNormal(worldPos);
            float sunShadow = GetSunShadow(worldPos, shadowNormal);   // this template always supplies worldPos
            // The same SKIN_SELF_SHADOW_REACH toward the sun: lit there, a shadow here is the skin's own.
            float sunShadowBeyond = GetSunShadow(worldPos + TESR_SmoothedSunDir.xyz * SKIN_SELF_SHADOW_REACH, shadowNormal);
        #else
            float sunShadow = 1.0f;
            float sunShadowBeyond = 1.0f;
        #endif

        // Vanilla's projected actor shadow, a gamma factor on the sun's colour.
        float3 projShadow = 1.0f;
        #ifdef PROJ_SHADOW
            float3 shadowTexel = tex2D(ShadowMap, IN.shadowUVs.xy).rgb;
            projShadow = lerp(1.0f, shadowTexel, tex2D(ShadowMaskMap, IN.shadowUVs.zw).x);
        #endif
    #endif

    // --- lights: diffuse and highlights kept apart (see PS_OUTPUT)
    float3 diffuseLight = 0.0f;
    float3 specularLight = 0.0f;
    float3 ambientLight = 0.0f;   // kept apart for the debug views
    float3 specular;

    #if !defined(DIFFUSE)
        float3 sunDir = float3(IN.uv.zw, IN.tangent.w);
        diffuseLight += SkinLight(albedo, N, Nsoft, geometricNormal, V, sunDir, PSLightColor[0].rgb * projShadow, sunShadow, sunShadowBeyond, true, specular);
        specularLight += specular;
    #endif

    #if POINTS > 0
        diffuseLight += PointLight(0, IN.light1.xyz, albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 1
        diffuseLight += PointLight(1, IN.light2.xyz, albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 2
        diffuseLight += PointLight(2, IN.light3.xyz, albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 3
        diffuseLight += PointLight(3, float3(IN.light1.w, IN.light2.w, IN.light3.w), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif

    // --- the lamps of the mesh's additive passes, which are then muted (Includes/MergedLights.hlsl)
    // Light-only passes only, without transmission (SkinLampLight). Each lamp's vector goes into
    // tangent space through the world frame, divided by its radius, as the vertex shader does for
    // its own lamps. The frame is read straight from the interpolators inside the loop: held in
    // temporaries next to everything else it overflows ps_3_0's 32.
    #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
        [branch] if (TESR_MergedLightCount.x > 0.0f) {
            [loop] for (int i = 0; i < MERGED_MAX_LIGHTS; i++) {
                if (i >= TESR_MergedLightCount.x) break;
                float3 toLamp = TESR_MergedLightPosition[i].xyz - IN.shadowWorldPos.xyz;
                float3 d = float3(dot(normalize(IN.tangent.xyz), toLamp), dot(normalize(IN.binormal.xyz), toLamp), dot(normalize(IN.normal.xyz), toLamp)) / TESR_MergedLightPosition[i].w;
                float3 lampColor = TESR_MergedLightColor[i].rgb * (1.0f - saturate(dot(d, d)));
                diffuseLight += SkinLampLight(albedo, N, Nsoft, V, d, lampColor, specular);
                specularLight += specular;
            }
        }
    #endif

    // --- sky and ambient (reflection first: it sets the share of the ambient it takes)
    #if !defined(DIFFUSE)
        // Built here, not at the top: nine values held through every light overflow ps_3_0's
        // 32 temporaries in the many-light variants.
        float3x3 worldFrame = float3x3(normalize(IN.tangent.xyz), normalize(IN.binormal.xyz), normalize(IN.normal.xyz));
        float3 worldN = normalize(mul(N, worldFrame));

        specularLight += SkinSkyReflection(-normalize(worldPos), worldFrame[2], worldN);
        ambientLight = SkinAmbient(AmbientColor.rgb, albedo, worldN, normalize(mul(Nsoft, worldFrame)));
        diffuseLight += ambientLight;
    #endif

    float4 color;
    color.rgb = encodeColor(diffuseLight + specularLight * skinCompensation);

    // The highlights' share of the colour once the pass is done: the AD passes are multiplied by
    // the base texture afterwards, which the compensation above already allowed for.
    float3 finalDiffuse = diffuseLight;
    #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
        finalDiffuse *= decodeColor(adAlbedo);
    #endif
    float3 specularShare = encodeColor(finalDiffuse + specularLight) - encodeColor(finalDiffuse);

    #ifdef FOG_COLOR
        bool fogOn = Toggles.y > 0.0f;
        color.rgb = fogOn ? lerp(color.rgb, IN.fog.rgb, IN.fog.a) : color.rgb;
        specularShare *= fogOn ? 1.0f - IN.fog.a : 1.0f;
    #endif

    [branch] if (TESR_SkinDebugData.x > 0.0f)
        color.rgb = SkinDebugView(TESR_SkinDebugData.x, diffuseLight - ambientLight, ambientLight, specularLight, albedo, adAlbedo);

    #if defined(DIFFUSE)
        color.a = 1.0f;
    #elif defined(ONLY_LIGHT)
        color.a = baseColor.a;
    #else
        color.a = baseColor.a * AmbientColor.a;
    #endif

    PS_OUTPUT OUT;
    OUT.color = color;
    OUT.scatter = float4(specularShare, length(worldPos));
    #if defined(DIFFUSE)
        OUT.albedo = 1.0f;   // never written: additive passes leave the albedo target alone
    #elif defined(ONLY_LIGHT)
        // What the engine multiplies this pass by, and the colour once it has.
        OUT.albedo = float4(saturate(adAlbedo), luma(color.rgb * adAlbedo));
    #else
        OUT.albedo = float4(saturate(baseColor.rgb), luma(color.rgb));
    #endif
    return OUT;
}

#endif // PS
