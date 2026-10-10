// Point light shadows applied to each lamp's own light, in the object shaders (forward), instead of
// darkening the finished frame (Effects/PointShadows, ShadowsInteriors). A lamp's shadow then takes
// away that lamp's light alone -- its colour, its highlight -- and leaves the ambient, glow,
// reflections and the other lamps as they are.
//
// The shadow slots' cubemaps (ShadowManager::RenderShadowCubeMap, ShadowsExteriorEffect) are written,
// face by face, into one 2D atlas, so a pixel shader needs one sampler for all of them: slot s face
// f at tile s * 6 + f (column tile % columns, row tile / columns), each face as texCUBE would
// read it (TESR_PointShadowParams.y tiles to a row: the atlas is sized for LightPoints at startup).
// Each texel holds the distance to the nearest surface over the slot's radius
// (Shadows/ShadowCubeToAtlas.pso). Each slot holds a cluster of lamps (ShaderManager::
// GetNearbyLights): its cube is drawn from the cluster's anchor, and every lamp of the cluster is
// shadowed through it.
//
// Per draw (NewVegas/Hooks/Shaders.cpp, ForwardPointShadows), for the lights of the game's current
// pass -- PSLightColor[j] is the pass's light j (ShadowLightShader::SetupGeometryOpt_Lights*) -- and
// for the merged lamps: the anchor and radius of the lamp's slot, and slot * 2 + fade (-1 none).

float4 TESR_PointShadowData : register(c189);         // x 1 when on, y 1 / atlas width, z 1 / atlas height, w a face's size in texels
float4 TESR_PointShadowBase[6] : register(c190);      // per draw, the pass's light j: xyz its slot's anchor (camera-relative), w its radius
float4 TESR_PointShadowBaseInfo[2] : register(c196);  // per draw, the pass's light j in component j % 4 of [j / 4]: slot * 2 + fade, -1 none
float4 TESR_PointShadowMerged[16] : register(c198);   // per draw, merged lamp i: as TESR_PointShadowBase; its info in TESR_MergedLightColor[i].w
float4 TESR_PointShadowPCSS : register(c187);         // x PCSS for the soft lookups: 0 off, 1 dithered, 2 smooth; y PCSSLightSize (lamps' radius, units), z PCSSMaxSpread (texels), w this frame's turn of the dithered pattern (0-1)
float4 TESR_PointShadowParams : register(c214);       // x NearFade: occluders within this many units of the lamp cast less shadow (0 off), y the atlas's tiles per row, z ShadowNormalOffset x 2 / face size, w ShadowSoftness (texels)

// MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString reads the sampler state from it).
// Read at texel centres only, where bilinear returns the texel itself. ("POINT" is a macro in the
// POINT variants, so it cannot be named here.)
sampler2D TESR_PointShadowAtlas : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

// The pixel's screen position (VPOS), set first thing in main (POINT_SHADOW_PIXEL): it turns the PCSS
// sample pattern per pixel, and TESR_PointShadowPCSS.w per frame, so the temporal filter averages
// many patterns into a smooth edge.
static float2 pointShadowPixel = 0.0f;
#define POINT_SHADOW_PIXEL(vpos) pointShadowPixel = (vpos)

// Poisson disks in the unit circle: 8 points for the blocker search, 16 for the filter.
static const float2 PointShadowSearchDisk[8] = {
    float2(-0.7534f, -0.3192f), float2( 0.2759f,  0.2351f), float2(-0.6527f, -0.7033f), float2( 0.4300f, -0.3790f),
    float2( 0.6336f,  0.1527f), float2(-0.6513f,  0.7315f), float2( 0.1599f,  0.6291f), float2(-0.1935f, -0.3351f) };
// The smooth filter's 3 x 3 tent: offsets in steps, and weights (1 2 1 x 1 2 1, over 16).
static const float3 PointShadowTent[9] = {
    float3(-1.0f, -1.0f, 0.0625f), float3(0.0f, -1.0f, 0.125f), float3(1.0f, -1.0f, 0.0625f),
    float3(-1.0f,  0.0f, 0.125f),  float3(0.0f,  0.0f, 0.25f),  float3(1.0f,  0.0f, 0.125f),
    float3(-1.0f,  1.0f, 0.0625f), float3(0.0f,  1.0f, 0.125f), float3(1.0f,  1.0f, 0.0625f) };
