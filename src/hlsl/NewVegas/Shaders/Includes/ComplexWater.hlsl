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
    float2 LTEXCOORD_7 : TEXCOORD7;              // wave texture position
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#include "Includes/PBR.hlsl"

// ---------------------------------------------------------------------------------------------
// Settings ([Shaders.Water.ComplexWater], WaterShaders::UpdateSettings). Registers clear of the
// engine's water constants (c0-c13), of the template's own (c14-c72), of Shadow.hlsl (c100-c133)
// and of the scene-depth constants below (c192-c201).
//   TESR_WaterLighting     x: SunShadows      y: AbsorptionDepth  z: WaterColorBrightness  w: WaveScattering
//   TESR_WaterLighting2    x: SpecularAA      y: PointLights      z: SunGlitter            w: DebugView
//   TESR_WaterLighting3    x: Foam            y: FoamWidth        z: ShoreFadeWidth        w: ReflectionBlur
//   TESR_WaterLighting4    x: Caustics        y: CausticsScale    z: 1 outdoors, 0 indoors (per frame)
//   TESR_WaterScatterColor rgb: ScatterColor, w: 1 when set (else the water form's own colours)
//   TESR_WaterAbsorption   rgb: AbsorptionColor, the absorption rate of each colour
//   TESR_WaterWaves        x: WaveHeight (units)  y: WaveLength (units)  z: WaveDirection (radians)  w: WaveSteepness
//   TESR_WaterWaves2       x: Whitecaps       y: WaveParallax     z: RefractionBlur        w: RefractionDispersion
// The DLL keeps the ones that must never be 0 (AbsorptionDepth, WaterColorBrightness, FoamWidth,
// CausticsScale) off 0.
// ---------------------------------------------------------------------------------------------
float4 TESR_WaterLighting     : register(c190);
float4 TESR_WaterLighting2    : register(c191);
float4 TESR_WaterLighting3    : register(c202);
float4 TESR_WaterLighting4    : register(c203);
float4 TESR_WaterScatterColor : register(c204);
float4 TESR_WaterAbsorption   : register(c205);
float4 TESR_WaterWaves        : register(c206);
float4 TESR_WaterWaves2       : register(c207);

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
// distance along the view axis, and the depth buffer's scale. Build it at the top level (ddx/ddy);
// the surface is flat, so the map is exact for points on the water plane near the pixel, which is
// all refraction needs.
struct WaterScreenMap {
    float2 worldX, worldY;   // camera-relative water-plane position, per pixel across and down
    float2 uvX, uvY;         // screen position, per pixel across and down
    float viewZ;             // the water's distance along the view axis at the pixel
    float viewZX, viewZY;    // ... per pixel across and down
    float det;
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
WaterScreenMap getWaterScreenMap(float3 surfaceFromCamera, float2 uv, float4 screenPos){
    WaterScreenMap map;
    map.worldX = ddx(surfaceFromCamera.xy);
    map.worldY = ddy(surfaceFromCamera.xy);
    map.uvX = ddx(uv);
    map.uvY = ddy(uv);
    map.det = map.worldX.x * map.worldY.y - map.worldX.y * map.worldY.x;
    map.viewZ = max(screenPos.w, 1e-3f);
    map.viewZX = ddx(map.viewZ);
    map.viewZY = ddy(map.viewZ);
    float waterDepth = 2.0f * screenPos.z / map.viewZ - 1.0f;
    map.reversed = waterDepth < 0.5f ? 1.0f : 0.0f;
    map.depthScale = max(getDepthFromFar(waterDepth, map.reversed), 1e-9f) / max(1.0f / map.viewZ - getInvFar(), 1e-9f);
    return map;
}

// Screen offset, in pixels across and down, of a water-plane offset (world units, horizontal).
float2 getPixelOffset(WaterScreenMap map, float2 worldOffset){
    if (abs(map.det) < 1e-10f) return 0.0f;
    float across = (worldOffset.x * map.worldY.y - worldOffset.y * map.worldY.x) / map.det;
    float down = (map.worldX.x * worldOffset.y - map.worldX.y * worldOffset.x) / map.det;
    return float2(across, down);
}

// Screen offset of a water-plane offset.
float2 getScreenOffset(WaterScreenMap map, float2 worldOffset){
    float2 pixels = getPixelOffset(map, worldOffset);
    return pixels.x * map.uvX + pixels.y * map.uvY;
}

// Distance along the view axis of the water-plane point worldOffset away from the pixel's.
float getWaterViewZ(WaterScreenMap map, float2 worldOffset){
    float2 pixels = getPixelOffset(map, worldOffset);
    return max(map.viewZ + pixels.x * map.viewZX + pixels.y * map.viewZY, 1e-3f);
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

// For DebugView 15, under the pixel: x the depth below the surface by TESR_DepthBufferWorld, y by
// TESR_DepthBufferBeforeWater, z 1 where the first shows the water surface itself.
float3 getDepthCopies(WaterScreenMap map, float3 surfaceFromCamera, float2 uv){
    float sceneZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x);
    float beforeWaterZ = getViewZFromDepth(map, tex2Dlod(TESR_DepthBufferBeforeWater, float4(uv, 0.0f, 0.0f)).x);
    float depth = max(surfaceFromCamera.z * (1.0f - sceneZ / map.viewZ), 0.0f);
    float beforeWaterDepth = max(surfaceFromCamera.z * (1.0f - beforeWaterZ / map.viewZ), 0.0f);
    return float3(depth, beforeWaterDepth, isWaterSurface(sceneZ, map.viewZ) ? 1.0f : 0.0f);
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

float2 getWaterPath(WaterScreenMap map, float3 surfaceFromCamera, float2 uv){
    return getWaterPathTo(getBedBehind(map, surfaceFromCamera, map.viewZ, uv), surfaceFromCamera);
}

// 1 where the depth buffer holds nothing at uv (still as cleared: the far end of its range), for
// DebugView 14.
float getSceneEmpty(WaterScreenMap map, float2 uv){
    float rawDepth = tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x;
    return getDepthFromFar(rawDepth, map.reversed) <= 1e-7f ? 1.0f : 0.0f;
}

// The depth buffer's near-plane scale against the DLL's own near plane, for DebugView 13.
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
// post, legs, the shore) or off the screen, the straight view is used, so nothing above the water
// smears into it. Returns the screen position of the bed seen; path and bed: for that bed.
// ---------------------------------------------------------------------------------------------
float2 getRefraction(float3 surfaceFromCamera, float3 N, float2 straightUV, WaterScreenMap map, float bedDepth, float strength, out float2 path, out float3 bed){
    float3 incident = normalize(surfaceFromCamera);
    float3 bent = refract(incident, N, 1.0f / 1.33f);
    bent = normalize(lerp(incident, dot(bent, bent) > 0.0f ? bent : incident, strength));
    // The water plane, seen from the camera: only meaningful with the camera above it.
    float planeZ = min(surfaceFromCamera.z, -1e-3f);

    float depth = bedDepth;
    float2 uv = straightUV;
    float3 waterPoint = surfaceFromCamera;
    float waterViewZ = map.viewZ;
    [unroll]
    for (int i = 0; i < 2; i++) {
        float3 target = surfaceFromCamera + bent * (depth / max(-bent.z, 0.1f));
        waterPoint = float3(target.xy * (planeZ / min(target.z, planeZ)), surfaceFromCamera.z);
        float2 offset = waterPoint.xy - surfaceFromCamera.xy;
        uv = straightUV + getScreenOffset(map, offset);
        waterViewZ = getWaterViewZ(map, offset);
        depth = max(surfaceFromCamera.z - getBedBehind(map, waterPoint, waterViewZ, uv).z, 0.0f);
    }

    bool leak = getSceneViewZ(map, uv, waterViewZ) < waterViewZ || any(uv != saturate(uv));
    uv = leak ? straightUV : uv;
    waterPoint = leak ? surfaceFromCamera : waterPoint;
    waterViewZ = leak ? map.viewZ : waterViewZ;
    bed = getBedBehind(map, waterPoint, waterViewZ, uv);
    path = getWaterPathTo(bed, waterPoint);
    return uv;
}

// The bed as seen through the water. Blurred the more water it is seen through (RefractionBlur),
// as fine particles in the water soften what lies deep; and split slightly by colour along the bend
// (RefractionDispersion), since water bends red light a little less than blue. Linear.
// tex2Dlod: the refraction map has no mipmaps, and the lookup follows the depth buffer.
float3 getRefractedBed(float2 uv, float2 straightUV, float pathLength){
    float2 bend = uv - straightUV;
    float dispersion = TESR_WaterWaves2.w * 0.5f;
    float3 bed;
    bed.r = tex2Dlod(RefractionMap, float4(straightUV + bend * (1.0f - dispersion), 0.0f, 0.0f)).r;
    bed.g = tex2Dlod(RefractionMap, float4(uv, 0.0f, 0.0f)).g;
    bed.b = tex2Dlod(RefractionMap, float4(straightUV + bend * (1.0f + dispersion), 0.0f, 0.0f)).b;

    float radius = TESR_WaterWaves2.z * saturate(pathLength / (10.0f * WATER_UNITS_PER_METRE)) * 0.006f;
    float3 blurred = bed;
    blurred += tex2Dlod(RefractionMap, float4(uv + float2( radius, 0.0f), 0.0f, 0.0f)).rgb;
    blurred += tex2Dlod(RefractionMap, float4(uv + float2(-radius, 0.0f), 0.0f, 0.0f)).rgb;
    blurred += tex2Dlod(RefractionMap, float4(uv + float2(0.0f,  radius), 0.0f, 0.0f)).rgb;
    blurred += tex2Dlod(RefractionMap, float4(uv + float2(0.0f, -radius), 0.0f, 0.0f)).rgb;
    return linearize(float4(blurred * 0.2f, 1.0f)).rgb;
}

// ---------------------------------------------------------------------------------------------
// Waves. One normal texture (water_NRM, watercalm_NRM for placed water) in four layers, each turned
// to its own angle so the texture repeat never lines up across the water: a broad swell, large and
// medium waves, and fine ripples that fade out with distance, where they would only shimmer. A very
// large, slow pattern varies the wave strength into rougher and calmer patches. choppiness,
// waveWidth and waveSpeed from the water's own section. tex2D: top level only.
// ---------------------------------------------------------------------------------------------
float3 getWaveNormal(float2 texPos, float distance, float4 waveParams){
    float choppiness = waveParams.x;
    float speed = WATER_SCROLL_TIME * 0.002f * waveParams.z;
    float2 p = texPos * waveParams.y;

    float near = 1.0f - saturate(distance / 3000.0f);
    float3 swell  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 0.15f, 0.61f) + normalize(float2(1, 3)) * speed * 0.5f).xyz);
    float3 large  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 0.5f, 1.23f) + normalize(float2(-3, -2)) * speed).xyz);
    float3 medium = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 2.0f, 2.17f) + normalize(float2(2, -1)) * speed).xyz);
    float3 micro  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 4.0f, 2.89f) + normalize(float2(2, 2)) * speed).xyz);
    float patches = 0.6f + 0.8f * saturate(tex2D(TESR_samplerWater, p * 0.03f + speed * 0.1f).x);

    float2 tilt = (swell.xy * 0.8f + large.xy + medium.xy * 0.5f + micro.xy * 0.3f * near) * patches;
    float up = (swell.z * 0.8f + large.z + medium.z * 0.5f + micro.z * 0.3f * near) / max(choppiness, 1e-6f);
    return normalize(float3(tilt, up));
}

