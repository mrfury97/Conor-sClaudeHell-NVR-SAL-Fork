// Bounce lighting from light probes (BounceLightingShaders): the probes round a point, and the
// light they say reaches a surface there facing n.
//
// Each probe holds, in two atlas textures (probe (x, y, z) at texel (z * X + x, y)):
//   A: its mean incoming light (linear, the lighting space) x validity, and validity
//   B: a direction factor x validity: the light reaching a surface facing n is mean x (1 + d . n),
//      a first order (L1) spherical harmonic with the cosine lobe folded in, its colour the mean's.
// Validity is 0 for a probe inside a wall (it saw back faces), so it does not count: A and B are
// premultiplied by it, and the blend divides by its own sum. Sampled with the hardware filter
// within a slice (x, y) and blended by hand across slices (z): DirectX 9 cannot render into volume
// textures, so the grid is laid out in 2D.

#ifndef BOUNCE_LIGHTING_HLSL
#define BOUNCE_LIGHTING_HLSL

// grid: xyz the first probe (camera relative). scale: xyz 1 / the spacing on each axis. size: xyz
// probes per axis, w the atlas's width in texels. Returns the light (rgb) and how far it is to be
// trusted (a, 0-1).
float4 ProbeLookup(sampler2D atlasA, sampler2D atlasB, float4 grid, float4 scale, float4 size, float3 pos, float3 n) {
    float3 g = (pos - grid.xyz) * scale.xyz;
    float3 c = clamp(g, 0.0f, size.xyz - 1.0f);
    float outside = length(g - c);   // beyond the grid's edge: faded out over one spacing
    float z0 = floor(c.z);
    float fz = c.z - z0;
    float z1 = min(z0 + 1.0f, size.z - 1.0f);
    float2 inv = 1.0f / float2(size.w, size.y);
    float4 uv0 = float4((float2(z0 * size.x + c.x, c.y) + 0.5f) * inv, 0.0f, 0.0f);
    float4 uv1 = float4((float2(z1 * size.x + c.x, c.y) + 0.5f) * inv, 0.0f, 0.0f);
    float4 a = lerp(tex2Dlod(atlasA, uv0), tex2Dlod(atlasA, uv1), fz);
    float3 b = lerp(tex2Dlod(atlasB, uv0).xyz, tex2Dlod(atlasB, uv1).xyz, fz);
    float valid = max(a.w, 1e-4f);
    float3 light = a.rgb / valid * max(1.0f + dot(b / valid, n), 0.0f);
    return float4(light, saturate(a.w * 4.0f) * saturate(1.0f - outside));
}

#ifndef PROBE_NO_DECLARE
float4 TESR_ProbeGrid : register(c218);       // as ProbeLookup's grid
float4 TESR_ProbeGridSize : register(c219);   // as ProbeLookup's size
float4 TESR_ProbeGridScale : register(c221);  // as ProbeLookup's scale
float4 TESR_ProbeLighting : register(c220);   // x strength (0: off), y the flat ambient kept, z 1: debug view, w intensity
// MUST stay on ONE line each (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_ProbeAtlasA : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_ProbeAtlasB : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

// The pixel's camera relative world position, set by the template before the ambient
// (getAmbientLighting), and whether it is real (a vanilla vertex shader leaves it undefined).
static float3 probeWorldPos = 0.0f;
static float probeWorldPosValid = 0.0f;
static float3 probeDebugLight = 0.0f;   // the debug view's colour (TESR_ProbeLighting.z)
static float probeDebugSet = 0.0f;
#endif

#endif