static const float2 PointShadowFilterDisk[16] = {
    float2(-0.7536f, -0.3193f), float2( 0.7565f, -0.6151f), float2(-0.0753f, -0.7435f), float2( 0.2760f,  0.2351f),
    float2(-0.7327f,  0.3662f), float2(-0.6524f, -0.7033f), float2(-0.3062f,  0.2214f), float2( 0.7799f,  0.6052f),
    float2( 0.3546f, -0.7801f), float2( 0.4299f, -0.3790f), float2(-0.2120f, -0.3352f), float2( 0.6336f,  0.1527f),
    float2(-0.1935f,  0.7977f), float2(-0.6513f,  0.7315f), float2( 0.1599f,  0.6291f), float2( 0.1151f, -0.1128f) };

// Where a direction from the anchor reads the atlas, as texCUBE would read face f of slot s: D3D's
// cube face selection and (sc, tc) per face, branchless (the merged lamps' loop runs at the pixel
// shader's register limit), then the tile: its corner in texels, and the position in it, kept margin
// texels inside it so the filter never reads a neighbour's texels.
void PointShadowAtlasTexel(float slot, float3 dir, float margin, out float2 corner, out float2 inTile) {
    float3 a = abs(dir);
    float xMajor = (a.x >= a.y && a.x >= a.z) ? 1.0f : 0.0f;
    float yMajor = (1.0f - xMajor) * (a.y >= a.z ? 1.0f : 0.0f);
    float zMajor = 1.0f - xMajor - yMajor;
    float major = dot(dir, float3(xMajor, yMajor, zMajor));
    float s = major > 0.0f ? 1.0f : -1.0f;
    float sc = xMajor * (-s * dir.z) + yMajor * dir.x + zMajor * (s * dir.x);   // +X -z, -X +z, +-Y +x, +Z +x, -Z -x
    float tc = yMajor * (s * dir.z) - (1.0f - yMajor) * dir.y;                  // +Y +z, -Y -z, others -y
    float tile = slot * 6.0f + yMajor * 2.0f + zMajor * 4.0f + (s > 0.0f ? 0.0f : 1.0f);
    float columns = TESR_PointShadowParams.y;
    // + 0.5: the divide may be a reciprocal and a multiply, and a tile at a row's start would
    // otherwise floor into the row before, at a column past the last.
    float row = floor((tile + 0.5f) / columns);
    float size = TESR_PointShadowData.w;
    inTile = clamp((float2(sc, tc) / abs(major) * 0.5f + 0.5f) * size, margin, size - margin);
    corner = float2(tile - row * columns, row) * size;
}

// One atlas texel (centre c, in atlas texels) against a pixel at distance (over the radius), less
// bias: 1 blocked, 0 not. An occluder within NearFade of the lamp blocks less, nothing at the lamp:
// its own fixture, someone standing at it.
float PointShadowTexel(sampler2D atlas, float2 invSize, float2 c, float distance, float radius) {
    float stored = tex2Dlod(atlas, float4(c * invSize, 0.0f, 0.0f)).r;
    float blocked = stored < distance ? 1.0f : 0.0f;
    [flatten] if (TESR_PointShadowParams.x > 0.0f)
        blocked *= saturate(stored * radius / TESR_PointShadowParams.x);
    return blocked;
}

// Bilinear percentage-closer filtering at q (atlas texels): the four texels around it, each
// compared, weighted as bilinear filtering weighs them.
float PointShadowPCF(sampler2D atlas, float2 invSize, float2 q, float distance, float radius) {
    float2 b = floor(q - 0.5f) + 0.5f;   // the centre of the texel below and left of q
    float2 f = q - b;
    float b00 = PointShadowTexel(atlas, invSize, b, distance, radius);
    float b10 = PointShadowTexel(atlas, invSize, b + float2(1.0f, 0.0f), distance, radius);
    float b01 = PointShadowTexel(atlas, invSize, b + float2(0.0f, 1.0f), distance, radius);
    float b11 = PointShadowTexel(atlas, invSize, b + float2(1.0f, 1.0f), distance, radius);
    return lerp(lerp(b00, b10, f.x), lerp(b01, b11, f.x), f.y);
}