// ---------------------------------------------------------------------------------------------
// Wave shape: Gerstner waves (WaveHeight, WaveLength, WaveDirection, WaveSteepness). Twelve waves of
// falling length -- two interleaved sets of six, so no one pattern repeats -- rolling out within
// about 55 degrees of the wind direction, as a wind sea does (wider
// and they cross into lumps instead of rolling crests), each moving at the speed a real water wave
// of its length does. A Gerstner wave's surface bunches up toward its crests, so the crests come
// sharp and the troughs broad and flat, as on real water -- the shape a plain sine or a normal map
// cannot give. Evaluated per pixel on the world position: the shading follows the waves exactly,
// though the mesh itself stays flat. Each wave fades out once it is too short for the pixels it
// covers, so distant water does not alias. The normal-map waves ride on top as the fine detail.
// ---------------------------------------------------------------------------------------------
#define GERSTNER_WAVES 12
static const float GerstnerLength[GERSTNER_WAVES] = { 1.0f, 0.62f, 0.41f, 0.27f, 0.17f, 0.11f, 0.81f, 0.5f, 0.33f, 0.22f, 0.14f, 0.09f };
static const float GerstnerAngle[GERSTNER_WAVES]  = { 0.0f, 0.3f, -0.25f, 0.55f, -0.5f, 0.8f, -0.12f, 0.62f, -0.7f, 0.18f, 0.95f, -0.9f };
static const float GerstnerPhase[GERSTNER_WAVES]  = { 0.0f, 1.7f, 4.1f, 2.6f, 5.3f, 0.9f, 3.3f, 5.9f, 1.2f, 4.6f, 2.1f, 0.4f };
// Scales the twelve so together they reach the height one set of six did (their lengths add to 4.67,
// the six's to 2.58): WaveHeight keeps its meaning.
#define GERSTNER_NORM (2.58f / 4.67f)
#define WATER_GRAVITY (9.8f * WATER_UNITS_PER_METRE)   // units per second squared

