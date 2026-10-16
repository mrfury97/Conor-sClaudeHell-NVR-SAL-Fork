// Screen-space subsurface scattering for skin.
//
// Uses Separable SSS. Copyright (C) 2012 by Jorge Jimenez and Diego Gutierrez. Adapted from
// Community Shaders' SeparableSSS.hlsli and SSSCommon.hlsli (GPL-3.0-or-later) for D3D9 pixel
// shaders; see src/effects/SkinScattering.h for how skin is found without a G-buffer.
//
// Written by the skin shaders (SkinTemplate.hlsl) during their own draws:
//   TESR_SkinScatterBuffer  rgb  the highlights' share of the skin colour (gamma, fogged)
//                           a    the camera distance; a pixel is skin only where this matches
//                                the depth buffer, so anything drawn over the skin is left alone
//   TESR_SkinAlbedoBuffer   rgb  the skin's albedo (gamma)
//                           a    the luminance of the colour the skin drew; a pixel is skin only
//                                where the scene still has it, so hair cards, decals and anything
//                                else drawn over the skin are left alone even without depth.
//                                The effect runs first in the chain so the scene is still as drawn.
//
// What diffuses is light, not the texture: each pixel's colour has its highlights taken out, is
// brought to linear, and divided by albedo^AlbedoDetail; the blurred light is multiplied back by
// the same factor (Community Shaders' pre/post scatter). Dark texels (brows, stubble, lashes)
// would blow up when divided, so they take no part, smoothly, and keep their own colour.
//
// Pass 1 blurs horizontally and writes the blurred light for skin pixels; pass 2 blurs that
// vertically, multiplies the albedo back and adds the highlights. Other pixels pass through.

#define SAMPLES 21   // SkinScatteringEffect::KernelSamples (Community Shaders' SSSS_N_SAMPLES)

float4 TESR_SkinScatterData;                 // x game units per kernel unit, y depth follow, z distance tolerance, w albedo detail
float4 TESR_SkinScatterKernel[SAMPLES];      // rgb weight, a offset; [0] is the centre
float4 TESR_ReciprocalResolution;             // xy 1 / screen size
float4 TESR_SkinScatterDebug;                // x [Shaders.Skin.Debug] DebugView 8-10 as 1-3: the skin test, scene / drawn brightness, what the blur changed;
                                             // y 1 to turn the kernel per pixel (TAA on), z this frame's turn (0-63)

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SkinScatterBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SkinAlbedoBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

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

// The colour test: the scene about as bright as the skin's own pass left it, or brighter by up to
// 60%. Later additive light passes on the same skin only brighten it (and the blur handles them:
// their highlights accumulate in the skin target too); hair, brows, decals and blood drawn over
// the skin darken it, or, where sunlit hair lies over darker skin, brighten it far more.
bool ColourStillSkin(float sceneLuma, float drawnLuma) {
    return sceneLuma > drawnLuma * 0.97f - 0.01f && sceneLuma < drawnLuma * 1.6f + 0.02f;
}

// The distance test: the scene's depth is the skin's, or something sits just in front of it
// (within 1.5 units, about 2 cm). The second case is FaceGen's brow strip, a mostly transparent
// mesh a few millimetres over the forehead that writes depth everywhere; the colour test then
// keeps its transparent part (still skin coloured) and drops the brow hairs. Anything further in
// front (gear, hands) fails here, whatever its colour.
bool DistanceStillSkin(float skinDistance, float sceneDistance) {
    float gap = skinDistance - sceneDistance;
    return abs(gap) < sceneDistance * TESR_SkinScatterData.z + 0.02f || (gap > 0.0f && gap < 1.5f);
}

// Skin where both tests pass.
bool IsSkin(float4 scatter, float drawnLuma, float3 sceneColor, float viewDepth, float2 uv) {
    float distance = viewDepth * length(toWorld(uv));
    return scatter.a > 0.0f
        && DistanceStillSkin(scatter.a, distance)
        && ColourStillSkin(luma(sceneColor), drawnLuma);
}

// The scene is gamma encoded (the skin shaders encode with sqrt); the blur works on light.
float3 Decode(float3 c) { return c * c; }
float3 Encode(float3 c) { return sqrt(max(c, 0.0f)); }

// Dark texels (painted brows, stubble, lashes; linear albedo under about 0.04, gamma 0.2) take no
// part: dividing by them turns any error into a bright smear.
float3 Participation(float3 albedo) { return smoothstep(0.01f, 0.04f, albedo); }
float3 AlbedoFactor(float3 albedo) { return pow(max(albedo, 1e-4f), TESR_SkinScatterData.w); }

// Light per unit (albedo ^ detail), the quantity that diffuses.
float3 ToLight(float3 color, float3 highlights, float3 albedoGamma) {
    float3 albedo = Decode(albedoGamma);
    float3 light = Decode(max(color - highlights, 0.0f)) * Participation(albedo) / AlbedoFactor(albedo);
    return min(light, 16.0f);   // a stray pixel can never flood its neighbours
}

// Pass 1's output can exceed 1; stored compressed, in case the target it is drawn into is 8 bit.
float3 Pack(float3 x) { return x / (1.0f + x); }
float3 Unpack(float3 x) { return x / max(1.0f - x, 1e-4f); }

