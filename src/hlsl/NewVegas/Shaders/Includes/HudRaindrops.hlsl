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
// The world (World setting), after SSS's wet surfaces (rain_patch_normal.ps): rocks, props, cars and
// NPCs outdoors in the rain get
//   - ripples where the rain lands on flat surfaces, worked out here, not read from a texture: each
//     cell of the surface takes a drop at a random place and time (more of them in heavier rain), which
//     sends out a ring at a steady speed with a crest trailing it, lower as it widens and dying away;
//     the rings of neighbouring cells add up where they cross, and fade where a pixel covers too much
//     of the surface to show them, rather than shimmer
//   - beads of water close by (WorldDroplets), the first person's drops, fading out sooner
//   - characters (skinned meshes: NPCs' bodies, faces, armour and clothes, creatures; and what hangs on a
//     skeleton: held and holstered weapons) get beads and no
//     ripples (rings need standing water, not a shoulder), with their own settings (Npc*: the hooks
//     write theirs into the same constants), the beads fading over the last 40% of their reach
//   - the wet surface's gloss
// Its wetness is the weather's (from the CPU); each pixel looks up whether something is above it in the
// ortho map, a little out from the surface along its normal so a wall's face looks past its own top (as
// SSS does), and it all fades out with distance, where it would only be noise.
//
// Everything follows the mesh's texture coordinates, scaled by how many game units a texture unit
// covers (powers of two) so it keeps one size on every mesh: no extra interpolator (the skin shader
// has none left), and the tilts land in the normal map's own tangent space.
#ifndef HUDRAINDROPS_INCLUDED
#define HUDRAINDROPS_INCLUDED

// Per draw, from the game's shader hooks (NewVegas/Hooks/Shaders.cpp, HudRaindropsEffect::
// SetDrawConstants); all 0 for draws without rain:
//   c218 x the rain now (world draws), y the drops' threshold (Density; 0: no rain on this draw),
//        z the clock, w the drops' refraction
//   c219 x how far the drops tilt the normal (Reflection), y the drops' gloss, z the wet surface's
//        gloss, w a drop texture tile's size in game units (DropSize)
//   c220 x the world's wetness (-1: a first-person draw), y where the world's rain is gone (game units;
//        it fades from 45% of it; negative: no beads), z a ripple tile's size, w 1 for a character (skinned: beads, no ripples)
//   c221-c223 the ortho map's camera (ShadowsExteriorEffect MapOrtho ShadowCameraToLight), its first
//        three columns (orthographic, so w is 1), world draws only. ps_3_0 ends at c223, and the low
//        registers are the game's per-draw values.
float4 HudRainDraw : register(c218);
float4 HudRainLook : register(c219);
float4 HudRainWorld : register(c220);
float4 HudRainOrtho[3] : register(c221);

// MUST stay on ONE line each (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_HudRainTexture : register(s13) < string ResourceName = "Effects\hud_raindrops.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_OrthoMapBuffer : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_HudRainState : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };   // 1 x 1 ("POINT" is a macro in the point-light passes)

#define HUDRAIN_PI 3.14159265f
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