// One wave's direction, wave number, amplitude and phase at worldPos. fade: 0-1, the pixel-size fade.
void getGerstnerWave(int i, float2 worldPos, float time, float pixelSize, float heightScale,
                     out float2 dir, out float k, out float amplitude, out float phase){
    float wavelength = TESR_WaterWaves.y * GerstnerLength[i];
    float angle = TESR_WaterWaves.z + GerstnerAngle[i];
    dir = float2(cos(angle), sin(angle));
    k = 6.2831853f / wavelength;
    float fade = saturate(wavelength / (pixelSize * 8.0f) - 1.0f);
    amplitude = TESR_WaterWaves.x * heightScale * GerstnerLength[i] * GERSTNER_NORM * fade;
    phase = k * dot(dir, worldPos) - sqrt(WATER_GRAVITY * k) * time + GerstnerPhase[i];
}

// Height alone, for the parallax search.
float getGerstnerHeight(float2 worldPos, float time, float pixelSize, float heightScale){
    float height = 0.0f;
    [unroll]
    for (int i = 0; i < GERSTNER_WAVES; i++) {
        float2 dir; float k; float amplitude; float phase;
        getGerstnerWave(i, worldPos, time, pixelSize, heightScale, dir, k, amplitude, phase);
        height += amplitude * sin(phase);
    }
    return height;
}

