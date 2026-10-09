// Point light shadows applied to each lamp's own light, in the object shaders (forward), instead of
// darkening the finished frame (Effects/PointShadows, ShadowsInteriors). A lamp's shadow then takes
// away that lamp's light alone -- its colour, its highlight -- and leaves the ambient, glow,
// reflections and the other lamps as they are.
//
// The shadow slots' cubemaps (ShadowManager::RenderShadowCubeMap, ShadowsExteriorEffect) are written,
// face by face, into one 2D atlas, so a pixel shader needs one sampler for all of them: slot s face
// f at tile s * 6 + f (column tile % columns, row tile / columns), each face as texCUBE would
// read it (TESR_PointShadowParams.y tiles to a row: the atlas is sized for LightPoints at startup).
// As exponential shadow maps (Shadows/ShadowCubeToAtlas.pso): exp(k * distance), blurred, so one
// bilinear read gives a soft edge. Each slot holds a cluster of lamps (ShaderManager::
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
float4 TESR_PointShadowParams : register(c214);       // x NearFade: occluders within this many units of the lamp cast less shadow (0 off), y the atlas's tiles per row

// MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString reads the sampler state from it).
// Bilinear: PointShadowVisibility reads the stored distance filtered. (Also, "POINT" is a macro in
// the POINT variants.)
sampler2D TESR_PointShadowAtlas : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

// The exponent k for a slot of radius r (distances are over r): 0.15 per world unit, so a surface 5
// units behind its occluder is half lit and 20 units behind it nearly fully shadowed; at most 80, as
// exp(80 * 1) x 16 must stay a float, so lamps reaching past about 533 units fade in over a longer
// distance behind their occluders (13 and 55 units at a radius of 1500). ShadowManager::
// ConvertCubeFaces uses the same.
float PointShadowExponent(float radius) {
    return min(0.15f * radius, 80.0f);
}

// Where a direction from the anchor reads the atlas, as texCUBE would read face f of slot s: D3D's
// cube face selection and (sc, tc) per face, branchless (the merged lamps' loop runs at the pixel
// shader's register limit), then the tile, kept half a texel inside it so the bilinear read never
// takes a neighbour's texels.
float2 PointShadowAtlasUV(float slot, float3 dir) {
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
    float row = floor(tile / columns);
    float size = TESR_PointShadowData.w;
    float2 inTile = clamp((float2(sc, tc) / abs(major) * 0.5f + 0.5f) * size, 0.5f, size - 0.5f);
    return (float2(tile - row * columns, row) * size + inTile) * TESR_PointShadowData.yz;
}

// How much of a lamp's light reaches a pixel: 1 lit, 0 shadowed. info is slot * 2 + fade (-1: the
// lamp has no slot), anchor the slot's anchor and radius, worldPos the pixel (camera-relative),
// valid 0 where the vertex shader sent no world position. One bilinear read of the exponential
// shadow map: exp(k * occluder) / exp(k * pixel), 1 or more where nothing is in front.
float PointShadowVisibility(float info, float4 anchor, float3 worldPos, float valid) {
    float visibility = 1.0f;
    [branch] if (info >= 0.0f && TESR_PointShadowData.x > 0.0f && valid > 0.0f && anchor.w > 0.0f) {
        float3 toLight = anchor.xyz - worldPos;
        float distance = length(toLight) / anchor.w;
        [branch] if (distance < 1.0f) {   // past the cube's far plane nothing was recorded
            float slot = floor(info * 0.5f);
            float stored = tex2Dlod(TESR_PointShadowAtlas, float4(PointShadowAtlasUV(slot, toLight * float3(-1.0f, -1.0f, 1.0f)), 0.0f, 0.0f)).r;
            float k = PointShadowExponent(anchor.w);
            float blocked = 1.0f - saturate(stored * exp(-k * distance));
            // An occluder within NearFade of the lamp blocks less, nothing at the lamp: its own
            // fixture, someone standing at it. Its distance, from the filtered map: log(stored) / k.
            [flatten] if (TESR_PointShadowParams.x > 0.0f)
                blocked *= saturate(log(max(stored, 1.0f)) / k * anchor.w / TESR_PointShadowParams.x);
            float lit = 1.0f - blocked;
            visibility = lerp(1.0f, lit, info - slot * 2.0f);   // the slot's fade
        }
    }
    return visibility;
}

// The pass's light j (PSLightColor[j]).
float PointShadowBase(int j, float3 worldPos, float valid) {
    float info = TESR_PointShadowBaseInfo[j / 4][j % 4];
    return PointShadowVisibility(info, TESR_PointShadowBase[j], worldPos, valid);
}
