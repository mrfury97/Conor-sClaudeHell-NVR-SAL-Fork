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
    float4 lPosition : TEXCOORD2;   // w: the view depth (terrain blending)
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
    OUT.lPosition = float4(IN.position.xyz, posPS.w);
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
    // Not centroid: terrain blending takes its screen derivatives, which centroid sampling (with anti-aliasing)
    // spoils at triangle edges by moving the sample point there. w: the view depth.
    float4 lPosition : TEXCOORD2;
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
// the land started drawing holds the objects drawn before it; where an object lies just below the land's surface
// the land fades out over it, so its buried part sinks into the ground instead of ending in a hard line. Faded by
// alpha blending, which the shader hooks switch on for the land's draws (TerrainShaders::BeginLandDraw), so no
// TAA is needed; or, with Dither, by a moving dither TAA smooths. (The same blend as Terrain Blending - NVSE.)
//
// The distance is how far the object's surface lies below the land, along the land's normal: the same from every
// viewing angle and on slopes. The band has a constant width across the ground: a fixed depth would be
// depth / tan(angle) wide there, a crisp seam at a wall but metres of see-through land over a shallow road
// shoulder, and a whole buried flat mesh (ending in a hard line at its border) when it lies parallel to the land.
// So the depth that fades is min(BlendDistance, GroundWidth x tan(angle)).
//
// The angle: the object's surface is rebuilt from the depth copy at this pixel and its four neighbours; on each
// axis the side without a jump is used (mesh edges, cracks, a different object), so triangle edges and outlines
// don't read as steep. Its normal against the land (its smoothed normal and its triangle's facet, the flatter;
// the flattest of the pairings that don't jump, so creases don't count) gives the angle, and the same
// neighbours give how fast the gap changes from pixel to pixel (the fade's minimum width).
float4 TESR_TerrainBlendData;   // x: on for this draw, y: blend distance, z: max distance, w: debug view
float4 TESR_TerrainBlendFrame;  // x: frame index, y: 1 to dither instead of blending
float4 TESR_TerrainBlendShape;  // x: ground width, y: height blend, z: slope blend, w: minimum pixels
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

struct TerrainBlendGap {
    float covered;    // how far below the land surface the object is (along the land's normal; 0 or less: none)
    float width;      // how much that changes from one pixel to the next
    float tangent;    // tan of the angle between the object's surface and the ground (0: parallel)
};

// The object behind a land point: along the same view ray, at the depth copy's view depth (view depth grows
// linearly along a ray). All in the land's space (world orientation).
float3 terrainBlendObject(float2 uv, float3 landPos, float landViewDepth, float3 eye) {
    float sceneViewDepth = terrainBlendLinearDepth(tex2Dlod(TESR_TerrainBlendDepth, float4(uv, 0.0f, 0.0f)).x);
    return eye + (landPos - eye) * (sceneViewDepth / max(landViewDepth, 1e-3f));
}

// screenPos: VPOS; landPos, eye: the land point and the eye in the land's space; landViewDepth: its view depth;
// dPx, dPy, dZx, dZy: their ddx/ddy, taken at the top level; normal: the land's geometric normal.
TerrainBlendGap terrainBlendGap(float2 screenPos, float3 landPos, float landViewDepth, float3 dPx, float3 dPy, float dZx, float dZy, float3 eye, float3 normal) {
    TerrainBlendGap gap;
    gap.width = 0.0f;
    gap.tangent = 0.0f;
    float2 texel = TESR_ReciprocalResolution.xy;
    float2 uv = (screenPos + 0.5f) * texel;
    float3 o = terrainBlendObject(uv, landPos, landViewDepth, eye);
    gap.covered = dot(landPos - o, normal);

    float limit = TESR_TerrainBlendData.y * 4.0f * max(1.0f, TESR_TerrainBlendShape.z);
    [branch] if (TESR_TerrainBlendData.x > 0.0f && gap.covered > 0.0f && gap.covered < limit) {
        float3 oR = terrainBlendObject(uv + float2(texel.x, 0.0f), landPos + dPx, landViewDepth + dZx, eye);
        float3 oL = terrainBlendObject(uv - float2(texel.x, 0.0f), landPos - dPx, landViewDepth - dZx, eye);
        float3 oD = terrainBlendObject(uv + float2(0.0f, texel.y), landPos + dPy, landViewDepth + dZy, eye);
        float3 oU = terrainBlendObject(uv - float2(0.0f, texel.y), landPos - dPy, landViewDepth - dZy, eye);
        float3 eR = oR - o, eL = o - oL, eD = oD - o, eU = o - oU;
        float3 ex = length(eR) < length(eL) ? eR : eL;   // the side without a jump
        float3 ey = length(eD) < length(eU) ? eD : eU;
        // Both sides of an axis where neither jumps (within 2x of each other): at a crease of the object's mesh
        // the sides lie on different faces, so every pairing is tried and the flattest kept.
        float3 ex2 = (length(eR) < 2.0f * length(eL) && length(eL) < 2.0f * length(eR)) ? (length(eR) < length(eL) ? eL : eR) : ex;
        float3 ey2 = (length(eD) < 2.0f * length(eU) && length(eU) < 2.0f * length(eD)) ? (length(eD) < length(eU) ? eU : eD) : ey;
        float3 facet = normalize(cross(dPx, dPy));
        float3 n1 = normalize(cross(ex, ey)), n2 = normalize(cross(ex2, ey)), n3 = normalize(cross(ex, ey2)), n4 = normalize(cross(ex2, ey2));
        float sineFacet = min(min(length(cross(n1, facet)), length(cross(n2, facet))), min(length(cross(n3, facet)), length(cross(n4, facet))));
        float sineNormal = min(min(length(cross(n1, normal)), length(cross(n2, normal))), min(length(cross(n3, normal)), length(cross(n4, normal))));
        float sine = min(sineFacet, sineNormal);
        gap.tangent = sine / max(sqrt(saturate(1.0f - sine * sine)), 1e-3f);
        gap.width = abs(dot(dPx - ex, normal)) + abs(dot(dPy - ey, normal));
    }
    return gap;
}