// Ripples: p in ripple cells, time in drop cycles (each cell's drops come once a cycle, out of step
// with its neighbours), rain 0-1. The cell's drop lands at a random place in it (a new place each
// cycle) if this cycle has one, more often in heavier rain; its ring runs out at a steady speed to half
// a cell or a whole one, a crest and one trailing it, falling away as it widens. The pixel takes the
// rings of its own cell and the eight round it, which reach into it. Slope (xy: across the rings) and
// strength (z).
float3 HudRainRipples(float2 p, float time, float rain) {
    float2 cell = floor(p);
    float3 sum = 0.0f;
    [loop] for (int j = -1; j <= 1; j++) {
        [loop] for (int i = -1; i <= 1; i++) {
            float2 c = cell + float2(i, j);
            float t = time + frac(sin(dot(c, float2(127.1f, 311.7f))) * 43758.5453f) * 7.0f;
            float cycle = floor(t);
            float phase = t - cycle;
            float4 h = frac(sin(float4(dot(c, float2(269.5f, 183.3f)), dot(c, float2(419.2f, 371.9f)), dot(c, float2(97.3f, 241.7f)), dot(c, float2(53.1f, 157.9f))) + cycle * float4(1.13f, 2.71f, 3.17f, 0.73f)) * 43758.5453f);
            float2 offset = p - (c + 0.15f + 0.7f * h.xy);
            float dist = length(offset);
            float front = dist - phase * (0.5f + 0.5f * h.w);              // ahead of the ring: positive
            float band = saturate(1.0f - abs(front + 0.1f) / 0.18f);       // the crest and the one behind it
            float amplitude = (h.z < rain * 0.85f ? 1.0f : 0.0f) * (1.0f - phase) * (1.0f - phase);
            float wave = sin(front * 52.36f) * band * amplitude;           // crests 0.12 of a cell apart
            sum += float3(offset / max(dist, 1e-4f) * wave, band * amplitude);
        }
    }
    return sum;
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

// The rain of a world pixel: ripples on flat surfaces (not on characters), beads close by, wet gloss.
void HudRainWorldSurface(float2 uv, float2 uvDx, float2 uvDy, float unitsPerUV, float3 worldPos, float3 worldNormal, float up) {
    float distance = length(worldPos);
    float fadeEnd = abs(HudRainWorld.y);
    float wet = HudRainWorld.x * (1.0f - smoothstep(fadeEnd * 0.45f, fadeEnd, distance));
    [branch] if (wet <= 0.0f) return;
    // Out from the surface along its normal, and a little up: walls look past their own top.
    wet *= HudRainOpenSky(worldPos + float3(worldNormal.xy * HUDRAIN_COVER_OFFSET, 2.0f));
    [branch] if (wet <= 0.0f) return;

    float rain = HudRainDraw.x;
    bool character = HudRainWorld.w > 0.5f;
    float flat = character ? 0.0f : smoothstep(0.65f, 0.85f, worldNormal.z);
    float3 total = 0.0f;

    [branch] if (flat > 0.0f && rain > 0.0f) {
        // Eight ripple cells per ripple tile (RippleSize): a ring reaches half a cell to a whole one.
        float cells = exp2(round(log2(max(unitsPerUV, 1e-3f) / HudRainWorld.z))) * 8.0f;
        // Where a pixel spans much of a crest's spacing (0.12 cell) the rings alias into shimmer: they
        // fade out from 0.05 cell to 0.1. The footprint is the geometric mean of the pixel's two sides,
        // not their sum: seen along the ground one side stretches, and by it the rings were gone a
        // couple of hundred units away (TAA settles what aliasing is left).
        float footprint = sqrt(length(uvDx) * length(uvDy)) * cells;
        float detail = saturate(2.0f - footprint / 0.05f);
        [branch] if (detail > 0.0f)
            total += HudRainRipples(uv * cells, HudRainDraw.z * 1.5f, rain) * (flat * detail * (0.5f + 0.5f * rain));
    }
    [branch] if (HudRainWorld.y > 0.0f) {   // beads close by: the world's out by a third of the way, a character's over the last 40%
        float near = character ? 1.0f - smoothstep(fadeEnd * 0.6f, fadeEnd, distance) : 1.0f - smoothstep(fadeEnd * 0.2f, fadeEnd * 0.35f, distance);
        [branch] if (near > 0.0f) {
            float tiles = exp2(round(log2(max(unitsPerUV, 1e-3f) / HudRainLook.w)));
            float2 p = uv * tiles;
            float3 drops = HudRainLayer(tex2Dgrad(TESR_HudRainTexture, p, uvDx * tiles, uvDy * tiles), 0.2f)
                         + HudRainLayer(tex2Dgrad(TESR_HudRainTexture, (p + float2(0.23f, 0.46f)) * 1.87f, uvDx * tiles * 1.87f, uvDy * tiles * 1.87f), 0.1f);
            drops *= saturate(up + 0.2f) * near;
            hudRainOffset = clamp(drops.xy, -1.0f, 1.0f) * (HudRainDraw.w / tiles);
            hudRainStrength = saturate(drops.z * 2.0f);
            total += drops;
        }
    }

    total *= wet;
    hudRainSlope = clamp(total.xy, -1.0f, 1.0f) * HudRainLook.x;
    hudRainGloss = saturate(wet * HudRainLook.z + total.z * HudRainLook.y);
}

// Call once per pixel at the TOP LEVEL (it takes derivatives), before the material's textures are
// read: they are then read at uv + hudRainOffset. position: any position that moves with the
// surface's pixels (only its derivatives are used, for the scale); worldPos: the pixel's
// camera-relative world position; up: how far the surface faces up (the world's up in tangent space, z).
void HudRaindrops(float2 uv, float3 position, float3 worldPos, float up) {
    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float unitsPerUV = (length(ddx(position)) + length(ddy(position))) / max(length(uvDx) + length(uvDy), 1e-6f);

    // The world's: the surface's own normal from its position, facing the camera.
    float3 worldNormal = normalize(cross(ddx(worldPos), ddy(worldPos)));
    worldNormal *= sign(dot(worldNormal, -worldPos));

    [branch] if (HudRainDraw.y > 0.0f) {
        [branch] if (HudRainWorld.x >= 0.0f) {
            HudRainWorldSurface(uv, uvDx, uvDy, unitsPerUV, worldPos, worldNormal, up);
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
