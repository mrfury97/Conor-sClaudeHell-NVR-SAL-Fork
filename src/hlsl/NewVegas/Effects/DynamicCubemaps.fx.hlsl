// Dynamic cubemaps: an environment cubemap around the camera, built from the screen every frame.
//
// A D3D9 port of Community Shaders' Dynamic Cubemaps (features/Dynamic Cubemaps:
// UpdateCubemapCS.hlsl, InferCubemapCS.hlsl, SpecularIrradianceCS.hlsl; GPL-3.0-or-later; the
// prefilter after Michal Siejak's "Physically Based Rendering", 2017-2018). CS runs compute
// shaders; here each step is a pixel shader pass drawn into one cube face at a time
// (src/effects/DynamicCubemaps.cpp drives them):
//
//   0 Capture   every texel of the capture cube looks along four directions across its footprint;
//               where they are on screen and far enough away, it takes the scene's colour there
//               (filtered, decoded to linear, averaged), blended 50/50 with what it held. Elsewhere it keeps what it held, its
//               coverage decaying. Stored premultiplied by coverage, so the mip chain (built by
//               box-filtering the faces) averages only what was seen.
//   1 Infer     fills what was never seen: coarser and coarser mips until the coverage reaches 1,
//               the rest from the sky (outdoors) or the room's ambient light (indoors).
//   2 Prefilter GGX importance sampling of the inferred cube, one roughness per mip
//               (roughness = mip / 8), with mip-filtered sampling.
//
// The objects' PBR materials sample the result along their reflection at mip roughness x 8
// (Shaders/Includes/Object.hlsl). Everything is camera-centred: one cube for the scene, not one
// per object, as in CS.

#define CUBE_MIPS 9      // DynamicCubemapsEffect::Mips
#define CUBE_SIZE 256.0f // DynamicCubemapsEffect::Size

float4 CubeFace;       // x face (0-5, D3D9 order +X -X +Y -Y +Z -Z), y 1 / face size, z roughness (prefilter), w coverage kept per frame
float4 CubeFallback;   // rgb the room's ambient light (linear), w 1 outdoors
float4 CubeCapture;    // y 1 to ignore what the cube held (reset)

float4 TESR_SkyIrradiance[9];

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
// Bound by DynamicCubemapsEffect, not by name.
samplerCUBE PreviousCube : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
samplerCUBE CaptureCube : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE InferredCube : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// Debug view only (technique Debug).
samplerCUBE EnvCube : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float4 CubeDebug;      // x view (1 panorama, 2 mirror, 3 coverage), y mip of the environment cube

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"

#define PI_F 3.14159265f

struct VSOUT {
    float4 vertPos : POSITION;
    float2 UVCoord : TEXCOORD0;
};

struct VSIN {
    float4 vertPos : POSITION0;
    float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN) {
    VSOUT OUT = (VSOUT)0.0f;
    OUT.vertPos = IN.vertPos;
    OUT.UVCoord = IN.UVCoord;
    return OUT;
}

// The world direction through a point of the current face, in texels (texel = vpos + 0..1 each
// way), inverting D3D9's cube face selection (sc, tc and the major axis per face, as texCUBE uses them).
float3 CubeDirectionAt(float2 texel) {
    float2 st = texel * CubeFace.y;
    float sc = 2.0f * st.x - 1.0f;
    float tc = 2.0f * st.y - 1.0f;
    float face = CubeFace.x;
    float3 d;
    if (face < 0.5f)      d = float3(1.0f, -tc, -sc);
    else if (face < 1.5f) d = float3(-1.0f, -tc, sc);
    else if (face < 2.5f) d = float3(sc, 1.0f, tc);
    else if (face < 3.5f) d = float3(sc, -1.0f, -tc);
    else if (face < 4.5f) d = float3(sc, -tc, 1.0f);
    else                  d = float3(-sc, -tc, -1.0f);
    return normalize(d);
}

// The direction through the centre of the texel at vpos.
float3 CubeDirection(float2 vpos) {
    return CubeDirectionAt(vpos + 0.5f);
}

// --- 0: capture ---------------------------------------------------------------------------------
// Closer than this, a surface is the player or what they hold, not the surroundings (CS: 16.5).
#define CAPTURE_MIN_DISTANCE 24.0f

// One of a texel's four samples: the scene's linear colour along dir, alpha 1 where it was seen.
float4 CaptureSample(float3 dir) {
    float3 view = mul(float4(dir, 0.0f), TESR_ViewTransform).xyz;
    [branch] if (view.z > 0.01f) {
        float3 screen = projectPosition(view);
        [branch] if (all(abs(screen.xy - 0.5f) < 0.5f)) {
            float depth = tex2Dlod(TESR_DepthBuffer, float4(screen.xy, 0.0f, 0.0f)).x * farZ;
            bool viewModel = tex2Dlod(TESR_DepthBufferViewModel, float4(screen.xy, 0.0f, 0.0f)).x > 0.0f;
            [branch] if (depth > CAPTURE_MIN_DISTANCE && !viewModel) {
                float3 color = tex2Dlod(TESR_RenderedBuffer, float4(screen.xy, 0.0f, 0.0f)).rgb;
                color *= color;   // linear: the frame is gamma 2 encoded with LinearLighting, display gamma without
                return float4(min(max(color, 0.0f), 64.0f), 1.0f);   // the sun's disc would otherwise flood whole mips
            }
        }
    }
    return 0.0f;
}