// How deep the fade reaches: BlendDistance (times SlopeBlend on steep land), but no more than GroundWidth across
// the ground. slope: 1 - normal.z.
float terrainBlendDepthRange(TerrainBlendGap gap, float slope) {
    float distance = TESR_TerrainBlendData.y * lerp(1.0f, TESR_TerrainBlendShape.z, saturate(slope * 2.0f));
    return min(distance, TESR_TerrainBlendShape.x * gap.tangent);
}

// How much of the land to keep at this pixel: 1 opaque, 0 faded away; -1 when blending doesn't apply. The fade is
// at least MinPixels wide on screen (a band only a pixel deep crawls as the view moves) and at most four times
// its depth (so a hill in front of a distant object never turns see-through at its edge).
float getTerrainBlend(TerrainBlendGap gap, float landViewDepth, float slope) {
    float maxDistance = TESR_TerrainBlendData.z;
    if (landViewDepth > maxDistance || gap.covered <= 0.0f) return -1.0f;   // the object in front: the depth test handles it
    float distance = terrainBlendDepthRange(gap, slope);
    if (distance < 0.05f) return -1.0f;   // parallel to the ground: nothing to fade
    float range = clamp(gap.width * TESR_TerrainBlendShape.w, distance, distance * 4.0f);
    float keep = saturate(gap.covered / range);
    // Back to a hard edge over the last quarter before the max distance, so there's no visible line.
    return lerp(keep, 1.0f, saturate((landViewDepth / maxDistance - 0.75f) * 4.0f));
}

// A natural edge: where the land's texture is high (its height map: the diffuse alpha), the land reaches a little
// further over the object, where it is low a little less. Strongest mid-fade, none at either end. height 0-1.
float terrainBlendEdge(float keep, float height) {
    if (keep >= 1.0f) return 1.0f;
    return saturate(keep + (height - 0.5f) * TESR_TerrainBlendShape.y * 4.0f * keep * (1.0f - keep));
}

// The blended diffuse colour (as blendDiffuseMaps) and the blended height for terrainBlendEdge (0.5 without a
// height map).
float4 blendDiffuseMapsHeight(float3 vertexColor, float2 uv, int texCount, sampler2D tex[7], float weights[7], float heightStatus[7]) {
    float4 color = 0.0f;
    [unroll] for (int i = 0; i < texCount; i++) {
        float4 texel = tex2D(tex[i], uv);
        color.rgb += texel.rgb * weights[i];
        color.a += (heightStatus[i] > 0.0f ? texel.a : 0.5f) * weights[i];
    }
    color.rgb *= vertexColor;
    return color;
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
    float4 baseHeight = blendDiffuseMapsHeight(IN.vertex_color, offsetUV, texCount, BaseMap, weights, heightStatus);
    float3 baseColor = baseHeight.rgb;
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

    float landAlpha = 1.0f;   // blended over what is behind it while terrain blending runs (alpha only: colour is untouched)
    // At the top level: the screen derivatives of the land's position and view depth (not centroid).
    float3 landPos = IN.lPosition.xyz;
    float landViewDepth = IN.lPosition.w;
    TerrainBlendGap gap = terrainBlendGap(IN.screenPos, landPos, landViewDepth, ddx(landPos), ddy(landPos),
        ddx(landViewDepth), ddy(landViewDepth), IN.viewPosition.xyz, normal);
    [branch] if (TESR_TerrainBlendData.x > 0.0f) {
        // Debug views (DebugView): 1 the blend band magenta, 2 how much land is kept (white: all), 3 how deep the
        // object is below the surface (black at the surface, white at four times BlendDistance; dark blue: nothing
        // behind the land), 4 how deep the fade reaches (white: the full BlendDistance, grey: a shallow object's seam).
        float debugView = TESR_TerrainBlendData.w;
        float keep = getTerrainBlend(gap, landViewDepth, 1.0f - normal.z);
        if (keep >= 0.0f) keep = terrainBlendEdge(keep, baseHeight.a);
        if (debugView > 2.5f) {
            if (gap.covered <= 0.0f) finalColor = float3(0.0f, 0.0f, 0.3f);
            else if (debugView < 3.5f) finalColor = saturate(gap.covered / (TESR_TerrainBlendData.y * 4.0f));
            else finalColor = saturate(terrainBlendDepthRange(gap, 1.0f - normal.z) / TESR_TerrainBlendData.y);
        }
        else if (keep >= 0.0f) {
            if (debugView > 1.5f)
                finalColor = keep;
            else if (debugView > 0.5f)
                finalColor = lerp(float3(1.0f, 0.0f, 1.0f), finalColor, keep);  // the blend band in magenta
            else if (keep < 1.0f) {
                if (TESR_TerrainBlendFrame.y > 0.5f)
                    clip(keep - terrainBlendNoise(IN.screenPos));
                else {
                    clip(keep - 0.004f);   // next to nothing of the land left: not drawn, so it leaves no depth there
                    landAlpha = keep;
                }
            }
        }
    }

    OUT.color_0.a = landAlpha;
    OUT.color_0.rgb = finalColor;

    return OUT;
};

#endif  // Pixel shader.