// Height, and the surface slope (dh/dx, dh/dy) with the Gerstner crest sharpening folded in.
float getGerstner(float2 worldPos, float time, float pixelSize, float heightScale, out float2 slope){
    float height = 0.0f;
    float3 n = float3(0.0f, 0.0f, 1.0f);
    float steepness = saturate(TESR_WaterWaves.w) / GERSTNER_WAVES;
    [unroll]
    for (int i = 0; i < GERSTNER_WAVES; i++) {
        float2 dir; float k; float amplitude; float phase;
        getGerstnerWave(i, worldPos, time, pixelSize, heightScale, dir, k, amplitude, phase);
        float s = sin(phase);
        float c = cos(phase);
        height += amplitude * s;
        n.xy -= dir * (k * amplitude * c);
        n.z -= steepness * saturate(amplitude / max(TESR_WaterWaves.x * heightScale * GerstnerLength[i] * GERSTNER_NORM, 1e-4f)) * s;   // sharpening fades with the wave
    }
    slope = -n.xy / max(n.z, 0.1f);
    return height;
}

// The tallest the waves get here, to put a height on a -1 (trough) to 1 (crest) scale.
float getGerstnerRange(float heightScale){
    return max(TESR_WaterWaves.x * heightScale * 2.58f, 1e-3f);
}