float4 SkinBlur(float2 uv, float2 dir, bool firstPass) {
    float4 colorM = tex2D(TESR_RenderedBuffer, uv);
    float4 scatterM = tex2D(TESR_SkinScatterBuffer, uv);
    float4 albedoM = tex2D(TESR_SkinAlbedoBuffer, uv);
    float depthM = tex2D(TESR_DepthBuffer, uv).x * farZ;
    // The scene as drawn: pass 1 reads it directly, pass 2 from the copy taken before pass 1.
    float4 sceneM = firstPass ? colorM : tex2D(TESR_SourceBuffer, uv);

    // Debug views (pass 2 only).
    //   1: green skin, red distance mismatch, blue colour mismatch
    //   2: the scene's brightness over the skin pass's own: green equal, red brighter, blue
    //      darker, full colour at 20% either way
    //   3: (at the end) how much the blur changed each pixel
    if (!firstPass && TESR_SkinScatterDebug.x > 0.0f && TESR_SkinScatterDebug.x < 2.5f && scatterM.a > 0.0f) {
        float3 tint;
        if (TESR_SkinScatterDebug.x > 1.5f) {
            float ratio = luma(sceneM.rgb) / max(albedoM.a, 1e-3f);
            tint = ratio < 1.0f ? lerp(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.2f, 1.0f), saturate((1.0f - ratio) * 5.0f))
                                : lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), saturate((ratio - 1.0f) * 5.0f));
        }
        else {
            float distance = depthM * length(toWorld(uv));
            bool distanceOK = DistanceStillSkin(scatterM.a, distance);
            bool colourOK = ColourStillSkin(luma(sceneM.rgb), albedoM.a);
            tint = !distanceOK ? float3(1.0f, 0.0f, 0.0f) : (!colourOK ? float3(0.0f, 0.2f, 1.0f) : float3(0.0f, 1.0f, 0.0f));
        }
        return float4(lerp(sceneM.rgb, tint, 0.6f), sceneM.a);
    }

    if (!IsSkin(scatterM, albedoM.a, sceneM.rgb, depthM, uv)) return colorM;

    float3 lightM = firstPass ? ToLight(colorM.rgb, scatterM.rgb, albedoM.rgb) : Unpack(colorM.rgb);

    // As Community Shaders, the kernel turned by a random angle per pixel (interleaved gradient noise,
    // moving each frame), the same in both passes so they stay at right angles: no axis-aligned rows
    // of taps, only noise for TAA to average. Off without TAA.
    [flatten] if (TESR_SkinScatterDebug.y > 0.5f) {
        float2 pixel = floor(uv / TESR_ReciprocalResolution.xy) + 5.588238f * TESR_SkinScatterDebug.z;
        float angle = frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f)))) * 6.2831853f;
        float c = cos(angle), s = sin(angle);
        dir = float2(dir.x * c - dir.y * s, dir.x * s + dir.y * c);
    }

    // One kernel unit on screen: the projection of TESR_SkinScatterData.x game units at this depth
    // (the same number of pixels across and down, so the turned kernel stays round).
    float2 stepUV = dir * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) * (0.5f * TESR_SkinScatterData.x / max(depthM, 1.0f));

    float3 blurred = lightM * TESR_SkinScatterKernel[0].rgb;
    [unroll]
    for (int i = 1; i < SAMPLES; i++) {
        float4 sampleUV = float4(uv + TESR_SkinScatterKernel[i].a * stepUV, 0.0f, 0.0f);
        float4 color = tex2Dlod(TESR_RenderedBuffer, sampleUV);
        float4 scatter = tex2Dlod(TESR_SkinScatterBuffer, sampleUV);
        float4 albedo = tex2Dlod(TESR_SkinAlbedoBuffer, sampleUV);
        float depth = tex2Dlod(TESR_DepthBuffer, sampleUV).x * farZ;
        float3 scene = firstPass ? color.rgb : tex2Dlod(TESR_SourceBuffer, sampleUV).rgb;

        float3 light = firstPass ? ToLight(color.rgb, scatter.rgb, albedo.rgb) : Unpack(color.rgb);

        // Light does not scatter across a depth jump (Jimenez: lerp back to the centre), nor
        // in from anything that is not skin.
        float follow = saturate(TESR_SkinScatterData.y * TESR_ProjectionTransform[1][1] * abs(depthM - depth));   // x 1 / tan(fov / 2), as CS
        light = lerp(light, lightM, follow * follow);
        light = IsSkin(scatter, albedo.a, scene, depth, sampleUV.xy) ? light : lightM;

        blurred += TESR_SkinScatterKernel[i].rgb * light;
    }

    if (firstPass) return float4(Pack(blurred), colorM.a);

    // Back to colour: the blurred light times the albedo factor, the original where the texture
    // took no part (an exact identity without blur), then the highlights.
    float4 original = sceneM;
    float3 albedo = Decode(albedoM.rgb);
    float3 participation = Participation(albedo);
    float3 light = blurred * AlbedoFactor(albedo) * participation
                 + Decode(max(original.rgb - scatterM.rgb, 0.0f)) * (1.0f - participation * participation);
    float3 result = Encode(light) + scatterM.rgb;

    // View 3, on skin (everything else is left as it is): the change, x8, over the dimmed scene:
    // dark where the blur did nothing, bright where it moved the colour by an eighth or more. Red
    // where it brightened, blue where it darkened.
    if (TESR_SkinScatterDebug.x > 2.5f) {
        float change = luma(result) - luma(original.rgb);
        float3 tint = change > 0.0f ? float3(1.0f, 0.35f, 0.25f) : float3(0.25f, 0.45f, 1.0f);
        float amount = saturate(length(result - original.rgb) * 8.0f);
        return float4(luma(original.rgb) * 0.25f + tint * amount, original.a);
    }
    return float4(result, original.a);
}

float4 BlurHorizontal(VSOUT IN) : COLOR0 {
    return SkinBlur(IN.UVCoord, float2(1.0f, 0.0f), true);
}

float4 BlurVertical(VSOUT IN) : COLOR0 {
    return SkinBlur(IN.UVCoord, float2(0.0f, 1.0f), false);
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 BlurHorizontal();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 BlurVertical();
    }
}