// Percentage-closer soft shadows: how wide a shadow's edge is follows how far the surface is behind
// what casts it, as a lamp of some size (PCSSLightSize) makes it -- sharp where things touch, wider
// away from them. A blocker search over the widest spread finds the casters' average distance
// (none: lit, and done); the penumbra is the lamp's size x (receiver - caster) / caster, in texels at
// the receiver (texelScale: a texel's width over the distance), from 1 texel to the widest. Then the
// filter at that width:
//   dithered (x 1): 16 taps of a Poisson disk, turned per pixel (and per frame with TAA), each
//     comparing the stored distance read filtered; cheap, and grainy unless TAA averages it.
//   smooth (x 2): a 3 x 3 tent of bilinear percentage-closer lookups (36 texels), its search not
//     turned either: no grain, nothing for TAA to do.
// Distances are over the radius.
float PointShadowPCSS(float2 q, float distance, float tilt, float texelScale, float radius, float maxSpread) {
    bool smooth = TESR_PointShadowPCSS.x > 1.5f;
    float turn = smooth ? 0.0f : 6.2831853f * frac(52.9829189f * frac(dot(pointShadowPixel, float2(0.06711056f, 0.00583715f))) + TESR_PointShadowPCSS.w);
    float2 rot = float2(cos(turn), sin(turn));
    float searchCompared = distance * (1.0f - texelScale * (0.5f + (maxSpread + 1.0f) * tilt));
    float sum = 0.0f, count = 0.0f;
    [loop] for (int i = 0; i < 8; i++) {
        float2 d = PointShadowSearchDisk[i];
        float2 o = float2(d.x * rot.x - d.y * rot.y, d.x * rot.y + d.y * rot.x) * maxSpread;
        float stored = tex2Dlod(TESR_PointShadowAtlas, float4((q + o) * TESR_PointShadowData.yz, 0.0f, 0.0f)).r;
        float found = stored < searchCompared ? 1.0f : 0.0f;
        sum += stored * found;
        count += found;
    }
    float blocked = 0.0f;
    [branch] if (count > 0.0f) {
        float blocker = sum / count;
        float penumbra = TESR_PointShadowPCSS.y * (distance - blocker) / max(blocker, 0.0001f) / (texelScale * distance * radius);
        float spread = clamp(penumbra, 1.0f, maxSpread);
        float compared = distance * (1.0f - texelScale * (0.5f + (spread + 1.0f) * tilt));
        [branch] if (smooth) {
            // Steps of three quarters of the width: the tent then reaches the width plus the
            // bilinear lookups' own texel, and its taps overlap enough not to show as layers.
            float step = max(spread * 0.75f - 0.5f, 0.0f);
            [branch] if (step <= 0.0f)   // every tap on the same point: one lookup is the same
                blocked = PointShadowPCF(TESR_PointShadowAtlas, TESR_PointShadowData.yz, q, compared, radius);
            else {
                [loop] for (int k = 0; k < 9; k++) {
                    float3 t = PointShadowTent[k];
                    blocked += t.z * PointShadowPCF(TESR_PointShadowAtlas, TESR_PointShadowData.yz, q + t.xy * step, compared, radius);
                }
            }
        }
        else {
            [loop] for (int j = 0; j < 16; j++) {
                float2 d = PointShadowFilterDisk[j];
                float2 o = float2(d.x * rot.x - d.y * rot.y, d.x * rot.y + d.y * rot.x) * spread;
                float stored = tex2Dlod(TESR_PointShadowAtlas, float4((q + o) * TESR_PointShadowData.yz, 0.0f, 0.0f)).r;
                float b = stored < compared ? 1.0f : 0.0f;
                [flatten] if (TESR_PointShadowParams.x > 0.0f)
                    b *= saturate(stored * radius / TESR_PointShadowParams.x);
                blocked += b;
            }
            blocked *= 1.0f / 16.0f;
        }
    }
    return blocked;
}

// One bilinear lookup at q, or four spread texels around it (soft).
float PointShadowFiltered(sampler2D atlas, float2 invSize, float2 q, float spread, float distance, float radius, bool soft) {
    float blocked;
    if (soft)
        blocked = 0.25f * (PointShadowPCF(atlas, invSize, q + float2(-spread, -spread), distance, radius)
                         + PointShadowPCF(atlas, invSize, q + float2( spread, -spread), distance, radius)
                         + PointShadowPCF(atlas, invSize, q + float2(-spread,  spread), distance, radius)
                         + PointShadowPCF(atlas, invSize, q + float2( spread,  spread), distance, radius));
    else
        blocked = PointShadowPCF(atlas, invSize, q, distance, radius);
    return blocked;
}

