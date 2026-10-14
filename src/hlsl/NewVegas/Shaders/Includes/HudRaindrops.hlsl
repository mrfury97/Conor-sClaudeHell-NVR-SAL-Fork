// Rain on surfaces, drawn in the meshes' own shaders (SkinTemplate, ObjectTemplate, ParallaxTemplate).
//
// First person (the arms, weapon and their gear): Screen Space Shaders' HUD raindrops (STALKER Anomaly,
// Ascii1457: screenspace_hud_raindrops.h, update 17), reworked for NVR:
//   - two layers of a drop texture (Textures\Effects\hud_raindrops.dds, made by
//     resource/Textures/Effects/make_hud_raindrops.py: r height, gb slope, a time offset)
//   - each drop shrinks toward its middle over its life and gives way to the next: its height less
//     a threshold that grows with time (and the Density setting)
//   - refraction by moving the texture coordinates under each drop before the material's textures
//     are read, so the drop magnifies the surface under it; reflection by tilting the normal map's
//     normal along the drop's slope; the wet surface and the drops made glossier
//   - surfaces facing up gather drops, the sides a few, the undersides none
// Its wetness (rain while the player is not under cover, drying after) is one value on the GPU, eased
// every frame by Effects/HudRaindrops.fx.
//
// Characters outdoors (Npc setting): NPCs, creatures and what they carry (skinned meshes, and anything
// hung on a skeleton: held and holstered weapons) get the beads too, with their own settings (Npc*),
// and a wet gloss; not under roofs or bridges (each surface checks the ortho map above it, a little out
// along its normal, as SSS does), fading out with distance. Their wetness is the weather's (from the
// CPU), not whether the player is under cover. The rest of the world is Effects/WetWorld.fx's: ripples,
// puddles and the wet look on surfaces facing up.
//
// Everything follows the mesh's texture coordinates, scaled by how many game units a texture unit
// covers (powers of two) so it keeps one size on every mesh: no extra interpolator (the skin shader
// has none left), and the tilts land in the normal map's own tangent space.
#ifndef HUDRAINDROPS_INCLUDED
#define HUDRAINDROPS_INCLUDED

// Per draw, from the game's shader hooks (NewVegas/Hooks/Shaders.cpp, HudRaindropsEffect::
// SetDrawConstants); all 0 for draws without rain:
//   c218 x unused, y the drops' threshold (Density; 0: no rain on this draw),
//        z the clock, w the drops' refraction
//   c219 x how far the drops tilt the normal (Reflection), y the drops' gloss, z the wet surface's
//        gloss, w a drop texture tile's size in game units (DropSize)
//   c220 x a character's wetness (-1: a first-person draw), y where a character's rain is gone (game
//        units; it fades from 45% of it; negative: no beads), z w unused
//   c221-c223 the ortho map's camera (ShadowsExteriorEffect MapOrtho ShadowCameraToLight), its first
//        three columns (orthographic, so w is 1), character draws only. ps_3_0 ends at c223, and the low
//        registers are the game's per-draw values.
float4 HudRainDraw : register(c218);
float4 HudRainLook : register(c219);
float4 HudRainWorld : register(c220);
float4 HudRainOrtho[3] : register(c221);

// MUST stay on ONE line each (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_HudRainTexture : register(s13) < string ResourceName = "Effects\hud_raindrops.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_OrthoMapBuffer : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_HudRainState : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };   // 1 x 1 ("POINT" is a macro in the point-light passes)

#define HUDRAIN_COVER_OFFSET 12.0f       // how far out from a surface the cover test looks (game units)

static float2 hudRainOffset = 0.0f;   // the texture coordinates' shift under the drops (refraction)
static float2 hudRainSlope = 0.0f;    // the tilt of the normal, tangent space
static float hudRainGloss = 0.0f;     // 0-1: how much glossier the surface is here
static float hudRainStrength = 0.0f;  // 0-1: how much of a drop is here (the parallax shader flattens its relief under it)

// One layer of drops: the drop's slope (xy) and strength (z) at this point of its life.
float3 HudRainLayer(float4 texel, float speed) {
    float2 slope = texel.gb * 2.0f - 1.0f;
    float life = 1.0f - frac(texel.a + HudRainDraw.z * speed);
    float strength = saturate(life * texel.r - HudRainDraw.y);
    return float3(slope * strength, strength);
}

