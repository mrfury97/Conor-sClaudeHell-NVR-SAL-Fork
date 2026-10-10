// One face of a lamp's shadow cubemap into its tile of the point shadow atlas
// (ShadowManager::ConvertCubeFaces, Shaders/Includes/PointShadow.hlsl): the stored distance (over
// the lamp's radius), unchanged. The object shaders filter it themselves, comparing each texel
// around a pixel with the pixel's distance (percentage-closer filtering).

float4 TESR_CubeToAtlas : register(c0);   // x the face (D3DCUBEMAP_FACES)

// MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString).
samplerCUBE ShadowCube : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

struct PS_INPUT {
    float2 uv : TEXCOORD0;   // the texel's centre in the face, 0..1
};

// The direction texCUBE reads face f at (sc, tc) in, as D3D selects faces: the inverse of the face
// selection in PointShadow.hlsl (PointShadowAtlasTexel).
float3 FaceDirection(float face, float2 st) {
    if (face < 0.5f) return float3(1.0f, -st.y, -st.x);    // +X: sc -z, tc -y
    if (face < 1.5f) return float3(-1.0f, -st.y, st.x);    // -X: sc +z, tc -y
    if (face < 2.5f) return float3(st.x, 1.0f, st.y);      // +Y: sc +x, tc +z
    if (face < 3.5f) return float3(st.x, -1.0f, -st.y);    // -Y: sc +x, tc -z
    if (face < 4.5f) return float3(st.x, -st.y, 1.0f);     // +Z: sc +x, tc -y
    return float3(-st.x, -st.y, -1.0f);                     // -Z: sc -x, tc -y
}

float4 main(PS_INPUT IN) : COLOR0 {
    float d = texCUBElod(ShadowCube, float4(FaceDirection(TESR_CubeToAtlas.x, IN.uv * 2.0f - 1.0f), 0.0f)).r;
    return (d > 0.0f && d < 1.0f) ? d : 1.0f;   // a texel nothing was drawn into (cleared to 1, or 0) reads as far
}