// How much of a lamp's light reaches a pixel: 1 lit, 0 shadowed. info is slot * 2 + fade (-1: the
// lamp has no slot), anchor the slot's anchor and radius, worldPos the pixel (camera-relative),
// normal its surface's (geometric, world), valid 0 where the vertex shader sent no world position.
//
// Percentage-closer filtering: the stored distances around the point are each compared with the
// pixel's, and the results filtered, so the shadow's edge is the share of the texels around it that
// see something nearer. soft: four bilinear lookups ShadowSoftness texels apart (16 texels; a soft
// edge about two texels plus ShadowSoftness wide); otherwise one (4 texels, one texel wide). The
// merged lamps' loops have no room for either: theirs are looked up first and packed (below). (An exponential shadow map came before:
// its comparison saturates, and any offset toward the lamp multiplied it, so its edges came out one
// texel wide and stair-stepped whatever the blur.)
//
// Normal offset (ShadowNormalOffset): the point looked up is pushed off the surface, toward the
// lamp's side of it, by that many shadow map texels at its distance. Slope bias: the texels the
// filter reads lie up to its reach away across the surface, and on a surface tilted from the lamp
// they lie that much nearer: the comparison allows that (tan of the tilt, at most 4) plus half a
// texel. Without them surfaces shadow themselves in bands (acne).
//
// Facing fade: the shadow fades out where the surface turns away from the lamp (full from about 15
// degrees above it). There the texels just past its own outline, seen from the lamp, see whatever
// is behind it: the shadow's edge followed the texel grid. Lambert light is about black there
// anyway; skin, which wraps its light past the terminator, showed it. The surface's own shape does
// that darkening.
#define POINT_SHADOW_BILINEAR 1
#define POINT_SHADOW_SOFT 2
float PointShadowLookup(float info, float4 anchor, float3 worldPos, float3 normal, float valid, int filter) {
    float visibility = 1.0f;
    [branch] if (info >= 0.0f && TESR_PointShadowData.x > 0.0f && valid > 0.0f && anchor.w > 0.0f) {
        float3 toLight = anchor.xyz - worldPos;
        float reach = length(toLight);
        float facing = dot(normal, toLight) / reach;   // the cosine of the lamp's angle from the normal
        toLight -= normal * (sign(facing) * TESR_PointShadowParams.z * reach);
        float distance = length(toLight) / anchor.w;
        // How much of the shadow shows: the slot's fade by the facing fade. Nothing (a surface turned
        // from the lamp, a slot fading in from nothing): the lookup would be discarded, so it is skipped.
        float slot = floor(info * 0.5f);
        float strength = (info - slot * 2.0f) * saturate(facing * 4.0f);
        [branch] if (distance < 1.0f && strength > 0.0f) {   // past the cube's far plane nothing was recorded
            float size = TESR_PointShadowData.w;
            bool soft = filter == POINT_SHADOW_SOFT;
            float cosine = max(abs(facing), 0.2f);
            float tilt = min(sqrt(1.0f - cosine * cosine) / cosine, 4.0f);
            // PCSS (TESR_PointShadowPCSS.x) for the soft lookups: its widest spread decides the margins.
            #ifdef POINT_SHADOW_NO_PCSS
                bool pcss = false;   // no registers to spare (SkinTemplate's four-lamp pass)
            #else
                bool pcss = soft && TESR_PointShadowPCSS.x > 0.0f;
            #endif
            float maxSpread = TESR_PointShadowPCSS.z;
            // The cube's lookup point, distance compared and filter spread.
            float spread = soft ? 0.5f * TESR_PointShadowParams.w : 0.0f;   // the lookups' offset from the point, texels
            // A texel spans 2 / size of the distance (90 degrees over size texels).
            float texelScale = 2.0f / size;
            float compared = distance * (1.0f - texelScale * (0.5f + (spread + 1.5f) * tilt));
            float d = distance;
            float2 corner, inTile;
            PointShadowAtlasTexel(slot, toLight * float3(-1.0f, -1.0f, 1.0f), (pcss ? maxSpread + 1.0f : spread) + 1.0f, corner, inTile);
            float2 q = corner + inTile;

            float blocked;
            [branch] if (pcss)
                blocked = PointShadowPCSS(q, d, tilt, texelScale, anchor.w, maxSpread);
            else
                blocked = PointShadowFiltered(TESR_PointShadowAtlas, TESR_PointShadowData.yz, q, spread, compared, anchor.w, soft);
            visibility = lerp(1.0f, 1.0f - blocked, strength);   // the slot's fade, and the facing fade
        }
    }
    return visibility;
}

// One bilinear lookup.
float PointShadowVisibility(float info, float4 anchor, float3 worldPos, float3 normal, float valid) {
    return PointShadowLookup(info, anchor, worldPos, normal, valid, POINT_SHADOW_BILINEAR);
}

