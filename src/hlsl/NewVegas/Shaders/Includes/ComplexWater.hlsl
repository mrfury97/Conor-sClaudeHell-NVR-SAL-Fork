// Complex Water: the functions ComplexWater.pso.hlsl is built from. See that file for the overview and
// for the constants and samplers these expect to be declared before this is included.

struct PS_INPUT {
    float4 LTEXCOORD_0 : TEXCOORD0_centroid;     // the surface point, camera-relative world position
    float4 LTEXCOORD_1 : TEXCOORD1_centroid;     // the surface point on the water plane, local
    float4 LTEXCOORD_2 : TEXCOORD2_centroid;     // projection to screen texture space, 1st row
    float4 LTEXCOORD_3 : TEXCOORD3_centroid;     // 2nd row
    float4 LTEXCOORD_4 : TEXCOORD4_centroid;     // 3rd row
    float4 LTEXCOORD_5 : TEXCOORD5_centroid;     // 4th row
    float4 LTEXCOORD_6 : TEXCOORD6;              // wading displacement map position
    float2 LTEXCOORD_7 : TEXCOORD7;              // the vanilla wave texture position (unused: every pattern is laid out over the world)
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#include "Includes/PBR.hlsl"

// ---------------------------------------------------------------------------------------------
// Settings (WaterShaders::ReadComplexWater), each kind of water its own: [Shaders.Water.ComplexWater]
// outdoors (and the distant water, and the surface seen from below), [Shaders.Water.Interiors],
// [Shaders.Water.Placed]. The same registers under each kind's own names (TESR_Water*,
// TESR_InteriorWater*, TESR_PlacedWater*, and TESR_BelowWater* for the surface seen from below, a
// copy of the cell's water's: the DLL binds by name), read here as TESR_Water*. Registers clear of the
// engine's water constants (c0-c13), of the template's own (c14-c72), of Shadow.hlsl (c100-c133)
// and of the scene-depth constants below (c192-c201).
//   TESR_WaterLighting     x: SunShadows      y: AbsorptionDepth  z: WaterColorBrightness  w: WaveScattering
//   TESR_WaterLighting2    x: SpecularAA      y: PointLights      z: SunGlitter            w: CrestSharpness
//   TESR_WaterLighting3    x: Foam            y: FoamWidth        z: ShoreFadeWidth        w: ReflectionBlur
//   TESR_WaterLighting4    x: Caustics        y: CausticsScale    z: 1 outdoors, 0 indoors (per frame)  w: RippleSize
//   TESR_WaterScatterColor rgb: ScatterColor, w: 1 when set (else the water form's own colours)
//   TESR_WaterAbsorption   rgb: AbsorptionColor, the absorption rate of each colour  w: ShallowWaves (units)
//   TESR_WaterWaves        x: WaveHeight (units, trough to crest)  y: WaveLength (units, crest to crest)  z: WaveDirection (radians)  w: WaveSteepness
//   TESR_WaterWaves2       x: Whitecaps       y: WaveParallax     z: RefractionBlur        w: RefractionDispersion
//   TESR_WaterLighting5    x: 1 when the game's reflection map is rendered (WaterReflections GameReflections on)  y: FoamScale (units)  z: SkyTint  w: Ripples
// The DLL keeps the ones that must never be 0 (AbsorptionDepth, WaterColorBrightness, FoamWidth,
// CausticsScale) off 0.
// ---------------------------------------------------------------------------------------------
#if WATER_PLACED
float4 TESR_PlacedWaterLighting     : register(c190);
float4 TESR_PlacedWaterLighting2    : register(c191);
float4 TESR_PlacedWaterLighting3    : register(c202);
float4 TESR_PlacedWaterLighting4    : register(c203);
float4 TESR_PlacedWaterScatterColor : register(c204);
float4 TESR_PlacedWaterAbsorption   : register(c205);
float4 TESR_PlacedWaterWaves        : register(c206);
float4 TESR_PlacedWaterWaves2       : register(c207);
float4 TESR_PlacedWaterLighting5    : register(c208);
#define TESR_WaterLighting     TESR_PlacedWaterLighting
#define TESR_WaterLighting2    TESR_PlacedWaterLighting2
#define TESR_WaterLighting3    TESR_PlacedWaterLighting3
#define TESR_WaterLighting4    TESR_PlacedWaterLighting4
#define TESR_WaterScatterColor TESR_PlacedWaterScatterColor
#define TESR_WaterAbsorption   TESR_PlacedWaterAbsorption
#define TESR_WaterWaves        TESR_PlacedWaterWaves
#define TESR_WaterWaves2       TESR_PlacedWaterWaves2
#define TESR_WaterLighting5    TESR_PlacedWaterLighting5
#elif WATER_INTERIOR
float4 TESR_InteriorWaterLighting     : register(c190);
float4 TESR_InteriorWaterLighting2    : register(c191);
float4 TESR_InteriorWaterLighting3    : register(c202);
float4 TESR_InteriorWaterLighting4    : register(c203);
float4 TESR_InteriorWaterScatterColor : register(c204);
float4 TESR_InteriorWaterAbsorption   : register(c205);
float4 TESR_InteriorWaterWaves        : register(c206);
float4 TESR_InteriorWaterWaves2       : register(c207);
float4 TESR_InteriorWaterLighting5    : register(c208);
#define TESR_WaterLighting     TESR_InteriorWaterLighting
#define TESR_WaterLighting2    TESR_InteriorWaterLighting2
#define TESR_WaterLighting3    TESR_InteriorWaterLighting3
#define TESR_WaterLighting4    TESR_InteriorWaterLighting4
#define TESR_WaterScatterColor TESR_InteriorWaterScatterColor
#define TESR_WaterAbsorption   TESR_InteriorWaterAbsorption
#define TESR_WaterWaves        TESR_InteriorWaterWaves
#define TESR_WaterWaves2       TESR_InteriorWaterWaves2
#define TESR_WaterLighting5    TESR_InteriorWaterLighting5
#elif WATER_BELOW
// The surface seen from underwater, one shader for every kind: the cell's water's settings.
float4 TESR_BelowWaterLighting     : register(c190);
float4 TESR_BelowWaterLighting2    : register(c191);
float4 TESR_BelowWaterLighting3    : register(c202);
float4 TESR_BelowWaterLighting4    : register(c203);
float4 TESR_BelowWaterScatterColor : register(c204);
float4 TESR_BelowWaterAbsorption   : register(c205);
float4 TESR_BelowWaterWaves        : register(c206);
float4 TESR_BelowWaterWaves2       : register(c207);
float4 TESR_BelowWaterLighting5    : register(c208);
#define TESR_WaterLighting     TESR_BelowWaterLighting
#define TESR_WaterLighting2    TESR_BelowWaterLighting2
#define TESR_WaterLighting3    TESR_BelowWaterLighting3
#define TESR_WaterLighting4    TESR_BelowWaterLighting4
#define TESR_WaterScatterColor TESR_BelowWaterScatterColor
#define TESR_WaterAbsorption   TESR_BelowWaterAbsorption
#define TESR_WaterWaves        TESR_BelowWaterWaves
#define TESR_WaterWaves2       TESR_BelowWaterWaves2
#define TESR_WaterLighting5    TESR_BelowWaterLighting5
#else
float4 TESR_WaterLighting     : register(c190);
float4 TESR_WaterLighting2    : register(c191);
float4 TESR_WaterLighting3    : register(c202);
float4 TESR_WaterLighting4    : register(c203);
float4 TESR_WaterScatterColor : register(c204);
float4 TESR_WaterAbsorption   : register(c205);
float4 TESR_WaterWaves        : register(c206);
float4 TESR_WaterWaves2       : register(c207);
float4 TESR_WaterLighting5    : register(c208);
#endif
float4 TESR_SkyColor          : register(c209);
float4 TESR_WaterWaveOrigin   : register(c210);   // xy the first wave layer's origin in the world, zw the second's (the DLL turns the waves about the player)

// Water's reflectance looking straight down: 2% (index of refraction 1.33).
#define WATER_F0 0.02f
// The sun glint's roughness on calm water.
#define WATER_ROUGHNESS 0.02f
// Game units in a metre, near enough.
#define WATER_UNITS_PER_METRE 70.0f

// Time. TESR_GameTime.x is the game's clock -- the time of day in game seconds, running at the
// game's timescale (30 by default), stopping when it stops and jumping back to 0 at midnight -- so
// nothing here uses it: every pattern would snap at midnight, and speed up with a timescale mod.
// TESR_GameTime.z is real seconds since the game started. WATER_SCROLL_TIME keeps the speeds the
// scrolling patterns were tuned at (game seconds at the default timescale); the wave shapes move in
// real seconds, at the speed real waves do.
#define WATER_SECONDS     (TESR_GameTime.z)
#define WATER_SCROLL_TIME (TESR_GameTime.z * 30.0f)

// A texture position turned by angle (radians): each wave layer, the foam and the caustics at their own.
float2 rotateWaterUV(float2 uv, float angle){
    float s = sin(angle);
    float c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

// ---------------------------------------------------------------------------------------------
// Screen positions. The vertex shader hands over the projection as four rows; a point on the water
// plane, offset in the plane's own units, projects to the reflection map's texture space. The
// refraction map (the scene behind the water) is the same space flipped vertically.
// ---------------------------------------------------------------------------------------------
float4 getStraightScreenPos(PS_INPUT IN){
    float4 screenPos;
    screenPos.x = dot(IN.LTEXCOORD_2, IN.LTEXCOORD_1);
    screenPos.w = dot(IN.LTEXCOORD_5, IN.LTEXCOORD_1);
    screenPos.y = screenPos.w - dot(IN.LTEXCOORD_3, IN.LTEXCOORD_1);
    screenPos.z = dot(IN.LTEXCOORD_4, IN.LTEXCOORD_1);
    return screenPos;
}

float4 getReflectionScreenPos(PS_INPUT IN, float2 planeOffset){
    float4 planePos = float4(IN.LTEXCOORD_1.xy + planeOffset, IN.LTEXCOORD_1.z, 1.0f);
    return mul(float4x4(IN.LTEXCOORD_2, IN.LTEXCOORD_3, IN.LTEXCOORD_4, IN.LTEXCOORD_5), planePos);
}

float4 flipToRefraction(float4 reflectionPos){
    float4 refractionPos = reflectionPos;
    refractionPos.y = reflectionPos.w - reflectionPos.y;
    return refractionPos;
}

// ---------------------------------------------------------------------------------------------
// The scene behind the water. The world depth buffer, which the DLL resolves just before the water
// is drawn (ShaderRecord::SetCT, for any game shader reading TESR_DepthBufferWorld), still holds
// what lies under and behind the water, so the real depth of water the view passes through is
// known everywhere: the game's own water depth map only grades the first few metres off the shore.
//
// The depth buffer is read against the water's own depth, not the DLL's copy of the camera's near
// plane: the water's vertex shader hands over the projection it was drawn with, so each pixel knows
// exactly the depth value the water itself has there and how far along the view axis it lies. For
// any perspective projection, the depth value's distance from the far end of its range (0 on a
// reversed buffer, 1 on a standard one) falls off as 1/viewZ, with a scale set by the near plane;
// that scale comes from the water itself. A near plane that does not match the one the game drew
// with (a camera mod, a changed fNearDistance) would otherwise scale every depth read by the
// mismatch. Which way the buffer runs also comes from the water: anything past twice the near
// plane has a depth value under 0.5 on a reversed buffer and over 0.5 on a standard one.
//
// Two copies of the depth buffer are read. The game draws the water in pieces, a quad per cell, with
// other geometry between them: TESR_DepthBufferWorld is taken as each piece is drawn, so it holds the
// pier and whatever else came before it, but also the water pieces already drawn;
// TESR_DepthBufferBeforeWater is taken before the first, so it holds no water, but misses what was
// drawn after it. Where the first shows the water surface itself (the same distance as the water
// point it is seen through), the second is used.
// ---------------------------------------------------------------------------------------------
// MUST stay on ONE line each (see Shadow.hlsl's TESR_ShadowAtlas).
sampler2D TESR_DepthBufferWorld : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferBeforeWater : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float4 TESR_CameraData : register(c200);     // y: far (only a small correction)

// The water's view of the screen: how points on the water plane map to screen positions (from how
// the water surface and its own screen position change from one pixel to the next, their
// screen-space derivatives, in the engine's own screen space, whichever way it is laid out), their
// distance along the view axis, and the depth buffer's scale. Build it at the top level (ddx/ddy).
// Exact for every point of the water plane, however far from the pixel: under a perspective
// projection a plane's 1/viewZ, and its position over viewZ, change linearly across the screen, so
// both are carried as they are and the pixel showing any point of the plane is solved for exactly.
// (Carrying the position and viewZ themselves, which are not linear, went wrong for the long offsets
// refraction makes close to the camera, and broke its test for the water itself.)
struct WaterScreenMap {
    float2 surfaceXY;        // the pixel's camera-relative water-plane position
    float2 dirX, dirY;       // that position over viewZ, per pixel across and down
    float2 uvX, uvY;         // screen position, per pixel across and down
    float viewZ;             // the water's distance along the view axis at the pixel
    float invZX, invZY;      // 1 / viewZ, per pixel across and down
    float reversed;          // 1 on a reversed depth buffer
    float depthScale;        // the depth buffer's 1/viewZ scale (the near plane, near enough)
};

// A depth value's distance from the far end of the buffer's range.
float getDepthFromFar(float rawDepth, float reversed){
    return reversed > 0.5f ? rawDepth : 1.0f - rawDepth;
}

float getInvFar(){
    return 1.0f / max(TESR_CameraData.y, 1000.0f);
}

// screenPos: the water's straight screen position (getStraightScreenPos): its z is half the clip
// depth plus half w, w the distance along the view axis.
// Only what reading the depth buffer at the pixel needs (no derivatives, so none of the offsets
// work): for the depth under the pixel before the waves (getShallowWaves), without holding the whole
// map through them -- the wading shader has no temp registers to spare.
WaterScreenMap getWaterScreenDepth(float4 screenPos){
    WaterScreenMap map;
    map.surfaceXY = map.dirX = map.dirY = map.uvX = map.uvY = 0.0f;
    map.invZX = map.invZY = 0.0f;
    map.viewZ = max(screenPos.w, 1e-3f);
    float waterDepth = 2.0f * screenPos.z / map.viewZ - 1.0f;
    map.reversed = waterDepth < 0.5f ? 1.0f : 0.0f;
    map.depthScale = max(getDepthFromFar(waterDepth, map.reversed), 1e-9f) / max(1.0f / map.viewZ - getInvFar(), 1e-9f);
    return map;
}

WaterScreenMap getWaterScreenMap(float3 surfaceFromCamera, float2 uv, float4 screenPos){
    WaterScreenMap map = getWaterScreenDepth(screenPos);
    float invZ = 1.0f / map.viewZ;
    map.surfaceXY = surfaceFromCamera.xy;
    map.dirX = ddx(surfaceFromCamera.xy * invZ);
    map.dirY = ddy(surfaceFromCamera.xy * invZ);
    map.uvX = ddx(uv);
    map.uvY = ddy(uv);
    map.invZX = ddx(invZ);
    map.invZY = ddy(invZ);
    return map;
}

// Screen offset, in pixels across and down, of a water-plane offset (world units, horizontal): the
// pixel s where the plane's position p(s) = dir(s) / invZ(s) reaches q = pixel + offset, both dir and
// invZ linear in s -- dir(0) + s.x dirX + s.y dirY = q (invZ(0) + s.x invZX + s.y invZY), two
// equations in s, solved.
float2 getPixelOffset(WaterScreenMap map, float2 worldOffset){
    float2 q = map.surfaceXY + worldOffset;
    float2 colX = map.dirX - q * map.invZX;
    float2 colY = map.dirY - q * map.invZY;
    float det = colX.x * colY.y - colY.x * colX.y;
    float2 rhs = worldOffset / map.viewZ;
    float2 pixels = float2(rhs.x * colY.y - colY.x * rhs.y, colX.x * rhs.y - rhs.x * colX.y) / det;
    return abs(det) > 1e-20f ? pixels : 0.0f;
}

// Screen offset of a water-plane offset.
float2 getScreenOffset(WaterScreenMap map, float2 worldOffset){
    float2 pixels = getPixelOffset(map, worldOffset);
    return pixels.x * map.uvX + pixels.y * map.uvY;
}

// Distance along the view axis of the water-plane point worldOffset away from the pixel's.
float getWaterViewZ(WaterScreenMap map, float2 worldOffset){
    float2 pixels = getPixelOffset(map, worldOffset);
    float invZ = 1.0f / map.viewZ + pixels.x * map.invZX + pixels.y * map.invZY;
    return max(1.0f / max(invZ, getInvFar() * 0.5f), 1e-3f);
}

// Distance along the view axis of a depth value. Nothing there reads as the far plane.
float getViewZFromDepth(WaterScreenMap map, float rawDepth){
    return 1.0f / (getInvFar() + getDepthFromFar(rawDepth, map.reversed) / map.depthScale);
}

// Whether something at sceneZ along the view axis is the water surface itself, seen through a water
// point waterViewZ along it (the same point of the same plane).
bool isWaterSurface(float sceneZ, float waterViewZ){
    return abs(sceneZ - waterViewZ) < 1.0f + waterViewZ * 1e-3f;
}

// Distance along the view axis of the scene behind the water at uv, seen through a water point
// waterViewZ along the view axis.
float getSceneViewZ(WaterScreenMap map, float2 uv, float waterViewZ){
    float sceneZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x);
    float beforeWaterZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferBeforeWater, float4(uv, 0.0f, 0.0f)).x);
    return isWaterSurface(sceneZ, waterViewZ) ? beforeWaterZ : sceneZ;
}

