// Decal PS -- vanilla SM3007.pso (BSSM_3XLIGHTING_VcPxoSpc, bullet holes and impact decals),
// plus PBR light/ambient scaling, the hemisphere skylight, and a forward sun shadow.
//
// Pairs with SM3004.vso. Both were vanilla, which is why decals kept flat vanilla lighting
// while the surfaces under them had PBR, skylight and shadows -- a bullet hole on a shadowed
// wall lit as though it were in full sun.
//
// LightData[8] at c21: four (colour, position+radius) pairs, same layout as SM3003.pso. Light
// 0 is the DIRECTIONAL sun -- LightData[1].xyz is a direction, not a position -- gated by
// ToggleNumLights.x, and it is the one that takes the shadow.
//
// SPACES: tangent/binormal/normal and objPos arrive in OBJECT space (SM3004.vso passes the
// vertex attributes through untransformed), and EyePosition is object-space to match. The
// skylight needs a WORLD normal, so it comes from shadowWorldPos via GetShadowGeometricNormal,
// exactly as ObjectTemplate does.
#include "includes/Shadow.hlsl"
#include "includes/Helpers.hlsl"
#include "includes/PBRScale.hlsl"
#include "includes/PointShadow.hlsl"   // each lamp's own shadow (ForwardPointShadows, set per draw by the Lighting30Shader hook)

float4 AmbientColor    : register(c0);
float4 EyePosition     : register(c1);
float4 MatAlpha        : register(c3);
float4 ParallaxData    : register(c7);
float4 ToggleNumLights : register(c20);   // x,y: light counts, z: specular power, w: spec scale
float4 LightData[8]    : register(c21);   // (colour, position+radius) x 4

sampler2D BaseMap   : register(s0);
sampler2D NormalMap : register(s1);
sampler2D HeightMap : register(s4);

struct VS_INPUT {
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float3 color          : COLOR0;
    float3 tangent        : TEXCOORD3_centroid;
    float3 binormal       : TEXCOORD4_centroid;
    float3 normal         : TEXCOORD5_centroid;
    float3 objPos         : TEXCOORD6_centroid;
    float4 fog            : TEXCOORD7_centroid;
    float2 vpos           : VPOS;   // the pixel's screen position (Includes/PointShadow.hlsl, POINT_SHADOW_PIXEL)
};

struct VS_OUTPUT {
    float4 color_0 : COLOR0;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    POINT_SHADOW_PIXEL(IN.vpos);

    float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));

    float3 toEye = EyePosition.xyz - IN.objPos;
    float3 eyeT = normalize(mul(tbn, toEye));

    // ddx/ddy must stay at top level, outside the raymarch below.
    float2 dx = ddx(IN.uv);
    float2 dy = ddy(IN.uv);

    // Parallax occlusion, reproducing vanilla's linear search. The ray starts at height 1 and
    // steps down until it falls below the height field; step sizes come straight from the
    // disassembly, ParallaxData.x scaling depth and .y the uv stride.
    float2 uvStride = eyeT.xy * (0.125f * ParallaxData.y);
    float invZ = 1.0f / (eyeT.z * -(length(eyeT.xy) * ParallaxData.x));
    float2 stepUV = uvStride * invZ;
    float stepZ = eyeT.z * invZ;

    float3 ray = float3(IN.uv, 1.0f);
    float prevDiff = 0.0f;
    float curDiff = 1.0f;

    [loop] for (int p = 0; p < 255; p++) {
        if (curDiff <= 0.0f) break;
        float height = tex2Dgrad(HeightMap, ray.xy, dx, dy).x;
        float diff = ray.z - height;
        ray.xy += stepUV;
        ray.z += stepZ;
        prevDiff = curDiff;
        curDiff = diff;
    }

    // Linear interpolation between the last two samples.
    float2 uv = (curDiff / (prevDiff - curDiff) - 1.0f) * stepUV + ray.xy;

    float4 normalTex = tex2D(NormalMap, uv);
    float3 N = normalize(expand(normalTex.xyz));

    // Applied once to the summed highlight, as vanilla does.
    float specScale = normalTex.w * LightData[1].w;

    // ddx/ddy must stay at top level, outside dynamic flow control.
    float3 shadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
#if FORWARD_SHADOWS
    float sunShadow = SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
                    ? GetSunShadow(IN.shadowWorldPos.xyz, shadowNormal)
                    : 1.0f;
#else
    float sunShadow = 1.0f;
#endif

    bool hasSun = ToggleNumLights.x > 0.0f;
    int numLights = (int)min(ToggleNumLights.x + ToggleNumLights.y, 4.0f);

    float3 diffuse = 0.0f;
    float3 specular = 0.0f;

    [unroll]
    for (int i = 0; i < 4; i++) {
        if (i >= numLights) break;

        float3 lightColor = PBRLight(LightData[i * 2].rgb);
        float4 lightVec   = LightData[i * 2 + 1];

        // Light 0 is the directional sun: lightVec.xyz is a direction and there is no falloff.
        bool isDirectional = (i == 0) && hasSun;

        float3 toLight = isDirectional ? lightVec.xyz : (lightVec.xyz - IN.objPos);
        float3 lightT = isDirectional ? mul(tbn, toLight) : normalize(mul(tbn, toLight));

        // 1 - (dist/radius)^2, clamped. None for the directional light.
        float att = 1.0f;
        if (!isDirectional) {
            float d = saturate(length(toLight) / lightVec.w);
            att = 1.0f - d * d;
        }

        // The shadow scales the sun only; point lights and ambient stay untouched.
        if (isDirectional) att *= sunShadow;
        else att *= PointShadowBaseFacingLamp(i, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);   // each lamp's own shadow (as SM3003.pso)

        float NdotL = max(dot(N, lightT), 0.0f);
        diffuse += lightColor * (NdotL * att);

        float3 halfway = normalize(eyeT + lightT);
        float specStrength = pow(saturate(dot(halfway, N)), ToggleNumLights.z);
        specular += lightColor * (specStrength * ToggleNumLights.w * att);
    }

    // shadowNormal is the geometric WORLD normal; N above is tangent space.
    float3 ambient = PBRAmbient(AmbientColor.rgb) + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);

    // Vanilla: (diffuseSum + AmbientColor) * albedo + specSum * specScale
    float4 baseTex = tex2D(BaseMap, uv);
    float3 albedo = baseTex.rgb * IN.color;
    float3 color = (diffuse + ambient) * albedo + specular * specScale;

    // Decal fog and blend modes, straight from the disassembly. MatAlpha .z and .w select
    // between the fogged, unfogged and whitened variants; .x scales the output alpha.
    float3 fogged   = lerp(color, IN.fog.rgb, IN.fog.w);
    float3 unfogged = color * (1.0f - IN.fog.w);
    float3 blended  = lerp(fogged, unfogged, MatAlpha.z);
    float3 whitened = lerp(color, 1.0f, IN.fog.w);

    OUT.color_0.rgb = lerp(blended, whitened, MatAlpha.w);
    OUT.color_0.a = baseTex.a * MatAlpha.x;

    return OUT;
};