// Four bilinear lookups, ShadowSoftness apart.
float PointShadowVisibilitySoft(float info, float4 anchor, float3 worldPos, float3 normal, float valid) {
    return PointShadowLookup(info, anchor, worldPos, normal, valid, POINT_SHADOW_SOFT);
}

// The pass's light j (PSLightColor[j]).
float PointShadowBase(int j, float3 worldPos, float3 normal, float valid) {
    float info = TESR_PointShadowBaseInfo[j / 4][j % 4];
    #ifdef POINT_SHADOW_BASE_ONE_READ
        return PointShadowVisibility(info, TESR_PointShadowBase[j], worldPos, normal, valid);
    #else
        return PointShadowVisibilitySoft(info, TESR_PointShadowBase[j], worldPos, normal, valid);
    #endif
}

// The pass's light j, for a surface without a usable normal (hair: thin strands, and its shaders
// light in object space). As if the surface faced the lamp: no facing fade, and the normal offset
// pushes the point looked up straight toward the lamp, which keeps strands from shadowing themselves.
float PointShadowBaseFacingLamp(int j, float3 worldPos, float valid) {
    float4 anchor = TESR_PointShadowBase[j];
    float3 towardLamp = normalize(anchor.xyz - worldPos + float3(0.0f, 0.0f, 0.0001f));   // never zero
    float info = TESR_PointShadowBaseInfo[j / 4][j % 4];
    return PointShadowVisibilitySoft(info, anchor, worldPos, towardLamp, valid);
}

// Packed visibilities (POINT_SHADOW_PACKED). The light-only passes light the merged lamps in a loop
// at ps_3_0's 32 temporaries, where a lookup per lamp does not fit: their shadows are looked up first,
// at the top of the shader where nothing else is held, and packed into two float4s -- three values a
// component, how much of each lamp is blocked in 6 bits (0-63). Three make 18 bits, so a word stays
// under 2^18 and word + 0.5 is still exact (four, 24 bits, went past 2^23, where a float holds no
// halves, and the rounding corrupted the lowest value). Values 0-23 (0-11 with POINT_SHADOW_PACK_ONE,
// which keeps one float4 only, for shaders with few values and no registers to spare). The lights
// then only unpack theirs.
#ifdef POINT_SHADOW_PACKED
    static float4 pointShadowPackA = 0.0f;   // components 0-3
    static float4 pointShadowPackB = 0.0f;   // components 4-7

    // 64^k for k = 0..2, exactly (exp2 need not be exact). By ranges, not ==: k comes from v / 3,
    // which need not be exact (with == the compiler folded a 0 here and divided by it).
    float PointShadowPackShift(float k) {
        return k < 0.5f ? 1.0f : (k < 1.5f ? 64.0f : 4096.0f);
    }

    // 1 in the component for packed word c (0-7) of each pack, by ranges for the same reason.
    float4 PointShadowPackMask(float c, float4 components) {
        return (abs(c - components) < 0.5f) ? 1.0f : 0.0f;
    }

    void PointShadowPackValue(float v, float visibility) {
        float component = floor(v / 3.0f + 0.1f);   // + 0.1: v / 3 may come out a hair under a whole number
        float value = round((1.0f - saturate(visibility)) * 63.0f) * PointShadowPackShift(v - component * 3.0f);
        pointShadowPackA += PointShadowPackMask(component, float4(0.0f, 1.0f, 2.0f, 3.0f)) * value;
        #ifndef POINT_SHADOW_PACK_ONE
            pointShadowPackB += PointShadowPackMask(component, float4(4.0f, 5.0f, 6.0f, 7.0f)) * value;
        #endif
    }

    float PointShadowPackedVisibility(float v) {
        float component = floor(v / 3.0f + 0.1f);
        float word = dot(pointShadowPackA, PointShadowPackMask(component, float4(0.0f, 1.0f, 2.0f, 3.0f)));
        #ifndef POINT_SHADOW_PACK_ONE
            word += dot(pointShadowPackB, PointShadowPackMask(component, float4(4.0f, 5.0f, 6.0f, 7.0f)));
        #endif
        // Half a unit up before each floor: the divides may be a reciprocal and a multiply, and a
        // quotient landing a hair under a whole number would floor to the value below.
        float above = floor((word + 0.5f) / PointShadowPackShift(v - component * 3.0f));
        float blocked = above - floor((above + 0.5f) / 64.0f) * 64.0f;   // this value's 6 bits
        return 1.0f - blocked / 63.0f;
    }
#endif