// Four filtered samples per texel on a rotated grid across its footprint: one point sample per
// texel caught or missed thin bright details (railings, string lights) from texel to texel, which
// showed as jagged, shimmering edges in sharp reflections.
float4 Capture(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
    float4 previous = CubeCapture.y > 0.5f ? 0.0f : texCUBElod(PreviousCube, float4(CubeDirection(vpos), 0.0f));

    float4 seen = CaptureSample(CubeDirectionAt(vpos + float2(0.375f, 0.125f)))
                + CaptureSample(CubeDirectionAt(vpos + float2(0.875f, 0.375f)))
                + CaptureSample(CubeDirectionAt(vpos + float2(0.125f, 0.625f)))
                + CaptureSample(CubeDirectionAt(vpos + float2(0.625f, 0.875f)));
    [branch] if (seen.a > 0.0f) {
        // The part of the texel that was seen blends in by its share of the texel.
        return lerp(previous, float4(seen.rgb / seen.a, 1.0f), 0.5f * 0.25f * seen.a);
    }
    return previous * CubeFace.w;
}

// --- 1: infer -----------------------------------------------------------------------------------
// The sky's radiance along a direction, from the SH NVR projects every frame (as
// SkyReflectionRadiance in Shaders/Includes/SkyAmbient.hlsl, sharp), with the ground below the
// horizon.
float3 SkyRadiance(float3 r) {
    float3 radiance = TESR_SkyIrradiance[0].rgb
        + 1.5f * (TESR_SkyIrradiance[1].rgb * r.y + TESR_SkyIrradiance[2].rgb * r.z + TESR_SkyIrradiance[3].rgb * r.x)
        + 4.0f * (TESR_SkyIrradiance[4].rgb * (r.x * r.y) + TESR_SkyIrradiance[5].rgb * (r.y * r.z)
                + TESR_SkyIrradiance[6].rgb * (3.0f * r.z * r.z - 1.0f) + TESR_SkyIrradiance[7].rgb * (r.x * r.z)
                + TESR_SkyIrradiance[8].rgb * (r.x * r.x - r.y * r.y));
    float3 skyOnGround = TESR_SkyIrradiance[0].rgb + TESR_SkyIrradiance[2].rgb + 2.0f * TESR_SkyIrradiance[6].rgb;
    return max(radiance, 0.0f) + max(skyOnGround, 0.0f) * (0.15f * saturate(-r.z));
}

float4 Infer(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
    float3 dir = CubeDirection(vpos);
    float4 sum = texCUBElod(CaptureCube, float4(dir, 0.0f));
    [loop] for (int mip = 1; mip < CUBE_MIPS && sum.a < 1.0f; mip++) {
        float4 coarser = texCUBElod(CaptureCube, float4(dir, (float)mip));
        float missing = 1.0f - sum.a;
        sum += coarser.a > missing ? coarser * (missing / coarser.a) : coarser;
    }

    // Still not covered: outdoors the sky; indoors the average of everything seen in the room
    // (the six faces' last mips, as CS does), or its ambient light before anything was seen.
    float3 fallback;
    [branch] if (CubeFallback.w > 0.5f) {
        fallback = SkyRadiance(dir);
    }
    else {
        float4 room = texCUBElod(CaptureCube, float4(1.0f, 0.0f, 0.0f, CUBE_MIPS - 1))
                    + texCUBElod(CaptureCube, float4(-1.0f, 0.0f, 0.0f, CUBE_MIPS - 1))
                    + texCUBElod(CaptureCube, float4(0.0f, 1.0f, 0.0f, CUBE_MIPS - 1))
                    + texCUBElod(CaptureCube, float4(0.0f, -1.0f, 0.0f, CUBE_MIPS - 1))
                    + texCUBElod(CaptureCube, float4(0.0f, 0.0f, 1.0f, CUBE_MIPS - 1))
                    + texCUBElod(CaptureCube, float4(0.0f, 0.0f, -1.0f, CUBE_MIPS - 1));
        fallback = room.a > 0.05f ? room.rgb / room.a : CubeFallback.rgb;
    }
    return float4(sum.rgb + fallback * saturate(1.0f - sum.a), 1.0f);
}

// --- 2: prefilter -------------------------------------------------------------------------------
#define PREFILTER_SAMPLES 16

