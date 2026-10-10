// A light probe's cubemap boiled down (BounceLightingShaders::CaptureProbe), into its texels of the
// two atlases (Includes/BounceLighting.hlsl), both at once (COLOR0 and COLOR1, the cube read once):
//   the colour atlas: the mean incoming light x validity, validity
//   the direction atlas: the direction factor x validity
// A device with one render target draws it twice: mode 1 puts the direction in COLOR0.
// The light reaching a surface facing n from a first order spherical harmonic L(w) = a + b . w is
// E(n) / pi = a + 2/3 b . n, with a the mean and b = 3 x the first moment over the sphere. Its
// direction is taken from the light's brightness (luminance), its colour from the mean: d =
// 2/3 b_lum / a_lum, so the light is mean x (1 + d . n).
// Validity: 0 once more than about a quarter of what the probe saw were back faces -- it is inside
// a wall (ProbeCapture).
//
// The cube is 8 texels a face: each tap reads 2 x 2 at once, on their shared corner, filtered.

float4 TESR_ProbeReduce : register(c0);   // x 0: COLOR0 the colour atlas, 1: COLOR0 the direction atlas (one render target)

// MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString).
samplerCUBE ProbeCube : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

struct PS_INPUT {
    float2 uv : TEXCOORD0;
};

// The direction texCUBE reads face f at (sc, tc) in, as D3D selects faces (as ShadowCubeToAtlas).
float3 FaceDirection(float face, float2 st) {
    if (face < 0.5f) return float3(1.0f, -st.y, -st.x);
    if (face < 1.5f) return float3(-1.0f, -st.y, st.x);
    if (face < 2.5f) return float3(st.x, 1.0f, st.y);
    if (face < 3.5f) return float3(st.x, -1.0f, -st.y);
    if (face < 4.5f) return float3(st.x, -st.y, 1.0f);
    return float3(-st.x, -st.y, -1.0f);
}

struct PS_OUTPUT {
    float4 colour : COLOR0;
    float4 direction : COLOR1;
};

PS_OUTPUT main(PS_INPUT IN) {
    float3 sumLight = 0.0f;
    float3 sumDirection = 0.0f;
    float sumLuminance = 0.0f;
    float sumBack = 0.0f;
    float sumWeight = 0.0f;
    [loop] for (int face = 0; face < 6; face++) {
        [loop] for (int j = 0; j < 4; j++) {
            [loop] for (int i = 0; i < 4; i++) {
                float2 st = float2(i, j) * 0.5f - 0.75f;
                float3 dir = FaceDirection((float)face, st);
                float weight = pow(1.0f + dot(st, st), -1.5f);   // the texels' solid angle
                float4 c = texCUBElod(ProbeCube, float4(dir, 0.0f));
                // The faces were drawn as the lamps' cubes are: the world direction is the cube's
                // with z turned (ShadowManager::RenderShadowCubeMap's faces).
                float3 world = normalize(dir) * float3(1.0f, 1.0f, -1.0f);
                float luminance = dot(c.rgb, float3(0.2126f, 0.7152f, 0.0722f));
                sumLight += c.rgb * weight;
                sumLuminance += luminance * weight;
                sumDirection += world * (luminance * weight);
                sumBack += c.a * weight;
                sumWeight += weight;
            }
        }
    }
    float valid = saturate((0.35f - sumBack / sumWeight) / 0.15f);
    // 2/3 of 3 x the first moment over the mean: 2 x (moment / mean). At most 1: a probe lit from one
    // side only gives nothing on the other, and not less.
    float3 direction = 2.0f * sumDirection / max(sumLuminance, 1e-5f);
    float strength = length(direction);
    direction *= strength > 1.0f ? 1.0f / strength : 1.0f;
    PS_OUTPUT OUT;
    float4 colour = float4(sumLight / sumWeight * valid, valid);
    float4 dir = float4(direction * valid, valid);
    OUT.colour = TESR_ProbeReduce.x < 0.5f ? colour : dir;
    OUT.direction = dir;
    return OUT;
}
