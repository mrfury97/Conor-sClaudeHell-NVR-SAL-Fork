// Auto exposure: the scene's average brightness, eased toward over time (AutoExposureEffect).
//
// The average is a geometric one (of log brightness), as cameras and engines meter: a small sun or
// lamp in view does not drag the whole frame dark, and a dark corner does not drag it bright. Taken
// over a 24 x 16 grid of filtered taps of the scene (before the tone curve, gamma encoded as the
// tonemapping shaders receive it), the centre counting more (CenterWeight). In three passes:
//   0  into TESR_AutoExposureGrid, 8 x 4: each texel the weighted log brightness of its 3 x 4 taps
//      and their weight. (One pixel taking all 384 taps one after another was slow: a GPU hides a
//      texture read's wait behind other pixels, and there were none.)
//   1  into TESR_AutoExposureNew, 1 x 1: the grid summed (8 bilinear taps, 2 x 2 texels each), eased
//      toward from the previous value, and the scale the tonemapping shaders apply worked out once
//      here rather than per pixel there.
//   2  copied into TESR_AutoExposureBuffer, which pass 1 reads next frame and the tonemapping
//      shaders this one (Includes/Tonemapping.hlsl). 32-bit: at 16 the easing stalled about a third
//      of a stop short at high frame rates, each frame's step below its precision.

float4 TESR_AutoExposureData;   // x 1 on, y target, z min scale, w max scale
float4 TESR_AutoExposureTime;   // x seconds to adapt to a brighter scene, y to a darker one, z centre weight, w this frame's time (s)
float4 TESR_ToneMapping;        // w: the gamma the tonemapping shaders linearize the scene with (Linearization)

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_AutoExposureBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_AutoExposureNew : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_AutoExposureGrid : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

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

static const float2 GRID = float2(24.0f, 16.0f);
static const float2 CELLS = float2(8.0f, 4.0f);   // TESR_AutoExposureGrid's size; GRID / CELLS taps each

float4 MeasureCells(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
    float gamma = TESR_ToneMapping.w > 0.0f ? TESR_ToneMapping.w : 2.2f;
    float logSum = 0.0f;
    float weightSum = 0.0f;
    [unroll] for (int y = 0; y < 4; y++) {
        [unroll] for (int x = 0; x < 3; x++) {
            float2 uv = (floor(vpos) * float2(3.0f, 4.0f) + float2(x, y) + 0.5f) / GRID;
            float3 color = pow(max(tex2Dlod(TESR_RenderedBuffer, float4(uv, 0.0f, 0.0f)).rgb, 0.0f), gamma);
            float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
            float2 fromCentre = (uv - 0.5f) * float2(1.0f, 0.75f);
            float weight = exp(-dot(fromCentre, fromCentre) * TESR_AutoExposureTime.z * 8.0f);
            logSum += log2(clamp(luminance, 1e-4f, 64.0f)) * weight;
            weightSum += weight;
        }
    }
    return float4(logSum, weightSum, 0.0f, 1.0f);
}

float4 Adapt(VSOUT IN) : COLOR0 {
    // Each tap on the corner of 2 x 2 texels: their average (the ratio below does not mind the 1/4).
    float2 sums = 0.0f;
    [unroll] for (int y = 0; y < 2; y++)
        [unroll] for (int x = 0; x < 4; x++)
            sums += tex2Dlod(TESR_AutoExposureGrid, float4(float2(2 * x + 1, 2 * y + 1) / CELLS, 0.0f, 0.0f)).rg;
    float measuredLog = sums.x / max(sums.y, 1e-6f);

    // Eased toward: exponentially, over AdaptBright seconds when the scene got brighter, AdaptDark
    // when it got darker (in log brightness, so a doubling takes as long either way up).
    float previous = tex2Dlod(TESR_AutoExposureBuffer, float4(0.5f, 0.5f, 0.0f, 0.0f)).r;
    float adaptedLog = measuredLog;
    [branch] if (previous > 0.0f && previous < 1e6f) {   // else nothing yet (or garbage): take it as it is
        float previousLog = log2(previous);
        float time = measuredLog > previousLog ? TESR_AutoExposureTime.x : TESR_AutoExposureTime.y;
        float amount = time > 0.0f ? 1.0f - exp(-TESR_AutoExposureTime.w / time) : 1.0f;
        adaptedLog = lerp(previousLog, measuredLog, amount);
    }
    float adapted = exp2(adaptedLog);
    float scale = clamp(TESR_AutoExposureData.y / adapted, TESR_AutoExposureData.z, TESR_AutoExposureData.w);
    return float4(adapted, scale, 0.0f, 1.0f);
}

float4 Copy(VSOUT IN) : COLOR0 {
    return float4(tex2Dlod(TESR_AutoExposureNew, float4(0.5f, 0.5f, 0.0f, 0.0f)).rg, 0.0f, 1.0f);
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 MeasureCells();
    }
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Adapt();
    }
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Copy();
    }
}