// The ortho map's verdict at a world point: 0 under cover (deeper than the top surface it recorded
// there by more than its slack, Effects/WetWorld.fx's test), 1 open to the sky (outside it too).
float HudRainOpenSky(float3 worldPos) {
    float4 p = float4(worldPos, 1.0f);
    float3 o = float3(dot(p, HudRainOrtho[0]), dot(p, HudRainOrtho[1]), dot(p, HudRainOrtho[2]));
    float2 ouv = float2(o.x * 0.5f + 0.5f, o.y * -0.5f + 0.5f);
    float top = tex2Dlod(TESR_OrthoMapBuffer, float4(ouv, 0.0f, 0.0f)).r;
    return (all(ouv > 0.0f) && all(ouv < 1.0f) && o.z > top + 0.003f) ? 0.0f : 1.0f;
}

// The rain on a character outdoors (an NPC, a creature, what they carry): beads and a wet gloss, out
// of the rain under cover (the ortho map, a little out from the surface along its normal), fading out
// with distance, the beads over the last 40% of the way. The world's surfaces are Effects/WetWorld.fx's.
void HudRainCharacter(float2 uv, float2 uvDx, float2 uvDy, float unitsPerUV, float3 worldPos, float3 worldNormal, float up) {
    float distance = length(worldPos);
    float fadeEnd = abs(HudRainWorld.y);
    float wet = HudRainWorld.x * (1.0f - smoothstep(fadeEnd * 0.45f, fadeEnd, distance));
    [branch] if (wet <= 0.0f) return;
    wet *= HudRainOpenSky(worldPos + float3(worldNormal.xy * HUDRAIN_COVER_OFFSET, 2.0f));
    [branch] if (wet <= 0.0f) return;

    float3 drops = 0.0f;
    [branch] if (HudRainWorld.y > 0.0f) {   // beads (NpcBeads)
        float near = 1.0f - smoothstep(fadeEnd * 0.6f, fadeEnd, distance);
        float tiles = exp2(round(log2(max(unitsPerUV, 1e-3f) / HudRainLook.w)));
        float2 p = uv * tiles;
        drops = HudRainLayer(tex2Dgrad(TESR_HudRainTexture, p, uvDx * tiles, uvDy * tiles), 0.2f)
              + HudRainLayer(tex2Dgrad(TESR_HudRainTexture, (p + float2(0.23f, 0.46f)) * 1.87f, uvDx * tiles * 1.87f, uvDy * tiles * 1.87f), 0.1f);
        drops *= saturate(up + 0.2f) * near * wet;
        drops.xy = clamp(drops.xy, -1.0f, 1.0f);
        hudRainOffset = drops.xy * (HudRainDraw.w / tiles);
        hudRainStrength = saturate(drops.z * 2.0f);
    }
    hudRainSlope = drops.xy * HudRainLook.x;
    hudRainGloss = saturate(wet * HudRainLook.z + drops.z * HudRainLook.y);
}

// Call once per pixel at the TOP LEVEL (it takes derivatives), before the material's textures are
// read: they are then read at uv + hudRainOffset. position: any position that moves with the
// surface's pixels (only its derivatives are used, for the scale); worldPos: the pixel's
// camera-relative world position; up: how far the surface faces up (the world's up in tangent space, z).
void HudRaindrops(float2 uv, float3 position, float3 worldPos, float up) {
    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float unitsPerUV = (length(ddx(position)) + length(ddy(position))) / max(length(uvDx) + length(uvDy), 1e-6f);

    // A character's: the surface's own normal from its position, facing the camera (the cover test).
    float3 worldNormal = normalize(cross(ddx(worldPos), ddy(worldPos)));
    worldNormal *= sign(dot(worldNormal, -worldPos));

    [branch] if (HudRainDraw.y > 0.0f) {
        [branch] if (HudRainWorld.x >= 0.0f) {
            HudRainCharacter(uv, uvDx, uvDy, unitsPerUV, worldPos, worldNormal, up);
        }
        else {
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
}

// The normal map's normal with the rain's tilt.
float3 HudRainNormal(float3 normalTS) {
    return normalize(normalTS + float3(hudRainSlope, 0.0f));
}

// A roughness made wet: water is smooth.
float HudRainRoughness(float roughness) {
    return lerp(roughness, 0.06f, hudRainGloss);
}

#endif
