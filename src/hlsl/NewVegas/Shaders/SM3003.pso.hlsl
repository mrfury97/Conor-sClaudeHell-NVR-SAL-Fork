// Hair PS -- vanilla SM3003.pso, PBR diffuse + ambient, forward sun shadow.
// Specular stays vanilla: Marschner R + TRT via Scheuermann tangent shifts, two AnisoMap
// fetches on (dot(shift,L), dot(shift,H)).
//
// LightData[8] at c21: four (colour, position+radius) pairs. Light 0 is the directional sun,
// gated by ToggleNumLights.x, and takes the shadow.

// Above the global 0: alpha-tested strands self-shadow without an offset. 0 inherits global.
#ifndef SHADOW_NORMAL_BIAS_TEXELS
    #define SHADOW_NORMAL_BIAS_TEXELS 2.5f
#endif
#include "includes/Shadow.hlsl"
#include "includes/Helpers.hlsl"
#include "includes/PBR.hlsl"
#include "includes/PBRScale.hlsl"
#include "includes/PointShadow.hlsl"
#include "includes/InverseSquare.hlsl"

float4 AmbientColor    : register(c0);
float4 EyePosition     : register(c1);
float4 HairTint        : register(c2);
float4 MatAlpha        : register(c3);
float4 ToggleNumLights : register(c20);   // x,y: light counts, w: specular scale
float4 LightData[8]    : register(c21);   // (colour, position+radius) x 4

sampler2D BaseMap   : register(s0);
sampler2D NormalMap : register(s1);
sampler2D AnisoMap  : register(s4);
sampler2D LayerMap  : register(s5);

// Diffuse lobe only; the highlight comes from AnisoMap.
#ifndef HAIR_ROUGHNESS
    #define HAIR_ROUGHNESS 0.6f
#endif

struct VS_INPUT {
    float2 vpos           : VPOS;   // the pixel's screen position (Includes/PointShadow.hlsl, POINT_SHADOW_PIXEL)
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float4 color          : COLOR0;
    float3 tangent        : TEXCOORD3_centroid;
    float3 binormal       : TEXCOORD4_centroid;
    float3 normal         : TEXCOORD5_centroid;
    float3 objPos         : TEXCOORD6_centroid;
    float4 fog            : TEXCOORD7_centroid;
};

struct VS_OUTPUT {
    float4 color_0 : COLOR0;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    POINT_SHADOW_PIXEL(IN.vpos);

    float3 T = normalize(IN.tangent);
    float3 B = normalize(IN.binormal);
    float3 Nw = normalize(IN.normal);
    float3x3 tbn = float3x3(T, B, Nw);

    float4 normalTex = tex2D(NormalMap, IN.uv);
    float3 N = normalize(expand(normalTex.xyz));

    // R and TRT lobes.
    float3 shiftB = normalize(N * 0.5f + float3(0.0f, 0.0f, 1.0f));
    float3 shiftA = normalize(shiftB + float3(-0.05f, 0.0f, 0.0f));

    float3 eyeT = normalize(mul(tbn, EyePosition.xyz - IN.objPos));

    // LayerMap over BaseMap. Vertex colour .y folds into the tint, not the albedo.
    float4 baseTex  = tex2D(BaseMap,  IN.uv);
    float4 layerTex = tex2D(LayerMap, IN.uv);
    float3 albedo = lerp(baseTex.rgb, layerTex.rgb, layerTex.a);

    //   base = lerp(0.5, HairTint, vertexColour.y)
    //   r7 = base * 2          albedo tint, sun R-lobe
    //   r8 = base * 0.3 + 0.2  point-light R-lobe, from base BEFORE the doubling
    float3 tintBase = IN.color.y * (HairTint.rgb - 0.5f) + 0.5f;
    float3 specTintSecondary = tintBase * 0.3f + 0.2f;  // r8 -- point lights
    float3 specTintPrimary   = tintBase * 2.0f;         // r7 -- sun, and the albedo tint
    float3 tintedAlbedo = albedo * specTintPrimary;

    // Applied once to the summed highlight. LightData[1].w: sun specular strength.
    float specScale = normalTex.w * LightData[1].w * IN.color.y;

    // Hair strands are thin, alpha-tested and often near-edge-on or overlapping within a
    // pixel quad, so GetShadowGeometricNormal's ddx/ddy reconstruction reads a different (or
    // near-degenerate) normal every couple of pixels. The self-shadow bias below offsets the
    // shadow-map sample ALONG this normal, so that noise turns directly into per-pixel
    // light/dark jitter -- the splotching. Substitute the view direction instead: it is
    // smooth (no derivatives), still points outward from the strand toward the camera, which
    // is enough to suppress self-shadow acne and give SkyAmbient a stable hemisphere split.
    // worldPos is camera-relative, so -worldPos already points from the surface to the camera.
    float3 shadowNormal = normalize(-IN.shadowWorldPos.xyz);
#if FORWARD_SHADOWS
    float sunShadow = SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
                    ? GetSunShadow(IN.shadowWorldPos.xyz, shadowNormal)
                    : 1.0f;
#else
    float sunShadow = 1.0f;
#endif

