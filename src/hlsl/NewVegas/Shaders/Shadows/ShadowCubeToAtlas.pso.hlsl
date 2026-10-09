// One face of a lamp's shadow cubemap into its tile of the point shadow atlas
// (ShadowManager::ConvertCubeFaces, Shaders/Includes/PointShadow.hlsl), as an exponential shadow
// map: exp(k * distance), blurred. Filtered that way, one bilinear read gives a soft edge, where
// the stored distance compared at one point gave a hard, pixelated one.
//
// Each atlas texel reads the cube along its own direction, and the blur's taps around it: past
// the face's edge they continue into the neighbouring face, so the blur crosses the cube's seams.

float4 TESR_CubeToAtlas : register(c0);   // x the face (D3DCUBEMAP_FACES), y the blur step in the face's -1..1 coordinates, z the exponent k

// MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString).
samplerCUBE ShadowCube : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

struct PS_INPUT {
    float2 uv : TEXCOORD0;   // the texel's centre in the face, 0..1
};

// The direction texCUBE reads face f at (sc, tc) in, as D3D selects faces: the inverse of the face
// selection in PointShadow.hlsl (PointShadowAtlasUV).
float3 FaceDirection(float face, float2 st) {
    if (face < 0.5f) return float3(1.0f, -st.y, -st.x);    // +X: sc -z, tc -y
    if (face < 1.5f) return float3(-1.0f, -st.y, st.x);    // -X: sc +z, tc -y
    if (face < 2.5f) return float3(st.x, 1.0f, st.y);      // +Y: sc +x, tc +z
    if (face < 3.5f) return float3(st.x, -1.0f, -st.y);    // -Y: sc +x, tc -z
    if (face < 4.5f) return float3(st.x, -st.y, 1.0f);     // +Z: sc +x, tc -y
    return float3(-st.x, -st.y, -1.0f);                     // -Z: sc -x, tc -y
}

// exp(k * the stored distance); a texel nothing was drawn into (cleared to 1, or 0) reads as far.
float Tap(float2 st) {
    float d = texCUBElod(ShadowCube, float4(FaceDirection(TESR_CubeToAtlas.x, st), 0.0f)).r;
    d = (d > 0.0f && d < 1.0f) ? d : 1.0f;
    return exp(TESR_CubeToAtlas.z * d);
}

float4 main(PS_INPUT IN) : COLOR0 {
    float2 st = IN.uv * 2.0f - 1.0f;
    float s = TESR_CubeToAtlas.y;
    // 3x3 tent (1 2 1), each tap bilinear
    float sum = Tap(st) * 4.0f;
    sum += (Tap(st + float2(s, 0.0f)) + Tap(st - float2(s, 0.0f)) + Tap(st + float2(0.0f, s)) + Tap(st - float2(0.0f, s))) * 2.0f;
    sum += Tap(st + float2(s, s)) + Tap(st - float2(s, s)) + Tap(st + float2(s, -s)) + Tap(st + float2(-s, s));
    return sum / 16.0f;
}
