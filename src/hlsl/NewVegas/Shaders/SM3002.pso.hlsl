// Hair PS -- vanilla SM3002.pso (BSSM_3XLIGHTING_H), PBR diffuse + ambient, forward sun shadow.
//
// The no-highlight sibling of SM3003.pso, which the trace pairs with the same SM3003.vso. The
// vanilla pair differ by 109 instructions, and SM3002 declares neither the AnisoMap sampler at
// s4 nor EyePosition at c1, and reads only ToggleNumLights .x and .y where SM3003 also takes .w
// for specular scale. Interpolators, MatAlpha components, the vertex-colour tint and the texkill
// are identical, so everything except the Marschner lobes carries over unchanged.
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
float4 HairTint        : register(c2);
float4 MatAlpha        : register(c3);
float4 ToggleNumLights : register(c20);   // x,y: light counts. w is specular, unused here.
float4 LightData[8]    : register(c21);   // (colour, position+radius) x 4

sampler2D BaseMap   : register(s0);
sampler2D NormalMap : register(s1);
sampler2D LayerMap  : register(s5);

// Unused by PBRDiffuse, which has no lobe to widen; kept so the call matches SM3003.pso.
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

    float3 N = normalize(expand(tex2D(NormalMap, IN.uv).xyz));

    // LayerMap over BaseMap. Vertex colour .y folds into the tint, not the albedo.
    float4 baseTex  = tex2D(BaseMap,  IN.uv);
    float4 layerTex = tex2D(LayerMap, IN.uv);
    float3 albedo = lerp(baseTex.rgb, layerTex.rgb, layerTex.a);

    //   base = lerp(0.5, HairTint, vertexColour.y), doubled into the albedo tint.
    float3 tintBase = IN.color.y * (HairTint.rgb - 0.5f) + 0.5f;
    float3 tintedAlbedo = albedo * (tintBase * 2.0f);

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

        // Albedo 1: pure lighting term, tinted albedo multiplied in once below. Separable at
        // metallicness 0, where reflectance is a constant 0.04. PBRDiffuse reads neither the eye
        // vector nor roughness, which is why this permutation needs no EyePosition.
        float3 lightDiffuse = PBRDiffuse(0.0f, HAIR_ROUGHNESS, 1.0f, N, N, L, lightColor) * atten;

        // The sun's shadow; each lamp's own (Includes/PointShadow.hlsl): LightData pair i is the
        // pass's light i (Lighting30Shader::SetupGeometryConstants_Lights, 0xBBC050).
        if (isDirectional) lightDiffuse *= sunShadow;
        else lightDiffuse *= PointShadowBaseFacingLamp(i, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);

        // Mask inactive lights.
        diffuse += lightDiffuse * ((i < numLights) ? 1.0f : 0.0f);
    }

    // Vanilla: (diffuseSum + AmbientColor) * (tint * albedo), with no highlight to add.
    // Ambient joins the sum before the albedo multiply, so unlit hair keeps its tint.
    float3 ambient = PBRAmbient(AmbientColor.rgb) + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
    float3 color = (diffuse + ambient) * tintedAlbedo;

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
