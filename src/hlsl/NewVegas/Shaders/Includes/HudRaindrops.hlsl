// Raindrops on the first-person arms and weapon, drawn in their own shaders (SkinTemplate.hlsl,
// ObjectTemplate.hlsl) so they stay on the surface however the arms move. The technique is Screen
// Space Shaders' HUD raindrops (STALKER Anomaly, Ascii1457: screenspace_hud_raindrops.h, update 17),
// reworked for NVR:
//   - two layers of a drop texture (Textures\Effects\hud_raindrops.dds, made by
//     resource/Textures/Effects/make_hud_raindrops.py: r height, gb slope, a time offset)
//   - each drop shrinks toward its middle over its life and gives way to the next: its height less
//     a threshold that grows with time (and the Density setting)
//   - refraction by moving the texture coordinates under each drop before the material's textures
//     are read, so the drop magnifies the surface under it; reflection by tilting the normal map's
//     normal along the drop's slope; the wet surface and the drops made glossier
//   - surfaces facing up gather drops, the sides a few, the undersides none
// SSS maps the drops on the mesh's position before skinning; here they follow the mesh's texture
// coordinates, which need no extra interpolator (the skin shader's have none left), scaled by how
// many game units a texture unit covers so a drop is about the same size on every mesh.
//
// The wetness (rain while not under cover, drying after) is one value on the GPU, eased every frame
// by Effects/HudRaindrops.fx; the per-draw constant says whether the draw is first-person geometry.
#ifndef HUDRAINDROPS_INCLUDED
#define HUDRAINDROPS_INCLUDED

// Per draw, from the game's shader hooks (NewVegas/Hooks/Shaders.cpp, HudRain::OnDraw): x 1 for
// first-person geometry while the effect runs (0 otherwise, and for every other draw), y the drops'
// threshold (Density), z the drops' clock, w the refraction.
float4 HudRainDraw : register(c218);
// x how far the drops tilt the normal (Reflection), y the drops' gloss, z the wet surface's gloss,
// w a drop texture tile's size in game units (DropSize). Set by the hooks with the above.
float4 HudRainLook : register(c219);

// MUST stay on ONE line each (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_HudRainTexture : register(s13) < string ResourceName = "Effects\hud_raindrops.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_HudRainState : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };   // 1 x 1 ("POINT" is a macro in the point-light passes)

static float2 hudRainOffset = 0.0f;   // the texture coordinates' shift under the drops (refraction)
static float2 hudRainSlope = 0.0f;    // the drops' tilt of the normal, tangent space
static float hudRainGloss = 0.0f;     // 0-1: how much glossier the surface is here
static float hudRainStrength = 0.0f;  // 0-1: how much of a drop is here (the parallax shader flattens its relief under it)

// One layer: the drop's slope (xy) and strength (z) at this point of its life.
float3 HudRainLayer(float4 texel, float speed) {
    float2 slope = texel.gb * 2.0f - 1.0f;
    float life = 1.0f - frac(texel.a + HudRainDraw.z * speed);
    float strength = saturate(life * texel.r - HudRainDraw.y);
    return float3(slope * strength, strength);
}

// Call once per pixel at the TOP LEVEL (it takes derivatives), before the material's textures are
// read: they are then read at uv + hudRainOffset. position: any position that moves with the
// surface's pixels (only its derivatives are used, for the drops' size); up: how far the surface
// faces up (the world's up in tangent space, z).
void HudRaindrops(float2 uv, float3 position, float up) {
    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float unitsPerUV = (length(ddx(position)) + length(ddy(position))) / max(length(uvDx) + length(uvDy), 1e-6f);

    [branch] if (HudRainDraw.x > 0.0f) {
        float wet = tex2Dlod(TESR_HudRainState, float4(0.5f, 0.5f, 0.0f, 0.0f)).r;
        [branch] if (wet > 0.0f) {
            // Drop texture tiles per texture unit, in whole powers of two so the drops keep their
            // size from frame to frame.
            float tiles = exp2(round(log2(max(unitsPerUV, 1e-3f) / HudRainLook.w)));
            float2 p = uv * tiles;
            float2 dx = uvDx * tiles;
            float2 dy = uvDy * tiles;
            float3 drops = HudRainLayer(tex2Dgrad(TESR_HudRainTexture, p, dx, dy), 0.2f)
                         + HudRainLayer(tex2Dgrad(TESR_HudRainTexture, (p + float2(0.23f, 0.46f)) * 1.87f, dx * 1.87f, dy * 1.87f), 0.1f);
            drops *= saturate(up + 0.2f) * wet;
            drops.xy = clamp(drops.xy, -1.0f, 1.0f);

            hudRainOffset = drops.xy * (HudRainDraw.w / tiles);
            hudRainSlope = drops.xy * HudRainLook.x;
            hudRainGloss = saturate(wet * HudRainLook.z + drops.z * HudRainLook.y);
            hudRainStrength = saturate(drops.z * 2.0f);
        }
    }
}

// The normal map's normal with the drops' tilt.
float3 HudRainNormal(float3 normalTS) {
    return normalize(normalTS + float3(hudRainSlope, 0.0f));
}

// A roughness made wet: water is smooth.
float HudRainRoughness(float roughness) {
    return lerp(roughness, 0.06f, hudRainGloss);
}

#endif