// What lies behind the water, camera-relative, seen through the water surface point waterPoint
// (camera-relative, waterViewZ along the view axis) at screen position uv: on the line from the
// camera through that point, as far as the depth buffer says. Distance along the view axis grows in
// step with distance along any line from the camera, so no view matrix or projection is needed.
float3 getBedBehind(WaterScreenMap map, float3 waterPoint, float waterViewZ, float2 uv){
    return waterPoint * (getSceneViewZ(map, uv, waterViewZ) / waterViewZ);
}

// x: how far the view travels through the water to what is behind it, y: how far that lies below
// the water surface, both in game units.
float2 getWaterPathTo(float3 bed, float3 waterPoint){
    return float2(max(length(bed) - length(waterPoint), 0.0f), max(waterPoint.z - bed.z, 0.0f));
}


// ---------------------------------------------------------------------------------------------
// DebugView: one term on its own, in place of the water. Compiled in (WATER_DEBUG_VIEW, set by the
// DLL from DebugView when the shaders load: restart the game to change it), so it costs the water
// nothing when off; when on, the shader stops at that term and shows it.
//   1 sun shadow on the surface (black in shadow)   2 what still shows through the water, per colour
//   3 reflection amount                              4 wave scattering
//   5 glint roughness (black calm, white widened)    6 point-light glints
//   7 path through the water, black 0 to white 20 m  8 depth below the surface, black 0 to white 20 m
//   9 foam                                           10 shoreline fade (black see-through, white solid)
//  11 caustics                                        12 wave height (black trough, white crest)
//  13 depth calibration: mid-grey where the game's near plane matches the DLL's, brighter where the
//     game's is further out (depth used to read too shallow), darker where nearer (too deep)
//  14 depth below the surface, black 0 to white 100 m, red where the depth buffer holds nothing
//     behind the water
//  15 the two depth copies (0 to 20 m each): red the depth taken as the water is drawn, green the
//     depth taken before any water; blue where the first shows the water itself (the second is
//     used there). Yellow-grey: both agree. Green: something drawn in between is in front (the pier,
//     or other water at another height)
//  16 shallow water (ShallowWaves): how much of the waves are left, black calm at the shore to white
//     open water
// ---------------------------------------------------------------------------------------------
#ifndef WATER_DEBUG_VIEW
    #define WATER_DEBUG_VIEW 0