// Hammersley points (i / 16, radical inverse of i): ps_3_0 has no integer bit operations.
static const float2 Hammersley[PREFILTER_SAMPLES] = {
    float2(0.0f / 16.0f, 0.0f),     float2(1.0f / 16.0f, 0.5f),     float2(2.0f / 16.0f, 0.25f),    float2(3.0f / 16.0f, 0.75f),
    float2(4.0f / 16.0f, 0.125f),   float2(5.0f / 16.0f, 0.625f),   float2(6.0f / 16.0f, 0.375f),   float2(7.0f / 16.0f, 0.875f),
    float2(8.0f / 16.0f, 0.0625f),  float2(9.0f / 16.0f, 0.5625f),  float2(10.0f / 16.0f, 0.3125f), float2(11.0f / 16.0f, 0.8125f),
    float2(12.0f / 16.0f, 0.1875f), float2(13.0f / 16.0f, 0.6875f), float2(14.0f / 16.0f, 0.4375f), float2(15.0f / 16.0f, 0.9375f)
};

float4 Prefilter(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
    float3 N = CubeDirection(vpos);
    float roughness = max(CubeFace.z, 0.02f);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float3 T = cross(N, float3(0.0f, 1.0f, 0.0f));
    T = dot(T, T) > 1e-5f ? normalize(T) : normalize(cross(N, float3(1.0f, 0.0f, 0.0f)));
    float3 S = cross(N, T);

    // Solid angle of one texel of the inferred cube's top mip (CUBE_SIZE x CUBE_SIZE per face).
    const float texelSolidAngle = 4.0f * PI_F / (6.0f * CUBE_SIZE * CUBE_SIZE);

    float3 color = 0.0f;
    float weight = 0.0f;
    [unroll] for (int i = 0; i < PREFILTER_SAMPLES; i++) {
        float2 u = Hammersley[i];
        float cosTheta = sqrt((1.0f - u.y) / max(1.0f + (alpha2 - 1.0f) * u.y, 1e-6f));
        float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 1e-8f));
        float phi = 2.0f * PI_F * u.x;
        float3 H = S * (sinTheta * cos(phi)) + T * (sinTheta * sin(phi)) + N * cosTheta;
        float3 L = 2.0f * dot(N, H) * H - N;
        float NdotL = dot(N, L);
        if (NdotL > 0.0f) {
            // Mip-filtered importance sampling (GPU Gems 3, ch. 20.4): the sample's solid angle picks the mip.
            float d = cosTheta * cosTheta * (alpha2 - 1.0f) + 1.0f;
            float pdf = alpha2 / (PI_F * d * d) * 0.25f;
            float sampleSolidAngle = 1.0f / (PREFILTER_SAMPLES * pdf);
            float mip = max(0.5f * log2(sampleSolidAngle / texelSolidAngle) + 1.0f, 0.0f);
            color += texCUBElod(InferredCube, float4(L, mip)).rgb * NdotL;
            weight += NdotL;
        }
    }
    return float4(color / max(weight, 1e-4f), 1.0f);
}

// --- debug view ([Shaders.DynamicCubemaps.Main] DebugView) ----------------------------------------
// Drawn over the finished frame, after tonemapping (DynamicCubemapsEffect::RenderDebug).
//   1 panorama: the environment cube unwrapped, longitude across, latitude up (world Z up), at mip y
//   2 mirror:   every surface reflects the cube as a perfect mirror at mip y; the sky shows the cube
//               straight along the view
//   3 coverage: the capture cube's coverage, panorama layout: white is what was seen, black what
//               the infer pass fills from the sky or the room's light
// The cube is linear HDR: shown through x / (1 + x) and gamma 2.
float3 PanoramaDirection(float2 uv) {
    float lon = (uv.x - 0.5f) * 2.0f * PI_F;
    float lat = (0.5f - uv.y) * PI_F;
    return float3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
}

float3 DebugDisplay(float3 c) {
    c = max(c, 0.0f);
    return sqrt(c / (1.0f + c));
}

float4 DebugView(VSOUT IN) : COLOR0 {
    float2 uv = IN.UVCoord;
    // Sampled up front: gradient samples must stay out of flow control.
    float3 normal = normalize(GetWorldNormal(uv));
    float depth = readDepth(uv);
    float3 eye = normalize(toWorld(uv));

    [branch] if (CubeDebug.x > 2.5f) {
        float coverage = saturate(texCUBElod(CaptureCube, float4(PanoramaDirection(uv), 0.0f)).a);
        return float4(coverage.xxx, 1.0f);
    }
    float3 dir;
    if (CubeDebug.x > 1.5f) dir = depth / farZ >= 0.999f ? eye : reflect(eye, normal);
    else dir = PanoramaDirection(uv);
    return float4(DebugDisplay(texCUBElod(EnvCube, float4(dir, CubeDebug.y)).rgb), 1.0f);
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Capture();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Infer();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Prefilter();
    }
}

// After the capture technique: RenderCubemaps selects that one by index 0.
technique Debug {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 DebugView();
    }
}
