// A light probe's cubemap (BounceLightingShaders::CaptureProbe): what a surface sends toward the
// probe -- its colour times the light on it from the lamps (the 24 nearest the probes, whatever the
// camera sees: BounceLightingShaders::GatherLamps), through their shadow maps, with the object shaders'
// falloff; plus, for more bounces, the light the probes round it already hold (Bounces). Alpha 1
// for a back face: a probe inside a wall sees them, and is set aside (ProbeReduce).
//
// In the lighting space of the object shaders: linear (colours squared) with LinearLighting, as
// they decode them, else as they come.

#define PROBE_NO_DECLARE
#include "../Includes/BounceLighting.hlsl"

float4 TESR_ShadowData : register(c0);                  // y 1: alpha tested, by the diffuse's alpha
float4 TESR_ProbeCapture : register(c1);                // x 1 linear lighting, y light scale, z bounces x intensity, w 1: a diffuse texture is bound
float4 TESR_CameraPosition : register(c2);
float4 TESR_ShadowCubeMapLightPosition : register(c3);  // xyz the probe (camera relative)
float4 TESR_PointShadowData : register(c4);             // as Includes/PointShadow.hlsl
float4 TESR_PointShadowParams : register(c5);
float4 TESR_InverseSquare : register(c6);               // as Includes/InverseSquare.hlsl
float4 TESR_ProbeGrid : register(c7);
float4 TESR_ProbeGridSize : register(c8);
float4 TESR_ProbeLampPosition[24] : register(c9);     // world position, w radius (0 past the last)
float4 TESR_ProbeLampColor[24] : register(c33);       // rgb colour x dimmer, w shadow info: slot * 2 + fade, -1 none
float4 TESR_ProbeLampAnchor[24] : register(c57);
float4 TESR_ProbeGridScale : register(c81);
float4 TESR_ProbeCaptureDebug : register(c82);          // x 1: every surface white and unlit (BounceLighting Debug 4)      // its slot's anchor (world), w the slot's radius

sampler2D DiffuseMap : register(s0);
// MUST stay on ONE line each (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_PointShadowAtlas : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_ProbeAtlasA : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_ProbeAtlasB : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