#endif
#define WATER_DEBUG(view, value) if (WATER_DEBUG_VIEW == (view)) { OUT.color_0 = float4((float3)(value), 1.0f); return OUT; }

// For DebugView 15, under the pixel: x the depth below the surface by TESR_DepthBufferWorld, y by
// TESR_DepthBufferBeforeWater, z 1 where the first shows the water surface itself.
float3 getDepthCopies(WaterScreenMap map, float3 surfaceFromCamera, float2 uv){
    float sceneZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x);
    float beforeWaterZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferBeforeWater, float4(uv, 0.0f, 0.0f)).x);
    float depth = max(surfaceFromCamera.z * (1.0f - sceneZ / map.viewZ), 0.0f);
    float beforeWaterDepth = max(surfaceFromCamera.z * (1.0f - beforeWaterZ / map.viewZ), 0.0f);
    return float3(depth, beforeWaterDepth, isWaterSurface(sceneZ, map.viewZ) ? 1.0f : 0.0f);
}

// For DebugView 14: 1 where the depth buffer holds nothing at uv (still as cleared: the far end of its range).
float getSceneEmpty(WaterScreenMap map, float2 uv){
    float rawDepth = tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x;
    return getDepthFromFar(rawDepth, map.reversed) <= 1e-7f ? 1.0f : 0.0f;
}

// For DebugView 13: the depth buffer's near-plane scale against the DLL's own near plane.
float getDepthCalibration(WaterScreenMap map){
    return map.depthScale / max(TESR_CameraData.x, 1e-3f);
}

// ---------------------------------------------------------------------------------------------
// Refraction, as real water bends light. The view ray is refracted through the wave normal at
// water's index of refraction (Snell's law, 1.33) and followed down to the bed: first to the depth
// of the bed under the pixel, then once more to the depth of whatever the bent ray actually lands
// on, so the offset comes out of the real geometry -- small over a shallow bed, large over a deep
// one, the bed raised and squeezed at low angles as it is through real water. The pixel that shows
// that point of the bed is where the line from the camera to it crosses the water surface; its
// screen position comes from the water's own screen map. strength: the water type's
// refractionPower, 1 real water. Where the bent ray lands on something in front of the water (a
// post, legs, the shore), the straight view is used, so nothing above the water smears into it; as
// it nears the edge of the screen the bend fades out into the straight view, rather than snapping to
// it where it leaves the screen (a hard edge that followed the ripples, close to the camera). Returns
// the screen position of the bed seen; path and bed: for that bed.
// straightBed: the bed under the pixel (getBedBehind at straightUV), which the caller has already
// read. Each depth read is made once: the second pass's is also the leak test's and the bed's.
// ---------------------------------------------------------------------------------------------
float2 getRefraction(float3 surfaceFromCamera, float3 N, float2 straightUV, WaterScreenMap map, float3 straightBed, float strength, out float2 path, out float3 bed){
    float3 incident = normalize(surfaceFromCamera);
    float3 bent = refract(incident, N, 1.0f / 1.33f);
    bent = normalize(lerp(incident, dot(bent, bent) > 0.0f ? bent : incident, strength));
    // The water plane, seen from the camera: only meaningful with the camera above it.
    float planeZ = min(surfaceFromCamera.z, -1e-3f);

    float depth = max(surfaceFromCamera.z - straightBed.z, 0.0f);
    float2 uv = straightUV;
    float3 waterPoint = surfaceFromCamera;
    float waterViewZ = map.viewZ;
    float sceneZ = map.viewZ;
    [unroll]
    for (int i = 0; i < 2; i++) {
        float3 target = surfaceFromCamera + bent * (depth / max(-bent.z, 0.1f));
        waterPoint = float3(target.xy * (planeZ / min(target.z, planeZ)), surfaceFromCamera.z);
        float2 offset = waterPoint.xy - surfaceFromCamera.xy;
        uv = straightUV + getScreenOffset(map, offset);
        waterViewZ = getWaterViewZ(map, offset);
        sceneZ = getSceneViewZ(map, uv, waterViewZ);
        depth = max(surfaceFromCamera.z - waterPoint.z * (sceneZ / waterViewZ), 0.0f);   // getBedBehind's
    }

    // How much of the bend is kept: none onto something in front of the water, and fading out over
    // the last 3% of the screen to its edge (none past it).
    float2 edge = min(uv, 1.0f - uv);
    float keep = sceneZ < waterViewZ ? 0.0f : saturate(min(edge.x, edge.y) / 0.03f);
    float3 refractedBed = waterPoint * (sceneZ / waterViewZ);
    uv = lerp(straightUV, uv, keep);
    waterPoint = lerp(surfaceFromCamera, waterPoint, keep);
    bed = lerp(straightBed, refractedBed, keep);
    path = getWaterPathTo(bed, waterPoint);
    return uv;
}