    int numLights = (int)min(ToggleNumLights.x + ToggleNumLights.y, 4.0f);

    float3 diffuse = 0.0f;
    float3 specular = 0.0f;

    // .x gates the sun; .y counts point lights 1..3.
    bool hasSun = ToggleNumLights.x > 0.0f;

    [unroll]
    for (int i = 0; i < 4; i++) {
        float3 lightColor = PBRLight(LightData[i * 2].rgb);
        float4 lightVec   = LightData[i * 2 + 1];

        bool isDirectional = (i == 0) && hasSun;

        float3 toLight = isDirectional ? lightVec.xyz : (lightVec.xyz - IN.objPos);

        // 1 - (dist/radius)^2, or inverse square (Includes/InverseSquare.hlsl; this light is linear
        // already). None for directional.
        float dist = length(toLight);
        float d = saturate(dist / max(lightVec.w, 0.0001f));
        float atten = isDirectional ? 1.0f : lampFalloffLinear(d * d, lightVec.w);

        float3 L = normalize(mul(tbn, toLight));
        float3 H = normalize(eyeT + L);

        // tex2Dlod, not tex2D: gradients under flow control are illegal in ps_3_0 (X3528).
        float lobeR   = tex2Dlod(AnisoMap, float4(dot(shiftA, L), dot(shiftA, H), 0.0f, 0.0f)).a;
        float lobeTRT = tex2Dlod(AnisoMap, float4(dot(shiftB, L), dot(shiftB, H), 0.0f, 0.0f)).a;

        float NdotL = saturate(dot(N, L));

        // r7 weights the sun's R lobe, r8 the point lights'. ToggleNumLights.w is sun-only.
        float3 rWeight = isDirectional ? specTintPrimary : specTintSecondary;
        float3 lobe = rWeight * lobeR + lobeTRT * 0.7f;
        float3 lightSpec = lobe * lightColor * NdotL * atten;
        if (isDirectional) lightSpec *= ToggleNumLights.w;

        // Albedo 1: pure lighting term, tinted albedo multiplied in once below. Separable at
        // metallicness 0, where reflectance is a constant 0.04.
        float3 lightDiffuse = PBRDiffuse(0.0f, HAIR_ROUGHNESS, 1.0f, N, eyeT, L, lightColor) * atten;

        // The sun's shadow; each lamp's own (Includes/PointShadow.hlsl): LightData pair i is the
        // pass's light i (Lighting30Shader::SetupGeometryConstants_Lights, 0xBBC050).
        if (isDirectional) {
            lightDiffuse *= sunShadow;
            lightSpec    *= sunShadow;
        }
        else {
            float lampShadow = PointShadowBaseFacingLamp(i, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
            lightDiffuse *= lampShadow;
            lightSpec    *= lampShadow;
        }

        // Mask inactive lights.
        float active = (i < numLights) ? 1.0f : 0.0f;
        diffuse  += lightDiffuse * active;
        specular += lightSpec * active;
    }

    // Vanilla: (diffuseSum + AmbientColor) * (r7 * albedo) + specSum * specScale
    // Ambient joins the sum before the albedo multiply, so unlit hair keeps its tint.
    float3 ambient = PBRAmbient(AmbientColor.rgb) + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
    float3 color = (diffuse + ambient) * tintedAlbedo + specular * specScale;

    // Blend mode: fog / premultiplied / additive, by MatAlpha.z and .w.
    float3 fogged   = lerp(color, IN.fog.rgb, IN.fog.w);
    float3 premult  = color * (1.0f - IN.fog.w);
    float3 additive = lerp(color, 1.0f, IN.fog.w);

    float3 result = lerp(fogged, premult, MatAlpha.z);
    result = lerp(result, additive, MatAlpha.w);

    OUT.color_0.rgb = result;
    OUT.color_0.a = baseTex.a * MatAlpha.x;

    // No texkill. baseTex.a fades smoothly from opaque core to fully transparent at strand
    // tips/edges; clip() at MatAlpha.y's mid-range threshold discarded that entire soft
    // transition, leaving only the fully-opaque core with a hard edge at the threshold contour
    // instead of a feathered fade. Real alpha blending (already active for this pass) handles
    // the full gradient correctly on its own -- a near-zero-alpha pixel contributes ~nothing to
    // the blend, so there's no correctness cost to leaving it in rather than discarding it.

    return OUT;
};