// Parallax (WaveParallax): a wave standing up hides what is behind it, and seen at a low angle the
// crest in front covers the trough beyond. The view ray is traced down from above the tallest crest
// to where it first meets the waves -- eight steps to find the first crossing, then two refinements
// between the steps either side of it -- so the shading is taken from the point the eye really sees,
// the nearest crest hiding what lies behind it, at any angle. Fades out with distance. Returns the
// world position to shade.
float2 getWaveParallax(float2 worldPos, float3 eyeDirection, float time, float pixelSize, float heightScale, float distance){
    float strength = TESR_WaterWaves2.y * (1.0f - saturate(distance / 4000.0f));
    // Along the view ray, the ground position moves by shift for each unit of height.
    float2 shift = eyeDirection.xy / max(eyeDirection.z, 0.25f) * strength;
    float range = getGerstnerRange(heightScale);

    // f = height of the ray above the waves: positive above the tallest crest, never positive below
    // the deepest trough, so a crossing always lies in between.
    float aboveH = range;
    float aboveF = range - getGerstnerHeight(worldPos + shift * range, time, pixelSize, heightScale);
    float belowH = -range;
    float belowF = -1.0f;
    bool found = false;
    [unroll]
    for (int i = 1; i <= 8; i++) {
        float h = range - range * 0.25f * i;
        float f = h - getGerstnerHeight(worldPos + shift * h, time, pixelSize, heightScale);
        if (!found && f <= 0.0f) {
            belowH = h;
            belowF = f;
            found = true;
        }
        if (!found) {
            aboveH = h;
            aboveF = f;
        }
    }
    [unroll]
    for (int j = 0; j < 2; j++) {
        float h = aboveH + (belowH - aboveH) * aboveF / max(aboveF - belowF, 1e-5f);
        float f = h - getGerstnerHeight(worldPos + shift * h, time, pixelSize, heightScale);
        if (f > 0.0f) { aboveH = h; aboveF = f; }
        else          { belowH = h; belowF = f; }
    }
    float hit = aboveH + (belowH - aboveH) * aboveF / max(aboveF - belowF, 1e-5f);
    return worldPos + shift * hit;
}

// Moves a wave texture position by a world-space offset, through the screen-space derivatives of
// both (the texture may be turned or scaled against the world, on placed water especially).
// ddx/ddy: top level only.
float2 getWaveTextureShift(float2 texPos, float2 worldPos, float2 worldShift){
    float2 wx = ddx(worldPos);
    float2 wy = ddy(worldPos);
    float det = wx.x * wy.y - wx.y * wy.x;
    float2 screen = abs(det) > 1e-8f ? float2(worldShift.x * wy.y - worldShift.y * wy.x, wx.x * worldShift.y - wx.y * worldShift.x) / det : 0.0f;
    return texPos + screen.x * ddx(texPos) + screen.y * ddy(texPos);
}

// Waves: the Gerstner shape with the normal-map detail on it, as slopes added together. heightOut:
// -1 in a trough to 1 on a crest. texPos: the wave texture position of the shaded point.
// refractionNormal: the same with the fine detail at 40%. The fine ripples glint and shade the
// surface, but at full strength they would scribble the bed seen through them into squiggles; what a
// bed seen through real water mostly follows is the larger waves.
float3 getWaves(float2 texPos, float2 worldPos, float distance, float4 waveParams, float time, float pixelSize, float heightScale, out float heightOut, out float3 refractionNormal){
    float2 slope;
    float height = getGerstner(worldPos, time, pixelSize, heightScale, slope);
    heightOut = TESR_WaterWaves.x > 0.0f ? clamp(height / getGerstnerRange(heightScale), -1.0f, 1.0f) : 0.0f;
    float3 detail = getWaveNormal(texPos, distance, waveParams);
    // A surface of slope s faces (-s, 1); the detail normal already faces its own way.
    float2 detailSlope = detail.xy / max(detail.z, 0.1f);
    refractionNormal = normalize(float3(-slope + detailSlope * 0.4f, 1.0f));
    return normalize(float3(-slope + detailSlope, 1.0f));
}

// Rain rings on the surface (WetWorld's rain amount). Four layers of the ripple texture, each
// dropping at its own time; faded out with distance.
float3 getRainRing(float2 uv, float time, float weight){
    float4 ripple = tex2D(TESR_RippleSampler, uv);
    ripple.yz = expand(ripple.yz);
    float period = frac(ripple.w + time);
    float timeFrac = period - 1.0f + ripple.x;
    float drop = saturate(0.2f + weight * 0.8f - period);
    float strength = drop * ripple.x * sin(clamp(timeFrac * 9.0f, 0.0f, 3.0f) * PI);
    return float3(ripple.yz * strength * 0.35f, 1.0f);
}