// The bed as seen through the water. Blurred the more water it is seen through (RefractionBlur),
// as fine particles in the water soften what lies deep; and split slightly by colour along the bend
// (RefractionDispersion), since water bends red light a little less than blue. Linear.
// tex2Dlod: the refraction map has no mipmaps, and the lookup follows the depth buffer.
// The split (RefractionDispersion 0) and the blur (under about half a pixel: the shallows, or
// RefractionBlur 0) are skipped when they would change nothing: one read instead of seven.
float3 getRefractedBed(float2 uv, float2 straightUV, float pathLength){
    float2 bend = uv - straightUV;
    float dispersion = TESR_WaterWaves2.w * 0.5f;
    float3 center = tex2Dlod(RefractionMap, float4(uv, 0.0f, 0.0f)).rgb;
    float3 bed = center;
    [branch] if (dispersion > 0.0f) {
        bed.r = tex2Dlod(RefractionMap, float4(straightUV + bend * (1.0f - dispersion), 0.0f, 0.0f)).r;
        bed.b = tex2Dlod(RefractionMap, float4(straightUV + bend * (1.0f + dispersion), 0.0f, 0.0f)).b;
    }

    float radius = TESR_WaterWaves2.z * saturate(pathLength / (10.0f * WATER_UNITS_PER_METRE)) * 0.006f;
    float3 blurred = bed + center * 4.0f;
    [branch] if (radius > 0.0002f) {
        blurred = bed;
        blurred += tex2Dlod(RefractionMap, float4(uv + float2( radius, 0.0f), 0.0f, 0.0f)).rgb;
        blurred += tex2Dlod(RefractionMap, float4(uv + float2(-radius, 0.0f), 0.0f, 0.0f)).rgb;
        blurred += tex2Dlod(RefractionMap, float4(uv + float2(0.0f,  radius), 0.0f, 0.0f)).rgb;
        blurred += tex2Dlod(RefractionMap, float4(uv + float2(0.0f, -radius), 0.0f, 0.0f)).rgb;
    }
    return linearize(float4(blurred * 0.2f, 1.0f)).rgb;
}

// ---------------------------------------------------------------------------------------------
// Ripples (Ripples, RippleSize). One normal texture (water_NRM, watercalm_NRM for placed water) in
// four layers over the world, each turned to its own angle so the texture repeat never lines up
// across the water: a broad swell, large and medium ripples, and fine ones that fade out with
// distance, where they would only shimmer. A very large, slow pattern varies their strength into
// rougher and calmer patches. Sized from WaveLength (RippleSize times it), so they keep to the waves
// they ride on; every layer travels downwind, each a little to one side of it, at the speed a water
// wave its size moves (about eight of its ripples to a texture tile): ripples running every which
// way across each other stand and wobble in place, like jelly. In world space, so its slopes add to
// the wave field's as they are, on placed water turned against the world too. The water forms'
// choppiness, waveWidth and waveSpeed are not used. tex2D: top level only.
// ---------------------------------------------------------------------------------------------
float3 getWaveNormal(float2 worldPos, float distance){
    float size = max(TESR_WaterWaves.y, 1.0f) * max(TESR_WaterLighting4.w, 0.05f);   // world units per unit of p
    float2 p = worldPos / size;
    float2 wind = float2(cos(TESR_WaterWaves.z), sin(TESR_WaterWaves.z));
    float time = WATER_SECONDS;
    // How far (in p) a layer drawn at scale s has moved: a deep-water wave of length L units moves at
    // sqrt(g L / 2 pi) = sqrt(109.3 L) units a second (70 units a metre).
    #define RIPPLE_TRAVEL(s) (sqrt(109.3f * size / ((s) * 8.0f)) * time / size)

    float near = 1.0f - saturate(distance / 3000.0f);
    float3 swell  = expand(tex2D(TESR_samplerWater, rotateWaterUV((p - rotateWaterUV(wind,  0.10f) * RIPPLE_TRAVEL(0.15f)) * 0.15f, 0.61f)).xyz);
    float3 large  = expand(tex2D(TESR_samplerWater, rotateWaterUV((p - rotateWaterUV(wind,  0.25f) * RIPPLE_TRAVEL(0.5f)) * 0.5f, 1.23f)).xyz);
    float3 medium = expand(tex2D(TESR_samplerWater, rotateWaterUV((p - rotateWaterUV(wind, -0.30f) * RIPPLE_TRAVEL(2.0f)) * 2.0f, 2.17f)).xyz);
    float3 micro  = expand(tex2D(TESR_samplerWater, rotateWaterUV((p - rotateWaterUV(wind,  0.45f) * RIPPLE_TRAVEL(4.0f)) * 4.0f, 2.89f)).xyz);
    float patches = 0.6f + 0.8f * saturate(tex2D(TESR_samplerWater, (p - wind * RIPPLE_TRAVEL(0.15f) * 0.2f) * 0.03f).x);
    #undef RIPPLE_TRAVEL

    float2 tilt = (swell.xy * 0.3f + large.xy + medium.xy * 0.5f + micro.xy * 0.3f * near) * patches;
    float up = (swell.z * 0.3f + large.z + medium.z * 0.5f + micro.z * 0.3f * near) / max(TESR_WaterLighting5.w, 1e-6f);
    return normalize(float3(tilt, up));
}

// ---------------------------------------------------------------------------------------------
// Wave shape (WaveHeight, WaveLength, WaveDirection, WaveSteepness): an animated patch of wind-driven
// water baked into a looping volume texture (Textures\Water\NVR_WaterWaves.dds, made by
// tools/water/generate_water_waves.py, the way film and game oceans are made): hundreds of waves
// from a real wind-wave spectrum, each moving at the speed a real water wave of its length does, the
// crests pulled sharp and the troughs broad, and where the crests fold, whitecap foam. Two layers of
// it, the second smaller and turned against the first and playing at its own speed, so the tiling
// never lines up. Per texel: R, G slope, B height, A crest folding.
// ---------------------------------------------------------------------------------------------
// MUST stay on ONE line (see Shadow.hlsl's TESR_ShadowAtlas).
sampler3D TESR_WaterWavesMap : register(s11) < string ResourceName = "Water\NVR_WaterWaves.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

// From the generator (it prints these).
#define WAVE_TEX_SIZE        128.0f
#define WAVE_TEX_PATCH       15.0f     // metres the patch spans
#define WAVE_TEX_PERIOD      12.0f     // seconds its loop lasts at that size
#define WAVE_TEX_PEAK        3.0f      // patch size over the peak wavelength
#define WAVE_TEX_HEIGHT_MAX  3.5205f   // stored height 1 over the height's standard deviation
#define WAVE_TEX_SLOPE_SCALE 147.9828f // slope_max * patch / standard deviation
// The second layer: its size and height against the first, its turn, and offsets in place and time.
#define WAVE_LAYER_B_SCALE   0.37f
#define WAVE_LAYER_B_ANGLE   0.26f     // 15 degrees off the wind: crossing waves, not a second sea
#define WAVE_LAYER_B_OFFSET  float2(0.31f, 0.67f)
#define WAVE_LAYER_B_TIME    0.43f

// Everything about the two layers that stays the same across the pixel.
struct WaveField {
    float2 dirA, dirB;      // each layer's wind direction (its texture's +u) in the world
    float tileA, tileB;     // world units each layer's patch spans
    float timeA, timeB;     // loop position
    float lodA, lodB;       // mip level for the pixel's size
    float height;           // world units per unit of stored height (first layer)
    float slope;            // world slope per unit of stored slope (the same for both layers)
};