struct PS_INPUT {
    float4 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

// The lamps' shadow lookup, as the lamp contact shadows do it (Effects/SunShadows.fx,
// LampCubeVisibility): one bilinear percentage-closer lookup in the point shadow atlas.
void LampAtlasTexel(float slot, float3 dir, out float2 corner, out float2 inTile) {
    float3 a = abs(dir);
    float xMajor = (a.x >= a.y && a.x >= a.z) ? 1.0f : 0.0f;
    float yMajor = (1.0f - xMajor) * (a.y >= a.z ? 1.0f : 0.0f);
    float zMajor = 1.0f - xMajor - yMajor;
    float major = dot(dir, float3(xMajor, yMajor, zMajor));
    float s = major > 0.0f ? 1.0f : -1.0f;
    float sc = xMajor * (-s * dir.z) + yMajor * dir.x + zMajor * (s * dir.x);
    float tc = yMajor * (s * dir.z) - (1.0f - yMajor) * dir.y;
    float tile = slot * 6.0f + yMajor * 2.0f + zMajor * 4.0f + (s > 0.0f ? 0.0f : 1.0f);
    float columns = TESR_PointShadowParams.y;
    float row = floor((tile + 0.5f) / columns);
    float size = TESR_PointShadowData.w;
    inTile = clamp((float2(sc, tc) / abs(major) * 0.5f + 0.5f) * size, 1.0f, size - 1.0f);
    corner = float2(tile - row * columns, row) * size;
}

float LampShadowTexel(float2 c, float distance) {
    float stored = tex2Dlod(TESR_PointShadowAtlas, float4(c * TESR_PointShadowData.yz, 0.0f, 0.0f)).r;
    return stored < distance ? 1.0f : 0.0f;
}

float LampVisibility(float info, float4 anchor, float3 worldPos, float3 normal) {
    float visibility = 1.0f;
    [branch] if (info >= 0.0f && TESR_PointShadowData.x > 0.0f && anchor.w > 0.0f) {
        float3 toLight = anchor.xyz - worldPos;
        float reach = length(toLight);
        float facing = dot(normal, toLight) / reach;
        toLight -= normal * (sign(facing) * TESR_PointShadowParams.z * reach);
        float distance = length(toLight) / anchor.w;
        [branch] if (distance < 1.0f) {
            float slot = floor(info * 0.5f);
            float cosine = max(abs(facing), 0.2f);
            float tilt = min(sqrt(1.0f - cosine * cosine) / cosine, 4.0f);
            float compared = distance * (1.0f - 2.0f / TESR_PointShadowData.w * (0.5f + 1.5f * tilt));
            float2 corner, inTile;
            LampAtlasTexel(slot, toLight * float3(-1.0f, -1.0f, 1.0f), corner, inTile);
            float2 q = corner + inTile;
            float2 b = floor(q - 0.5f) + 0.5f;
            float2 f = q - b;
            float blocked = lerp(lerp(LampShadowTexel(b, compared), LampShadowTexel(b + float2(1.0f, 0.0f), compared), f.x),
                                 lerp(LampShadowTexel(b + float2(0.0f, 1.0f), compared), LampShadowTexel(b + float2(1.0f, 1.0f), compared), f.x), f.y);
            visibility = lerp(1.0f, 1.0f - blocked, info - slot * 2.0f);   // the slot's fade
        }
    }
    return visibility;
}

float4 main(PS_INPUT IN) : COLOR0 {
    float4 albedo = TESR_ProbeCapture.w > 0.5f ? tex2D(DiffuseMap, IN.uv) : float4(0.5f, 0.5f, 0.5f, 1.0f);
    if (TESR_ShadowData.y > 0.5f && albedo.a < 0.2f) discard;

    // The surface's normal: the mesh's, or for a mesh without normals the triangle's, turned to the
    // probe (it cannot then say it is a back face).
    float3 toProbe = TESR_ShadowCubeMapLightPosition.xyz - IN.worldPos.xyz;
    float3 faceted = normalize(cross(ddx(IN.worldPos.xyz), ddy(IN.worldPos.xyz)));
    faceted *= dot(faceted, toProbe) < 0.0f ? -1.0f : 1.0f;
    bool hasNormal = dot(IN.normal, IN.normal) > 0.25f;
    float3 N = hasNormal ? normalize(IN.normal) : faceted;
    if (hasNormal && dot(N, toProbe) < 0.0f) return float4(0.0f, 0.0f, 0.0f, 1.0f);   // a back face
    if (TESR_ProbeCaptureDebug.x > 0.5f) return float4(1.0f, 1.0f, 1.0f, 0.0f);

    bool linearLight = TESR_ProbeCapture.x > 0.5f;
    float3 color = linearLight ? albedo.rgb * albedo.rgb : albedo.rgb;
    float3 worldPos = IN.worldPos.xyz + TESR_CameraPosition.xyz;
    float3 light = 0.0f;
    [loop] for (int i = 0; i < 24; i++) {
        float4 lamp = TESR_ProbeLampPosition[i];
        if (lamp.w <= 0.0f) break;   // filled from the start
        float3 L = lamp.xyz - worldPos;
        float x = dot(L, L) / (lamp.w * lamp.w);
        float NdotL = dot(N, L) * rsqrt(max(dot(L, L), 1e-4f));
        [branch] if (x < 1.0f && NdotL > 0.0f) {
            // The object shaders' falloff: vanilla's 1 - x on the gamma-space colour (squared when
            // decoded), or inverse square (Includes/InverseSquare.hlsl), in linear terms.
            float3 lampColor = TESR_ProbeLampColor[i].rgb;
            float falloff;
            if (linearLight) {
                lampColor *= lampColor;
                float q = TESR_InverseSquare.y;
                falloff = lamp.w < TESR_InverseSquare.x ? max(q / (q + x) - TESR_InverseSquare.z, 0.0f) * TESR_InverseSquare.w : (1.0f - x) * (1.0f - x);
            }
            else
                falloff = 1.0f - x;
            float visibility = LampVisibility(TESR_ProbeLampColor[i].w, TESR_ProbeLampAnchor[i], worldPos, N);
            light += lampColor * (falloff * NdotL * visibility);
        }
    }
    float3 radiance = color * light * TESR_ProbeCapture.y;

    // More bounces: what the probes round it already hold, as the object shaders take it.
    [branch] if (TESR_ProbeCapture.z > 0.0f) {
        float4 held = ProbeLookup(TESR_ProbeAtlasA, TESR_ProbeAtlasB, TESR_ProbeGrid, TESR_ProbeGridScale, TESR_ProbeGridSize, IN.worldPos.xyz, N);
        radiance += color * held.rgb * (held.a * TESR_ProbeCapture.z);
    }
    return float4(radiance, 0.0f);
}