float3 getRainRipples(float2 texPos, float3 N, float distance, float rain){
    float fade = 1.0f - saturate(distance / 3500.0f);
    float4 weights = saturate(float4(1.0f, 0.75f, 0.5f, 0.25f) * rain * 4.0f) * 2.0f * fade;
    float4 times = float4(0.96f, 0.97f, 0.98f, 0.99f) * 0.07f * WATER_SCROLL_TIME;
    float2 uv = texPos * 5.0f;
    float3 r1 = getRainRing(uv + float2(0.25f, 0.0f), times.x, weights.x);
    float3 r2 = getRainRing(uv * 1.1f + float2(-0.55f, 0.3f), times.y, weights.y);
    float3 r3 = getRainRing(uv * 1.3f + float2(0.6f, 0.85f), times.z, weights.z);
    float3 r4 = getRainRing(uv * 1.5f + float2(0.5f, -0.75f), times.w, weights.w);
    float2 rings = weights.x * r1.xy + weights.y * r2.xy + weights.z * r3.xy + weights.w * r4.xy;
    return normalize(float3(N.xy + rings, N.z));
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
    float3 path = getGlint(N, sunDirection, eyeDirection, 0.3f);
    float3 sparkle = min(getGlint(N, sunDirection, eyeDirection, WATER_ROUGHNESS), 40.0f);
    glint += (sparkle * (path / (path + 1.0f)) * 0.15f + path * 0.1f) * TESR_WaterLighting2.z;
    return glint * 10.0f;
}

// How much of the reflection shows: Schlick with water's own 2%, so the water is clear looking down
// and a mirror at low angles; reflectivity (the water's own section) scales it.
float getFresnel(float3 N, float3 eyeDirection, float reflectivity){
    float fresnel = WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - saturate(dot(eyeDirection, N)), 5.0f);
    return saturate(fresnel * reflectivity);
}

// Wave scattering (WaveScattering): looking toward a low sun, sunlight shines through the thin tops
// and flanks of the waves and lights them up in the water's colour. Measured across the water: the
// wave normals lean only slightly off vertical, and the view down onto the water is well off the sun
// even with the sun straight ahead. Gone once the sun is under the horizon. In light units.
float3 getWaveScattering(float3 N, float3 eyeDirection, float3 sunDirection, float3 sunLight, float3 waterHue, float waveHeight){
    // Tilted flanks, and more on the crests, where the water is thinnest; little in the troughs.
    float crest = saturate(length(N.xy) * 5.0f) * lerp(0.4f, 1.6f, saturate(waveHeight * 0.5f + 0.5f));
    float2 viewFlat = -eyeDirection.xy * rsqrt(max(dot(eyeDirection.xy, eyeDirection.xy), 1e-6f));
    float2 sunFlat = sunDirection.xy * rsqrt(max(dot(sunDirection.xy, sunDirection.xy), 1e-6f));
    float towardSun = pow(saturate(dot(viewFlat, sunFlat)), 3.0f);
    float lowSun = (1.0f - saturate(sunDirection.z)) * saturate(sunDirection.z * 20.0f);
    return waterHue * sunLight * crest * towardSun * lowSun * TESR_WaterLighting.w;
}