// WaveHeight is the typical height from trough to crest (the significant wave height, four standard
// deviations); WaveLength the typical length from crest to crest (the spectrum's peak).
WaveField getWaveField(float time, float pixelSize){
    WaveField field;
    // The baked waves travel toward -u in the texture (the generator's phase convention): turn the
    // texture half round so they run downwind, with the ripples, foam and caustics.
    float angle = TESR_WaterWaves.z + 3.14159265f;
    field.dirA = float2(cos(angle), sin(angle));
    field.dirB = float2(cos(angle + WAVE_LAYER_B_ANGLE), sin(angle + WAVE_LAYER_B_ANGLE));
    field.tileA = max(TESR_WaterWaves.y, 1.0f) * WAVE_TEX_PEAK;
    field.tileB = field.tileA * WAVE_LAYER_B_SCALE;
    // Waves scaled up by s take sqrt(s) longer to pass (deep water): each layer plays at its own speed.
    float patchUnits = WAVE_TEX_PATCH * WATER_UNITS_PER_METRE;
    field.timeA = time / (WAVE_TEX_PERIOD * sqrt(field.tileA / patchUnits));
    field.timeB = time / (WAVE_TEX_PERIOD * sqrt(field.tileB / patchUnits)) + WAVE_LAYER_B_TIME;
    field.lodA = max(log2(pixelSize * WAVE_TEX_SIZE / field.tileA), 0.0f);
    field.lodB = max(log2(pixelSize * WAVE_TEX_SIZE / field.tileB), 0.0f);
    float sigma = TESR_WaterWaves.x * 0.25f;
    field.height = sigma * WAVE_TEX_HEIGHT_MAX;
    field.slope = sigma * WAVE_TEX_SLOPE_SCALE / field.tileA * lerp(0.5f, 1.5f, saturate(TESR_WaterWaves.w));
    return field;
}

float4 sampleWaveLayer(float2 worldPos, float2 dir, float tile, float2 offset, float time, float lod){
    float2 uv = float2(dot(worldPos, dir), dot(worldPos, float2(-dir.y, dir.x))) / tile + offset;
    return tex3Dlod(TESR_WaterWavesMap, float4(uv, time, lod));
}

// The height of the waves at worldPos, for the parallax trace.
// Crest sharpness (CrestSharpness): real wind waves are not symmetric -- the crests come to a point
// and the troughs are broad and flat. The second-order (Stokes) correction does just that: a term in
// the height squared lifts the crests and fills the troughs, less the mean it adds, so the water
// level stays where it was. h + s (h^2 / r - mean), s up to 0.5 of the tallest height r, so the
// slope (times 1 + 2 s h / r) never turns over in the deepest trough. slopeFactor: what the slopes
// are scaled by at this height.
float shapeWaveCrest(WaveField field, float h, out float slopeFactor){
    float r = max(field.height * (1.0f + WAVE_LAYER_B_SCALE), 1e-3f);
    float s = 0.5f * saturate(TESR_WaterLighting2.w);
    float sigma = field.height / WAVE_TEX_HEIGHT_MAX;
    float meanSquare = sigma * sigma * (1.0f + WAVE_LAYER_B_SCALE * WAVE_LAYER_B_SCALE) / r;
    slopeFactor = 1.0f + 2.0f * s * h / r;
    return h + s * (h * h / r - meanSquare);
}

float getWaveFieldHeight(WaveField field, float2 worldPos){
    float a = sampleWaveLayer(worldPos - TESR_WaterWaveOrigin.xy, field.dirA, field.tileA, 0.0f, field.timeA, field.lodA).b;
    float b = sampleWaveLayer(worldPos - TESR_WaterWaveOrigin.zw, field.dirB, field.tileB, WAVE_LAYER_B_OFFSET, field.timeB, field.lodB).b;
    float slopeFactor;
    return shapeWaveCrest(field, field.height * ((a * 2.0f - 1.0f) + WAVE_LAYER_B_SCALE * (b * 2.0f - 1.0f)), slopeFactor);
}

// The tallest the waves can reach.
float getWaveFieldRange(WaveField field){
    // The sharpened crests reach up to (1 + s) of the unshaped height (shapeWaveCrest).
    return max(field.height * (1.0f + WAVE_LAYER_B_SCALE) * (1.0f + 0.5f * saturate(TESR_WaterLighting2.w)), 1e-3f);
}

// Height, world slope (dh/dx, dh/dy) and crest folding at worldPos.
float getWaveFieldSurface(WaveField field, float2 worldPos, out float2 slope, out float fold){
    float4 a = sampleWaveLayer(worldPos - TESR_WaterWaveOrigin.xy, field.dirA, field.tileA, 0.0f, field.timeA, field.lodA);
    float4 b = sampleWaveLayer(worldPos - TESR_WaterWaveOrigin.zw, field.dirB, field.tileB, WAVE_LAYER_B_OFFSET, field.timeB, field.lodB);
    // Stored slopes are along each layer's own axes: turn them back into the world.
    float2 slopeA = a.rg * 2.0f - 1.0f;
    float2 slopeB = b.rg * 2.0f - 1.0f;
    slope = field.slope * (slopeA.x * field.dirA + slopeA.y * float2(-field.dirA.y, field.dirA.x)
                         + slopeB.x * field.dirB + slopeB.y * float2(-field.dirB.y, field.dirB.x));
    fold = max(a.a, b.a * 0.7f);
    float slopeFactor;
    float height = shapeWaveCrest(field, field.height * ((a.b * 2.0f - 1.0f) + WAVE_LAYER_B_SCALE * (b.b * 2.0f - 1.0f)), slopeFactor);
    slope *= slopeFactor;
    return height;
}

// Parallax (WaveParallax): a wave standing up hides what is behind it, and seen at a low angle the
// crest in front covers the trough beyond. The view ray is traced down from above the tallest crest
// to where it first meets the waves -- up to eight steps to find the first crossing, then two
// refinements between the steps either side of it -- so the shading is taken from the point the eye
// really sees, the nearest crest hiding what lies behind it, at any angle. Fades out with distance.
// Returns the world position to shade. pixelSize: world units per pixel.
// Cost: the steps stop reading the wave texture once the crossing is found; a short trace (a steep
// view) takes fewer steps, about one per quarter of the smaller layer's wavelength, 3 to 8; and one
// that would move the shading less than a pixel is not traced at all.
float2 getWaveParallax(float2 worldPos, float3 eyeDirection, WaveField field, float distance, float pixelSize){
    float strength = TESR_WaterWaves2.y * (1.0f - saturate(distance / 4000.0f));
    // Along the view ray, the ground position moves by shift for each unit of height.
    float2 shift = eyeDirection.xy / max(eyeDirection.z, 0.25f) * strength;
    float range = getWaveFieldRange(field);
    float reach = length(shift) * range * 2.0f;          // how far over the water the whole trace runs
    float2 result = worldPos;
    // Faded out (by 4000 units, and all the distant water), or under a pixel: no trace at all.
    [branch] if (strength > 0.0f && reach > pixelSize) {
        float steps = clamp(ceil(reach / (field.tileB / WAVE_TEX_PEAK * 0.25f)), 3.0f, 8.0f);

        // f = height of the ray above the waves: positive above the tallest crest, never positive below
        // the deepest trough, so a crossing always lies in between (the last step, at -range, is one).
        float aboveH = range;
        float aboveF = range - getWaveFieldHeight(field, worldPos + shift * range);
        float belowH = -range;
        float belowF = -1.0f;
        bool found = false;
        [loop]
        for (int i = 1; i <= 8; i++) {
            [branch] if (!found && i <= steps) {
                float h = range - range * 2.0f * i / steps;
                float f = h - getWaveFieldHeight(field, worldPos + shift * h);
                if (f <= 0.0f) {
                    belowH = h;
                    belowF = f;
                    found = true;
                }
                else {
                    aboveH = h;
                    aboveF = f;
                }
            }
        }
        [loop]
        for (int j = 0; j < 2; j++) {
            float h = aboveH + (belowH - aboveH) * aboveF / max(aboveF - belowF, 1e-5f);
            float f = h - getWaveFieldHeight(field, worldPos + shift * h);
            if (f > 0.0f) { aboveH = h; aboveF = f; }
            else          { belowH = h; belowF = f; }
        }
        float hit = aboveH + (belowH - aboveH) * aboveF / max(aboveF - belowF, 1e-5f);
        result = worldPos + shift * hit;
    }
    return result;
}

// Shallow water (ShallowWaves): waves feel the bottom and die down as the water shallows, so at the
// shore the surface lies nearly flat and glassy against the ground rather than its crests cutting
// through it. depthBelow: the depth of water under the pixel. 1 in open water, falling smoothly to 0
// at the waterline over ShallowWaves units of depth. 0 (the setting): waves run to the edge.
float getShallowWaves(float depthBelow){
    float depth = TESR_WaterAbsorption.w;
    return depth > 0.0f ? smoothstep(0.0f, depth, depthBelow) : 1.0f;
}

// Gusts: a very large, slow pattern over the water -- the wave texture's own height, 24 times the
// first layer's size, drifting over ten minutes -- that roughens some stretches and calms others, as
// gusts of wind do. Breaks up the tiling that would otherwise line up into a grid across kilometres
// of open water, and gives the distant water its patches. A factor on the slopes, 0.65 to 1.35.
float getWaveGusts(WaveField field, float2 worldPos){
    float2 p = worldPos - TESR_WaterWaveOrigin.xy;
    float2 uv = float2(dot(p, field.dirA), dot(p, float2(-field.dirA.y, field.dirA.x))) / (field.tileA * 24.0f) + float2(0.5f, 0.25f);
    float g = tex3Dlod(TESR_WaterWavesMap, float4(uv, WATER_SECONDS / 600.0f, 3.0f)).b;
    return lerp(0.65f, 1.35f, saturate((g - 0.5f) * 2.5f + 0.5f));
}

// Waves: the wave field with the normal-map detail on it, as slopes added together. heightOut: -1
// in a trough to 1 on a crest (half WaveHeight either way); foldOut: crest folding, 0-1, for the
// whitecaps. worldPos: the shaded point (after the parallax). refractionNormal: the same with
// the fine detail at 40%. The fine ripples glint and shade the surface, but at full strength they
// would scribble the bed seen through them into squiggles; what a bed seen through real water mostly
// follows is the larger waves.
// shallow: getShallowWaves, already applied to the field; here to the crest folding, and to the
// ripples, which keep a third of their strength in the shallowest water (a breath of wind still
// ruffles it).
float3 getWaves(float2 worldPos, float distance, WaveField field, float shallow, out float heightOut, out float foldOut, out float3 refractionNormal){
    float2 slope;
    float height = getWaveFieldSurface(field, worldPos, slope, foldOut);
    foldOut *= shallow;
    heightOut = TESR_WaterWaves.x > 0.0f ? clamp(height / (TESR_WaterWaves.x * 0.5f), -1.0f, 1.0f) : 0.0f;
    float3 detail = getWaveNormal(worldPos, distance);
    // A surface of slope s faces (-s, 1); the detail normal already faces its own way.
    float2 detailSlope = detail.xy / max(detail.z, 0.1f) * lerp(0.35f, 1.0f, shallow);
    // Gusts roughen the waves and their ripples together (not the whitecaps: those follow the crests' folding alone).
    float gust = getWaveGusts(field, worldPos);
    slope *= gust;
    detailSlope *= gust;
    refractionNormal = normalize(float3(-slope + detailSlope * 0.4f, 1.0f));
    return normalize(float3(-slope + detailSlope, 1.0f));
}

// Rain rings on the surface (WetWorld's rain amount). Four layers of the ripple texture, each
// dropping at its own time; faded out with distance. Over the world at WetWorld's own size (a ripple
// tile every 120 units), so the rain on the water matches the rain in the puddles beside it, on
// every kind of water (the vanilla texture position's scale is each water form's own).
// dx, dy: ddx and ddy of worldPos, taken at the top level, so the rings can be skipped (dry weather,
// or past their fade) without four texture reads.
float3 getRainRing(float2 uv, float2 dx, float2 dy, float time, float weight){
    float4 ripple = tex2Dgrad(TESR_RippleSampler, uv, dx, dy);
    ripple.yz = expand(ripple.yz);
    float period = frac(ripple.w + time);
    float timeFrac = period - 1.0f + ripple.x;
    float drop = saturate(0.2f + weight * 0.8f - period);
    float strength = drop * ripple.x * sin(clamp(timeFrac * 9.0f, 0.0f, 3.0f) * PI);
    return float3(ripple.yz * strength * 0.35f, 1.0f);
}

float3 getRainRipples(float2 worldPos, float2 dx, float2 dy, float3 N, float distance, float rain){
    float fade = 1.0f - saturate(distance / 3500.0f);
    float3 result = N;
    [branch] if (rain * fade > 0.0f) {
        float4 weights = saturate(float4(1.0f, 0.75f, 0.5f, 0.25f) * rain * 4.0f) * 2.0f * fade;
        float4 times = float4(0.96f, 0.97f, 0.98f, 0.99f) * 0.07f * WATER_SCROLL_TIME;
        float2 uv = worldPos / 120.0f;
        dx /= 120.0f;
        dy /= 120.0f;
        float3 r1 = getRainRing(uv + float2(0.25f, 0.0f), dx, dy, times.x, weights.x);
        float3 r2 = getRainRing(uv * 1.1f + float2(-0.55f, 0.3f), dx * 1.1f, dy * 1.1f, times.y, weights.y);
        float3 r3 = getRainRing(uv * 1.3f + float2(0.6f, 0.85f), dx * 1.3f, dy * 1.3f, times.z, weights.z);
        float3 r4 = getRainRing(uv * 1.5f + float2(0.5f, -0.75f), dx * 1.5f, dy * 1.5f, times.w, weights.w);
        float2 rings = weights.x * r1.xy + weights.y * r2.xy + weights.z * r3.xy + weights.w * r4.xy;
        result = normalize(float3(N.xy + rings, N.z));
    }
    return result;
}

// The engine's wading ripples around the player and actors (the displacement map), bent into the
// wave normal.
float3 getWadingNormal(float2 displacementPos, float blendRadius, float3 N){
    float4 displacement = tex2D(DisplacementMap, displacementPos);
    float2 wadeSlope = (displacement.zw - 0.5f) * blendRadius / 2.0f;   // reconstructZ is a macro: pass a name
    float3 wade = reconstructZ(wadeSlope);
    return normalize(float3(N.xy + wade.xy * 2.0f, N.z * wade.z));
}

// ---------------------------------------------------------------------------------------------
// Light on the surface
// ---------------------------------------------------------------------------------------------

// GGX glint off a water normal. NdotV and NdotL are kept off exactly 0 inside the BRDF, whose
// 4 * NdotV * NdotL denominator would make 0/0 at the horizon; multiplied by the real NdotL after.
float3 getGlint(float3 N, float3 L, float3 eyeDirection, float roughness){
    float3 H = normalize(eyeDirection + L);
    float NdotL = shades(N, L);
    float NdotV = max(shades(N, eyeDirection), 1e-4f);
    float NdotH = shades(N, H);
    float3 Ks = FresnelShlick(0.08, H, eyeDirection);
    return BRDF(roughness, Ks, NdotV, max(NdotL, 1e-4f), NdotH) * NdotL;
}

// Glint roughness with specular anti-aliasing (SpecularAA): calm water's glint is so narrow that
// where the waves are finer than a pixel it lands on some pixels and misses their neighbours and
// crawls; widened by how fast the normal changes across the pixel, and a little with distance.
// ddx/ddy inside: top level only.
float getSpecularRoughness(float3 N, float distance){
    float antiAliased = SpecularAA(N, WATER_ROUGHNESS);
    antiAliased = sqrt(antiAliased * antiAliased + saturate(distance / 16000.0f) * 0.04f);
    return lerp(WATER_ROUGHNESS, antiAliased, saturate(TESR_WaterLighting2.x));
}

// The sun on the water: the glint, and with SunGlitter the broad sun path full of sharp sparkles
// that sunlit water seen toward the sun has -- a soft lobe draws the path, and the sharp glint of
// each wave facet, kept inside it and capped, draws the sparkles. In sunColor units.
float3 getSunGlint(float3 N, float3 sunDirection, float3 eyeDirection, float roughness){
    float3 glint = getGlint(N, sunDirection, eyeDirection, roughness);
    // A narrower path, and more of it drawn by the sparkles than by its own soft glow: fine points of
    // light in a column toward the sun, not broad gold patches.
    float3 path = getGlint(N, sunDirection, eyeDirection, 0.18f);
    float3 sparkle = min(getGlint(N, sunDirection, eyeDirection, WATER_ROUGHNESS), 40.0f);
    glint += (sparkle * (path / (path + 1.0f)) * 0.25f + path * 0.05f) * TESR_WaterLighting2.z;
    return glint * 10.0f;
}