// Reflection, blurred on choppy water (ReflectionBlur): four taps around the lookup, spread by how
// steep the waves are there, so calm water stays mirror-sharp. Linear. tex2Dproj: top level only.
float3 getBlurredReflection(float4 reflectionPos, float3 N){
    float radius = TESR_WaterLighting3.w * 0.006f * saturate(length(N.xy) * 4.0f) * reflectionPos.w;
    float4 sum = tex2Dproj(ReflectionMap, reflectionPos);
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4( radius, 0.0f, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(-radius, 0.0f, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(0.0f,  radius, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(0.0f, -radius, 0.0f, 0.0f));
    return linearize(sum * 0.2f).rgb;
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
// A factor on the sunlit bed. Needs TESR_CameraPosition. tex2Dlod, a level from the distance.
float getCaustics(float3 bedFromCamera, float depthBelow, float4 waveParams){
    float speed = WATER_SCROLL_TIME * 0.002f * waveParams.z;
    float2 world = (bedFromCamera.xy + TESR_CameraPosition.xy) / TESR_WaterLighting4.y;
    float lod = log2(max(length(bedFromCamera) / (TESR_WaterLighting4.y * 2.0f), 1.0f));
    float2 a = expand(tex2Dlod(TESR_samplerWater, float4(world + float2(0.8f, 0.6f) * speed * 3.0f, 0.0f, lod))).xy;
    float2 b = expand(tex2Dlod(TESR_samplerWater, float4(rotateWaterUV(world * 1.37f, 1.1f) - float2(0.3f, 0.95f) * speed * 3.0f, 0.0f, lod))).xy;
    float focus = saturate(1.0f - length(a + b) * 1.8f);
    focus = focus * focus * focus * 3.0f;
    float depthFade = saturate(depthBelow / 20.0f) * exp(-depthBelow / 500.0f);
    return focus * depthFade * TESR_WaterLighting4.x;
}

// ---------------------------------------------------------------------------------------------
// The edge
// ---------------------------------------------------------------------------------------------

// Foam (Foam, FoamWidth): where the view passes through the least water -- along the shore and
// around anything standing in the water -- solid at the edge and breaking into drifting patches made
// from the wave texture at two other sizes. By the path through the water, not the depth under the
// point: in front of a post that depth is small all the way down the post. tex2D: top level only.
// The patchy pattern foam breaks up into: the wave texture at two other sizes. tex2D: top level.
float getFoamNoise(float2 texPos, float4 waveParams){
    float speed = WATER_SCROLL_TIME * 0.002f * waveParams.z;
    float2 p = texPos * waveParams.y;
    return saturate(tex2D(TESR_samplerWater, rotateWaterUV(p * 3.0f, 0.4f) + float2(0.7f, 0.3f) * speed).x
                  + tex2D(TESR_samplerWater, rotateWaterUV(p * 7.0f, 1.9f) - float2(0.4f, 0.9f) * speed).y - 0.5f);
}

float getFoamMask(float pathLength, float foamNoise){
    float band = 1.0f - saturate(pathLength / TESR_WaterLighting3.y);
    band *= band;
    return saturate((band * 1.6f - (1.0f - foamNoise)) * 2.5f) * saturate(TESR_WaterLighting3.x);
}

// Whitecaps (Whitecaps): the tallest crests break into foam, patchy like the shore foam. waveHeight:
// -1 trough to 1 crest; the more Whitecaps, the lower on the wave they start.
float getWhitecaps(float waveHeight, float foamNoise){
    float amount = saturate(TESR_WaterWaves2.x);
    float cap = saturate((waveHeight - lerp(1.0f, 0.35f, amount)) * 5.0f);
    return saturate(cap * (foamNoise * 1.5f - 0.2f)) * amount;
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

// ---------------------------------------------------------------------------------------------
// DebugView: one term on its own, in place of the water.
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
// ---------------------------------------------------------------------------------------------
struct WaterDebug {
    float shadow;
    float3 transmittance;
    float fresnel;
    float3 scattering;
    float roughness;
    float3 pointLights;
    float2 path;
    float foam;
    float alpha;
    float caustics;
    float waveHeight;
    float depthCalibration;
    float straightDepth;
    float sceneEmpty;
    float3 depthCopies;
};

float3 getWaterDebugView(float view, WaterDebug d){
    float3 result = d.shadow;
    result = view > 1.5f ? d.transmittance : result;
    result = view > 2.5f ? d.fresnel : result;
    result = view > 3.5f ? saturate(d.scattering) : result;
    result = view > 4.5f ? saturate((d.roughness - WATER_ROUGHNESS) / 0.4f) : result;
    result = view > 5.5f ? saturate(d.pointLights) : result;
    result = view > 6.5f ? saturate(d.path.x / (20.0f * WATER_UNITS_PER_METRE)) : result;
    result = view > 7.5f ? saturate(d.path.y / (20.0f * WATER_UNITS_PER_METRE)) : result;
    result = view > 8.5f ? d.foam : result;
    result = view > 9.5f ? saturate(d.alpha) : result;
    result = view > 10.5f ? saturate(d.caustics) : result;
    result = view > 11.5f ? d.waveHeight * 0.5f + 0.5f : result;
    result = view > 12.5f ? saturate(d.depthCalibration * 0.5f) : result;
    result = view > 13.5f ? (d.sceneEmpty > 0.5f ? float3(1.0f, 0.0f, 0.0f) : saturate(d.straightDepth / (100.0f * WATER_UNITS_PER_METRE)).xxx) : result;
    result = view > 14.5f ? float3(saturate(d.depthCopies.xy / (20.0f * WATER_UNITS_PER_METRE)), d.depthCopies.z) : result;
    return result;
}