// How much of the reflection shows: Schlick with water's own 2%, so the water is clear looking down
// and a mirror at low angles. Real water's, for every kind (the water forms' reflectivity is not used).
float getFresnel(float3 N, float3 eyeDirection){
    return WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - saturate(dot(eyeDirection, N)), 5.0f);
}

// Wave scattering (WaveScattering): sunlight that enters the far side of a wave, passes through its
// thin top and comes out toward the eye lights the crest from within. The light's path: in along the
// sun's direction, bent a little by the surface it enters (the normal), out toward the eye -- so the
// glow sits on the backs of the waves in line with the sun and moves along them as the view does,
// strongest looking toward a low sun but there whenever the light lines up. Where the water is
// thinnest: the crests (height squared, the troughs staying dark) and most where they pinch and fold.
// Its colour is what a thin layer of water lets through: the red goes first (AbsorptionColor), a
// bright turquoise, with the water's own hue. Gone once the sun is under the horizon. In light units.
float3 getWaveScattering(float3 N, float3 eyeDirection, float3 sunDirection, float3 sunLight, float3 waterHue, float waveHeight, float waveFold){
    float3 through = normalize(sunDirection + N * 0.6f);
    float toEye = pow(saturate(dot(eyeDirection, -through)), 4.0f);
    float crestHeight = saturate(waveHeight * 0.5f + 0.5f);
    float crest = lerp(0.1f, 1.0f, crestHeight * crestHeight) + waveFold * 0.8f;
    float aboveHorizon = saturate(sunDirection.z * 10.0f);
    // A thin crest, about 120 units of water (getTransmittance's absorption), with the water's hue.
    float3 thin = exp(-TESR_WaterAbsorption.rgb * (120.0f * TESR_WaterLighting.y / 300.0f)) * lerp(waterHue, 1.0f, 0.5f);
    thin /= max(max(thin.r, max(thin.g, thin.b)), 1e-4f);
    return thin * sunLight * (toEye * crest * aboveHorizon * TESR_WaterLighting.w);
}

// Reflection, blurred on choppy water (ReflectionBlur): four taps around the lookup, spread by how
// steep the waves are there, so calm water stays mirror-sharp. Linear. tex2Dproj: top level only.
// The sky along a reflected ray R, for when the game's reflection map is not rendered
// (WaterReflections GameReflections off): the horizon colour low down, the sky's colour higher up.
float3 getSkyReflection(float3 R, float3 horizon){
    float up = saturate(R.z);
    float3 low = lerp(horizon, linearize(TESR_SkyLowColor).rgb, saturate(up * 4.0f));
    return lerp(low, linearize(TESR_SkyColor).rgb, saturate(up * 1.5f - 0.2f));
}

// SkyTint: the water body glows with the light it takes in from the whole sky, so it takes the
// sky's colour -- half the sky overhead, half its horizon -- as a tint that keeps its brightness:
// bluer under a clear sky, warmer at sunset, greyer overcast. Outdoors only (horizon: skyLight).
float3 getSkyTint(float3 horizon){
    float3 sky = lerp(horizon, linearize(TESR_SkyColor).rgb, 0.5f);
    float3 hue = min(sky / max(luma(sky), 1e-4f), 2.0f);
    return lerp(1.0f, hue, saturate(TESR_WaterLighting5.z));
}

// reflectionUV: the lookup's screen position (the projected reflectionPos); dx, dy its ddx and ddy,
// taken at the top level, so the whole read can sit in a branch (getReflection).
float3 getBlurredReflection(float2 reflectionUV, float2 dx, float2 dy, float3 N){
    float radius = TESR_WaterLighting3.w * 0.006f * saturate(length(N.xy) * 4.0f);
    float4 sum = tex2Dgrad(ReflectionMap, reflectionUV, dx, dy);
    sum += tex2Dgrad(ReflectionMap, reflectionUV + float2( radius, 0.0f), dx, dy);
    sum += tex2Dgrad(ReflectionMap, reflectionUV + float2(-radius, 0.0f), dx, dy);
    sum += tex2Dgrad(ReflectionMap, reflectionUV + float2(0.0f,  radius), dx, dy);
    sum += tex2Dgrad(ReflectionMap, reflectionUV + float2(0.0f, -radius), dx, dy);
    return linearize(sum * 0.2f).rgb;
}

// What outdoor water reflects: the game's reflection map, blurred on rough water, or with
// GameReflections off (the map is not rendered) the sky along the reflected ray. A real branch: the
// five reads (one without blur: the distant water) are not made while the map is off. reflectionPos:
// getReflectionScreenPos; top level.
float3 getReflection(float4 reflectionPos, float3 N, float3 eyeDirection, float3 skyLight, bool blur){
    float2 reflectionUV = reflectionPos.xy / reflectionPos.w;
    float2 dx = ddx(reflectionUV);
    float2 dy = ddy(reflectionUV);
    float3 reflection;
    [branch] if (TESR_WaterLighting5.x > 0.5f)
        reflection = blur ? getBlurredReflection(reflectionUV, dx, dy, N) : linearize(tex2Dgrad(ReflectionMap, reflectionUV, dx, dy)).rgb;
    else
        reflection = getSkyReflection(reflect(-eyeDirection, N), skyLight);
    return reflection;
}

// ---------------------------------------------------------------------------------------------
// The water body
// ---------------------------------------------------------------------------------------------

// The colour the water body glows with: ScatterColor when set -- the game's water colours are near
// black once linear -- else the water form's shallow-to-deep colour by depth.
float3 getWaterColor(float3 shallowColor, float3 deepColor, float depthBelow){
    return TESR_WaterScatterColor.w > 0.5f ? TESR_WaterScatterColor.rgb
                                           : lerp(shallowColor, deepColor, saturate(depthBelow / (20.0f * WATER_UNITS_PER_METRE)));
}

// Beer-Lambert absorption over the path through the water: each colour at its own rate
// (AbsorptionColor), how fast set by AbsorptionDepth. At the defaults, 5 m of water lets through
// about a third of the red and three quarters of the green and blue.
float3 getTransmittance(float pathLength){
    return exp(-TESR_WaterAbsorption.rgb * pathLength * (TESR_WaterLighting.y / 300.0f));
}

// Caustics (Caustics, CausticsScale): sunlight focused by the waves into bright rippling lines on
// the bed in shallow water. Two layers of the wave texture drift across the bed's world position;
// where their slopes cancel, the light converges. None at the waterline, fading out in deep water.
// A factor on the bed: light, the sunlight reaching it (its luma, times the shadow). Needs
// TESR_CameraPosition. tex2Dlod, a level from the distance; not read at all where the result would
// be too faint to see (deep water, shade, night, Caustics 0).
float getCaustics(float3 bedFromCamera, float depthBelow, float light){
    float depthFade = saturate(depthBelow / 20.0f) * exp(-depthBelow / 500.0f);
    float amount = depthFade * TESR_WaterLighting4.x * light;
    float caustics = 0.0f;
    [branch] if (amount > 0.002f) {
        // The light the waves focus moves with them: both layers drift downwind (a little to either side),
        // at a third of the speed of the waves (a deep-water wave of WaveLength moves sqrt(109.3 L) units
        // a second), in CausticsScale's units.
        float2 wind = float2(cos(TESR_WaterWaves.z), sin(TESR_WaterWaves.z));
        float travel = sqrt(109.3f * max(TESR_WaterWaves.y, 1.0f)) * 0.35f * WATER_SECONDS / TESR_WaterLighting4.y;
        float2 world = (bedFromCamera.xy + TESR_CameraPosition.xy) / TESR_WaterLighting4.y;
        float lod = log2(max(length(bedFromCamera) / (TESR_WaterLighting4.y * 2.0f), 1.0f));
        float2 a = expand(tex2Dlod(TESR_samplerWater, float4(world - rotateWaterUV(wind, 0.2f) * travel, 0.0f, lod))).xy;
        float2 b = expand(tex2Dlod(TESR_samplerWater, float4(rotateWaterUV((world - rotateWaterUV(wind, -0.25f) * travel * 0.8f) * 1.37f, 1.1f), 0.0f, lod))).xy;
        float focus = saturate(1.0f - length(a + b) * 1.8f);
        caustics = focus * focus * focus * 3.0f * amount;
    }
    return caustics;
}

// ---------------------------------------------------------------------------------------------
// The edge
// ---------------------------------------------------------------------------------------------

// Foam (Foam, FoamWidth, FoamScale): a foam texture (Textures\Water\NVR_Foam.dds) -- R dense,
// cellular foam, G broken, streaky foam -- in world space, two layers turned against each other so
// it does not repeat, both drifting slowly downwind (a little to either side of the wind), with the
// waves and whitecaps they sit on rather than across them.
// MUST stay on ONE line (see Shadow.hlsl's TESR_ShadowAtlas).
sampler2D TESR_FoamMap : register(s13) < string ResourceName = "Water\NVR_Foam.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

// The foam texture at worldPos (x dense, y streaky). dx, dy: ddx and ddy of worldPos, taken at the
// top level, so it can be read inside a branch (getFoam).
float2 getFoamTexture(float2 worldPos, float2 dx, float2 dy, float time){
    float scale = 1.0f / max(TESR_WaterLighting5.y, 1.0f);
    float2 uv = worldPos * scale;
    dx *= scale;
    dy *= scale;
    float2 wind = float2(cos(TESR_WaterWaves.z), sin(TESR_WaterWaves.z));
    float2 a = tex2Dgrad(TESR_FoamMap, uv - rotateWaterUV(wind, 0.15f) * (0.0125f * time), dx, dy).rg;
    float2 b = tex2Dgrad(TESR_FoamMap, rotateWaterUV((uv - rotateWaterUV(wind, -0.2f) * (0.0115f * time)) * 0.71f, 1.3f),
                         rotateWaterUV(dx * 0.71f, 1.3f), rotateWaterUV(dy * 0.71f, 1.3f)).rg;
    return 0.5f * (a + b);
}

// Foam where the view passes through the least water -- along the shore and around anything
// standing in the water. By the path through the water, not the depth under the point: in front of a
// post that depth is small all the way down the post. Solid right at the edge; further out only the
// thickest of the dense foam holds together, and beyond it, out to twice FoamWidth, only streaks.
float getFoamMask(float pathLength, float2 foamTexture){
    float band = 1.0f - saturate(pathLength / TESR_WaterLighting3.y);
    float outer = 1.0f - saturate(pathLength / (TESR_WaterLighting3.y * 2.0f));
    float dense = max(smoothstep(0.9f - band, 1.1f - band, foamTexture.x), band * band * band * band);
    float streaks = smoothstep(0.9f - outer, 1.1f - outer, foamTexture.y) * outer;
    return max(dense, streaks) * saturate(TESR_WaterLighting3.x);
}

// Whitecaps (Whitecaps): where the wave crests fold over, they break into streaky foam: the more a
// crest folds, the more of the pattern shows. fold: 0-1 from the wave field; the more Whitecaps, the
// less folding it takes. Only the crests that fold hardest break, and their foam fades out at its
// edges instead of ending in a hard line.
float getWhitecaps(float fold, float2 foamTexture){
    float amount = saturate(TESR_WaterWaves2.x);
    float cap = saturate((fold - (1.0f - amount) * 0.9f) * 3.0f);
    return smoothstep(0.85f - cap, 1.2f - cap, foamTexture.y) * cap * saturate(amount * 4.0f);
}

// The colour of foam: white froth, full of air, lit by the whole sky and by the sun from nearly any
// angle (it is rough through and through, so a low sun still lights it, warmly, rather than leaving
// it grey). shadow: 1 in the sun.
float3 getFoamColor(float3 sunLight, float3 sunDirection, float3 skyLight, float shadow){
    return 0.9f * (skyLight * 0.8f + sunLight * shadow * saturate(sunDirection.z * 0.6f + 0.4f));
}

// All the foam at a point: the edge's (pathLength: the path through the water there; a negative one
// for none, the distant water) and the whitecaps'. The foam texture is only read where one of them
// can show -- near an edge, or on a crest folding past the whitecaps' threshold -- which leaves most
// of the open water without it. dx, dy: ddx and ddy of worldPos at the top level.
float getFoam(float2 worldPos, float2 dx, float2 dy, float time, float pathLength, float fold){
    float amount = saturate(TESR_WaterWaves2.x);
    bool edge = pathLength >= 0.0f && pathLength < TESR_WaterLighting3.y * 2.0f && TESR_WaterLighting3.x > 0.0f;
    bool caps = amount > 0.0f && fold > (1.0f - amount) * 0.9f;
    float foam = 0.0f;
    [branch] if (edge || caps) {
        float2 foamTexture = getFoamTexture(worldPos, dx, dy, time);
        foam = getWhitecaps(fold, foamTexture);
        if (pathLength >= 0.0f) foam = max(foam, getFoamMask(pathLength, foamTexture));
    }
    return foam;
}

// Shoreline fade (ShoreFadeWidth): the water fades out over that much depth at the edge, lapping in
// and out with shoreMovement; the foam stays visible through it and draws the waterline. 0: none.
float getShoreAlpha(float depthBelow, float shoreMovement, float foam){
    float width = TESR_WaterLighting3.z;
    if (width <= 0.0f) return 1.0f;
    float lap = sin(WATER_SCROLL_TIME * shoreMovement * 0.1f) * 0.25f;
    return max(smoothstep(0.0f, width, depthBelow + lap * width), foam * 0.9f);
}

// ---------------------------------------------------------------------------------------------
// Point lights (PointLights): lamps and campfires glinting on the water. Needs TESR_CameraPosition,
// TESR_ShadowLightPosition[12], TESR_LightPosition[12] and TESR_LightColor[24]. Light positions
// are world space; toLight is built as (light - camera) - pixel with the pixel camera-relative, so
// the large coordinates cancel first. A light that does not reach the pixel is skipped, and the
// loop ends at the first slot where both lists are empty (both are packed from slot 0).
// ---------------------------------------------------------------------------------------------
#ifdef WATER_POINT_LIGHTS
float3 getPointLightGlint(float3 N, float4 light, float4 colour, float3 pixelFromCamera, float3 eyeDirection, float roughness){
    float3 toLight = (light.xyz - TESR_CameraPosition.xyz) - pixelFromCamera;
    float distSq = dot(toLight, toLight);
    float radiusSq = light.w * light.w;
    float3 glint = 0.0f;
    [branch]
    if (light.w > 0.0f && distSq < radiusSq) {
        float s = distSq / radiusSq;
        float atten = saturate(((1.0f - s) * (1.0f - s)) / (1.0f + 5.0f * s));
        float3 L = toLight * rsqrt(max(distSq, 1e-4f));
        glint = getGlint(N, L, eyeDirection, roughness) * colour.rgb * colour.w * atten;
    }
    return glint;
}

float3 getPointLights(float3 N, float3 pixelFromCamera, float3 eyeDirection, float roughness){
    float3 specular = 0.0f;
    float strength = TESR_WaterLighting2.y;
    [branch]
    if (strength > 0.0f) {
        [loop]
        for (int i = 0; i < 12; i++) {
            if (TESR_ShadowLightPosition[i].w <= 0.0f && TESR_LightPosition[i].w <= 0.0f) break;
            specular += getPointLightGlint(N, TESR_ShadowLightPosition[i], TESR_LightColor[i], pixelFromCamera, eyeDirection, roughness);
            specular += getPointLightGlint(N, TESR_LightPosition[i], TESR_LightColor[12 + i], pixelFromCamera, eyeDirection, roughness);
        }
        specular *= strength;
    }
    return specular;
}
#endif

// ---------------------------------------------------------------------------------------------
// The sun shadow on the surface (SunShadows). Needs Shadow.hlsl; forward shadows only
// (FORWARD_SHADOWS compiled in, and not suppressed at runtime -- GetSunShadow gives 1 then). The
// surface is flat, so the bias normal is up. [branch] around it; tex2Dlod inside.
// ---------------------------------------------------------------------------------------------
#ifdef WATER_SUN_SHADOWS
float getWaterSunShadow(float3 surfaceFromCamera){
    float shadow = 1.0f;
#if FORWARD_SHADOWS
    [branch]
    if (TESR_WaterLighting.x > 0.0f)
        shadow = lerp(1.0f, GetSunShadow(surfaceFromCamera, float3(0.0f, 0.0f, 1.0f)), saturate(TESR_WaterLighting.x));
#endif
    return shadow;
}
#endif

