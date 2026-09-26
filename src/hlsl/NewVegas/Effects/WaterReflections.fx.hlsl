// Screen-space reflections on the water, as a post-process for New Vegas Reloaded.
//
// Runs on the finished frame, before tonemapping. Finds the water from the depth buffer (flat, at
// the water height), turns the view ray off the waves there, and marches the reflected ray across
// the screen against the same depth buffer the other effects use. Where it meets something, the
// water takes that pixel's colour in proportion to the Fresnel reflectance; where it finds nothing
// (the sky, anything off screen) the water keeps the reflection it was drawn with.
//
// TESR_WaterReflectionsData  x: Strength  y: MaxDistance (units)  z: Distortion (0-1, how much the
//                            waves bend the reflection)  w: DebugView
//   DebugView 1: water mask (white where the effect runs)
//             2: the search: green hit (brighter the more trusted), teal a hit where the ray went
//                behind the surface further than the thickness (its back or underside, not on the
//                screen, taken from its front); blue a hit thrown out as the
//                first-person weapon; yellow the ray went behind something (not the water), but by more
//                than the thickness allowed (passed behind it); dark red never behind anything; black not
//                searched
//             3: the reflected colour alone
//             4: the reflection amount (Fresnel * confidence * Strength)
//             5: the frame 1.5 m above each water point, through the effect's own projection:
//                posts and the pier show on the water a little below where they stand (magenta
//                off the screen) -- checks the projection on its own
//             6: the ray's first step that went behind something (not the water): its distance behind, over the
//                thickness allowed (green within, red beyond; black never behind)

float4 TESR_ReciprocalResolution;
float4 TESR_GameTime;
float4 TESR_WaterSettings;        // x: water height
float4 TESR_WaterWaves;           // x: WaveHeight  y: WaveLength  z: WaveDirection  w: WaveSteepness
float4 TESR_WaterWaveOrigin;      // xy the first wave layer's origin in the world, zw the second's
float4 TESR_WaterLighting3;       // w: ReflectionBlur
float4 TESR_WaterLighting4;       // z: 1 outdoors, 0 indoors  w: RippleSize
float4 TESR_WaterLighting5;       // x: 1 when the game's reflection map is drawn (GameReflections)  w: Ripples
float4 TESR_SkyColor;
float4 TESR_SkyLowColor;
float4 TESR_HorizonColor;
float4 TESR_WaterReflectionsData;

sampler2D TESR_SourceBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_samplerWater : register(s4) < string ResourceName = "Water\water_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler3D TESR_WaterWavesMap : register(s3) < string ResourceName = "Water\NVR_WaterWaves.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

struct VSOUT
{
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

#define SSR_STEPS   48
#define SSR_REFINE  5
#define WATER_F0    0.02f

static const float strength = TESR_WaterReflectionsData.x;
static const float maxDistance = max(TESR_WaterReflectionsData.y, 100.0f);
static const float distortion = saturate(TESR_WaterReflectionsData.z);
static const float debugView = TESR_WaterReflectionsData.w;

// The camera's axes in the world (the columns of the view transform, as toWorld reads them).
static const float3 camRight   = float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]);
static const float3 camUp      = float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]);
static const float3 camForward = float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);

// A camera-relative world position to (screen uv, view depth): the exact inverse of
// toWorld(uv) * readDepth(uv).
float3 toScreen(float3 position){
	float z = max(dot(position, camForward), 1e-3f);
	float x = dot(position, camRight);
	float y = dot(position, camUp);
	return float3(0.5f + 0.5f * TESR_ProjectionTransform[0][0] * x / z, 0.5f - 0.5f * TESR_ProjectionTransform[1][1] * y / z, z);
}

// ---------------------------------------------------------------------------------------------
// The wave field of the water shader (Shaders/Includes/ComplexWater.hlsl, getWaveField and
// getWaveFieldSurface): the same texture, tiling and timing, so the reflection bends with the
// waves the water shows. Slopes only.
// ---------------------------------------------------------------------------------------------
#define WAVE_TEX_SIZE        128.0f
#define WAVE_TEX_PATCH       15.0f
#define WAVE_TEX_PERIOD      12.0f
#define WAVE_TEX_PEAK        3.0f
#define WAVE_TEX_HEIGHT_MAX  3.5205f
#define WAVE_TEX_SLOPE_SCALE 147.9828f
#define WAVE_LAYER_B_SCALE   0.37f
#define WAVE_LAYER_B_ANGLE   0.55f
#define WAVE_LAYER_B_OFFSET  float2(0.31f, 0.67f)
#define WAVE_LAYER_B_TIME    0.43f
#define WATER_UNITS_PER_METRE 70.0f

float2 sampleWaveSlope(float2 worldPos, float2 dir, float tile, float2 offset, float time, float lod){
	float2 uv = float2(dot(worldPos, dir), dot(worldPos, float2(-dir.y, dir.x))) / tile + offset;
	float2 slope = tex3Dlod(TESR_WaterWavesMap, float4(uv, time, lod)).rg * 2.0f - 1.0f;
	// Stored along the layer's own axes: turn it back into the world.
	return slope.x * dir + slope.y * float2(-dir.y, dir.x);
}

float2 getWaveSlope(float2 worldPos, float pixelSize){
	float time = TESR_GameTime.z;
	float angle = TESR_WaterWaves.z;
	float2 dirA = float2(cos(angle), sin(angle));
	float2 dirB = float2(cos(angle + WAVE_LAYER_B_ANGLE), sin(angle + WAVE_LAYER_B_ANGLE));
	float tileA = max(TESR_WaterWaves.y, 1.0f) * WAVE_TEX_PEAK;
	float tileB = tileA * WAVE_LAYER_B_SCALE;
	float patchUnits = WAVE_TEX_PATCH * WATER_UNITS_PER_METRE;
	float timeA = time / (WAVE_TEX_PERIOD * sqrt(tileA / patchUnits));
	float timeB = time / (WAVE_TEX_PERIOD * sqrt(tileB / patchUnits)) + WAVE_LAYER_B_TIME;
	float lodA = max(log2(pixelSize * WAVE_TEX_SIZE / tileA), 0.0f);
	float lodB = max(log2(pixelSize * WAVE_TEX_SIZE / tileB), 0.0f);
	float sigma = TESR_WaterWaves.x * 0.25f;
	float slopeScale = sigma * WAVE_TEX_SLOPE_SCALE / tileA * lerp(0.5f, 1.5f, saturate(TESR_WaterWaves.w));
	return slopeScale * (sampleWaveSlope(worldPos - TESR_WaterWaveOrigin.xy, dirA, tileA, 0.0f, timeA, lodA)
	                   + sampleWaveSlope(worldPos - TESR_WaterWaveOrigin.zw, dirB, tileB, WAVE_LAYER_B_OFFSET, timeB, lodB));
}

float2 rotateWaterUV(float2 uv, float angle){
	float s = sin(angle);
	float c = cos(angle);
	return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

// The gusts of the water shader (getWaveGusts): a factor on the slopes, 0.65 to 1.35.
float getWaveGusts(float2 worldPos){
	float angle = TESR_WaterWaves.z;
	float2 dirA = float2(cos(angle), sin(angle));
	float tileA = max(TESR_WaterWaves.y, 1.0f) * WAVE_TEX_PEAK;
	float2 p = worldPos - TESR_WaterWaveOrigin.xy;
	float2 uv = float2(dot(p, dirA), dot(p, float2(-dirA.y, dirA.x))) / (tileA * 24.0f) + float2(0.5f, 0.25f);
	float g = tex3Dlod(TESR_WaterWavesMap, float4(uv, TESR_GameTime.z / 600.0f, 3.0f)).b;
	return lerp(0.65f, 1.35f, saturate((g - 0.5f) * 2.5f + 0.5f));
}

// The ripples of the water shader (getWaveNormal: Ripples, RippleSize), as a world slope, so the
// reflection wobbles with the same ripples the water is shaded with. tex2Dgrad, the gradients from
// the pixel's world footprint (dwx, dwy: ddx and ddy of the world position, taken at the top).
float2 getRippleSlope(float2 worldPos, float2 dwx, float2 dwy, float distance){
	float size = max(TESR_WaterWaves.y, 1.0f) * max(TESR_WaterLighting4.w, 0.05f);
	float2 p = worldPos / size;
	float2 gx = dwx / size;
	float2 gy = dwy / size;
	float2 wind = float2(cos(TESR_WaterWaves.z), sin(TESR_WaterWaves.z));
	float time = TESR_GameTime.z;
	#define RIPPLE_TRAVEL(s) (sqrt(109.3f * size / ((s) * 8.0f)) * time / size)
	#define RIPPLE(s, turn, angle) expand(tex2Dgrad(TESR_samplerWater, rotateWaterUV((p - rotateWaterUV(wind, turn) * RIPPLE_TRAVEL(s)) * (s), angle), rotateWaterUV(gx * (s), angle), rotateWaterUV(gy * (s), angle)).xyz)
	float near = 1.0f - saturate(distance / 3000.0f);
	float3 swell  = RIPPLE(0.15f,  0.10f, 0.61f);
	float3 large  = RIPPLE(0.5f,   0.25f, 1.23f);
	float3 medium = RIPPLE(2.0f,  -0.30f, 2.17f);
	float3 micro  = RIPPLE(4.0f,   0.45f, 2.89f);
	float patches = 0.6f + 0.8f * saturate(tex2Dgrad(TESR_samplerWater, (p - wind * RIPPLE_TRAVEL(0.15f) * 0.2f) * 0.03f, gx * 0.03f, gy * 0.03f).x);
	#undef RIPPLE
	#undef RIPPLE_TRAVEL
	float2 tilt = (swell.xy * 0.3f + large.xy + medium.xy * 0.5f + micro.xy * 0.3f * near) * patches;
	float up = (swell.z * 0.3f + large.z + medium.z * 0.5f + micro.z * 0.3f * near) / max(TESR_WaterLighting5.w, 1e-6f);
	float3 n = normalize(float3(tilt, up));
	return n.xy / max(n.z, 0.1f);
}

// The water shader's sky along a reflected ray (getSkyReflection), what outdoor water reflects when
// the game's reflection map is not drawn. Linear.
float3 getSkyReflection(float3 R){
	float up = saturate(R.z);
	float3 low = lerp(linearize(TESR_HorizonColor.rgb), linearize(TESR_SkyLowColor.rgb), saturate(up * 4.0f));
	return lerp(low, linearize(TESR_SkyColor.rgb), saturate(up * 1.5f - 0.2f));
}

// readDepth without derivatives, for the loops.
float readDepthLod(float2 uv){
	return tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x * farZ;
}

// Water (by height) within the tolerance of the depth buffer at that distance.
bool isWaterHeight(float worldZ, float depth){
	return abs(worldZ - TESR_WaterSettings.x) < 2.0f + depth * 0.002f;
}

bool isViewModel(float2 uv){
	float viewmodelDepth = tex2Dlod(TESR_DepthBufferViewModel, float4(uv, 0.0f, 0.0f)).x;
	return (invertedDepth == 0 && viewmodelDepth < 0.9f) || (invertedDepth > 0 && viewmodelDepth > 0.01f);
}

// Interleaved gradient noise: a different start along the ray for neighbouring pixels, so the
// steps' banding turns into fine noise.
float getNoise(float2 pixel){
	return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float4 WaterReflections(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_SourceBuffer, uv);

	float depth = readDepth(uv);
	float3 surface = toWorld(uv) * depth;                        // camera-relative
	float3 worldPos = surface + TESR_CameraPosition.xyz;
	float2 dwx = ddx(worldPos.xy);
	float2 dwy = ddy(worldPos.xy);
	float pixelSize = max(length(dwx), length(dwy));

	bool water = depth < farZ * 0.99f && surface.z < 0.0f && isWaterHeight(worldPos.z, depth);
	if (debugView == 1) return water ? white : black;
	if (!water) return color;
	if (debugView == 5) {
		float3 above = toScreen(surface + float3(0.0f, 0.0f, 105.0f));
		return all(above.xy == saturate(above.xy)) ? tex2Dlod(TESR_SourceBuffer, float4(above.xy, 0.0f, 0.0f)) : magenta;
	}

	// The normal of the waves, and the view ray turned off it. Kept pointing up: a ray turned into
	// the water would have nothing to reflect.
	float3 eyeDirection = normalize(surface);                   // camera to surface
	// The waves (the wave field, scaled by the gusts, as the water shader has them) aim the reflected
	// ray. The fine ripples on them do not: each pixel following its own ripple splinters a reflection
	// into long jagged streaks, where on real water thousands of them only smear it -- so they widen
	// the blur (rippleSlope, below) and weigh the reflection (the Fresnel, on the full normal).
	float2 slope = 0.0f;
	float2 rippleSlope = 0.0f;
	[branch] if (distortion > 0.0f) {
		float gust = getWaveGusts(worldPos.xy);
		slope = getWaveSlope(worldPos.xy, pixelSize) * gust * distortion;
		[branch] if (TESR_WaterLighting5.w > 0.0f) rippleSlope = getRippleSlope(worldPos.xy, dwx, dwy, length(surface.xy)) * gust;
	}
	float3 N = normalize(float3(-slope, 1.0f));
	float3 R = reflect(eyeDirection, N);
	R = normalize(float3(R.xy, max(R.z, 0.02f)));

	float cosTheta = saturate(dot(-eyeDirection, normalize(float3(-slope - rippleSlope * distortion, 1.0f))));
	// As the water shader weighs its own reflection (getFresnel): real water's.
	float fresnel = WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - cosTheta, 5.0f);
	// Too little reflection to see (looking steeply down, or a low Strength): not
	// worth the search. Its whole result would change the water by under 2%.
	if (debugView == 0 && fresnel * strength < 0.02f) return color;

	// The ray, from the surface to MaxDistance or to just in front of the camera, whichever is
	// nearer; then as a segment on the screen, cut where it leaves the screen. Along the segment,
	// uv moves linearly and 1/depth does (perspective).
	float rayForward = dot(R, camForward);
	float rayLength = maxDistance;
	if (rayForward < 0.0f) rayLength = min(rayLength, (depth - nearZ * 2.0f - 1.0f) / -rayForward);
	float3 start = toScreen(surface);
	float3 end = toScreen(surface + R * max(rayLength, 1.0f));
	float2 delta = end.xy - start.xy;
	float tMax = 1.0f;
	if (delta.x > 0.0f) tMax = min(tMax, (1.0f - start.x) / delta.x);
	if (delta.x < 0.0f) tMax = min(tMax, -start.x / delta.x);
	if (delta.y > 0.0f) tMax = min(tMax, (1.0f - start.y) / delta.y);
	if (delta.y < 0.0f) tMax = min(tMax, -start.y / delta.y);
	float2 pixels = abs(delta * tMax) / TESR_ReciprocalResolution.xy;
	float directionFade = smoothstep(-0.9f, -0.5f, rayForward);

	float status = 0.0f;                                        // 0 not marched, 1 nothing found, 2 hit
	float hitT = 0.0f;
	float confidence = 0.0f;
	float firstBehind = -1.0f;                                  // DebugView 2 and 6: behind / thickness at the first step behind anything
	bool loose = false;                                         // DebugView 2: a hit on the surface standing in for its back
	float hitBehind = 0.0f;                                     // behind / thickness at the hit
	float hitDistance = 0.0f;                                   // world units along the ray to the hit
	float hitDepth = 1.0f;                                      // the hit's view depth
	if (rayLength > 1.0f && max(pixels.x, pixels.y) > 2.0f && directionFade > 0.0f) {
		status = 1.0f;
		float k0 = 1.0f / start.z;
		float k1 = 1.0f / end.z;
		float jitter = getNoise(uv / TESR_ReciprocalResolution.xy);
		// As many steps as the ray is long on the screen, one about every 8 pixels (12 to 48).
		float steps = clamp(ceil(max(pixels.x, pixels.y) / 8.0f), 12.0f, SSR_STEPS);
		float before = 0.0f;
		float beforeZ = start.z;
		float after = 0.0f;
		bool found = false;

		[loop]
		for (int i = 1; i <= SSR_STEPS; i++) {
			// The loop keeps its fixed count (no break or continue, as found explains); the steps past
			// the ray's own count, or past the hit, skip their work.
			[branch] if (!found && i <= steps) {
				float t = tMax * (i - 1.0f + jitter) / (steps - 1.0f + jitter);
				float2 rayUV = lerp(start.xy, end.xy, t);
				float rayZ = 1.0f / lerp(k0, k1, t);
				float sceneZ = readDepthLod(rayUV);
				float thickness = max(abs(rayZ - beforeZ) * 1.5f, 30.0f + rayZ * 0.01f);
				float behind = rayZ - sceneZ;
				// Behind what the screen shows there, but not so far that the ray passed behind it; and
				// not the water itself (it cannot reflect itself).
				bool solid = !found && behind > 0.0f && !isWaterHeight(TESR_CameraPosition.z + toWorld(rayUV).z * sceneZ, sceneZ);
				if (firstBehind < 0.0f && solid) firstBehind = behind / thickness;
				// Or further behind, where what the ray can only have gone through is its back or
				// underside (the underside of the pier, the back of a post), which the screen does not
				// show: the surface in front stands in for it. Only for a ray heading away from the
				// camera, and only for a surface no nearer the camera than the ray's start -- nothing
				// nearer can be in the ray's way, so the ray passed behind it.
				bool reached = solid && rayForward > 0.0f && sceneZ > start.z;
				// The first such step is the hit. Recorded, not left by break: the loop runs to the end
				// (as the water shader's own traces do), each step after the hit doing nothing.
				if (solid && (behind < thickness || reached)) {
					loose = behind >= thickness;
					hitBehind = behind / thickness;
					after = t;
					found = true;
				}
				if (!found) {
					before = t;
					beforeZ = rayZ;
				}
			}
		}

		if (found) {
			// Close in on where the ray passes behind the surface.
			[unroll]
			for (int j = 0; j < SSR_REFINE; j++) {
				float t = (before + after) * 0.5f;
				float rayZ = 1.0f / lerp(k0, k1, t);
				if (rayZ > readDepthLod(lerp(start.xy, end.xy, t))) after = t;
				else before = t;
			}
			hitT = after;
			float2 hitUV = lerp(start.xy, end.xy, hitT);
			status = 3.0f;                                      // thrown out as the weapon, unless:
			if (!isViewModel(hitUV)) {
				status = 2.0f;
				float hitZ = 1.0f / lerp(k0, k1, hitT);
				float along = abs(end.z - start.z) > 1.0f ? saturate((hitZ - start.z) / (end.z - start.z)) : hitT;
				float2 edge = min(hitUV, 1.0f - hitUV);
				float edgeFade = smoothstep(0.0f, 0.08f, min(edge.x, edge.y));
				float distanceFade = 1.0f - smoothstep(0.7f, 1.0f, along);
				// A stand-in for an underside or a back fades the further behind the surface the ray
				// went: its edges ease out instead of switching off where the rays start to miss.
				float looseFade = loose ? lerp(1.0f, 0.4f, saturate((hitBehind - 1.0f) / 8.0f)) : 1.0f;
				confidence = edgeFade * distanceFade * directionFade * looseFade;
				hitDistance = along * rayLength;
				hitDepth = hitZ;
			}
		}
	}

	if (debugView == 2) {
		if (status == 2.0f) return loose ? float4(0.0f, max(confidence, 0.2f) * 0.6f, max(confidence, 0.2f), 1.0f) : float4(0.0f, max(confidence, 0.2f), 0.0f, 1.0f);
		if (status == 3.0f) return blue;
		if (status == 1.0f) return firstBehind > 0.0f ? yellow : float4(0.3f, 0.0f, 0.0f, 1.0f);
		return black;
	}
	if (debugView == 6) return firstBehind < 0.0f ? black : (firstBehind <= 1.0f ? float4(0.0f, 1.0f - firstBehind * 0.7f, 0.0f, 1.0f) : float4(saturate(firstBehind / 10.0f) * 0.7f + 0.3f, 0.0f, 0.0f, 1.0f));
	if (confidence <= 0.0f) return debugView >= 3 ? black : color;

	// Blurred as rippled water blurs a reflection: by a cone around the ray, so sharp where a post
	// meets the water and softer up it, the further the ray went (the cone widens with the water's
	// ReflectionBlur, the slope here, and most with the ripples). Eight taps, in two rings. Wider where the front stands in
	// for an underside, whose single edge row would otherwise streak down the water; that underside is
	// in its own shade (the pier's, over the water), so it is taken darker than the lit front.
	float2 reflectedUV = lerp(start.xy, end.xy, hitT);
	float worldPerPixel = hitDepth * 2.0f * TESR_ReciprocalResolution.y / TESR_ProjectionTransform[1][1];
	float cone = 0.01f + 0.03f * saturate(TESR_WaterLighting3.w / 3.0f) + length(slope) * 0.05f + length(rippleSlope) * 0.25f;
	float radius = clamp(hitDistance * cone / max(worldPerPixel, 1e-3f), loose ? 4.0f : 1.0f, 16.0f);
	float2 r1 = TESR_ReciprocalResolution.xy * radius * 0.7071f;
	float2 r2 = TESR_ReciprocalResolution.xy * radius * 0.5f;
	float3 reflection = linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2(-r1.x, -r1.y), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2( r1.x, -r1.y), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2(-r1.x,  r1.y), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2( r1.x,  r1.y), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2( r2.x,  0.0f), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2(-r2.x,  0.0f), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2( 0.0f,  r2.y), 0.0f, 0.0f)).rgb)
	                  + linearize(tex2Dlod(TESR_SourceBuffer, float4(reflectedUV + float2( 0.0f, -r2.y), 0.0f, 0.0f)).rgb);
	reflection *= loose ? 0.125f * 0.3f : 0.125f;
	float amount = saturate(fresnel * confidence * strength);
	if (debugView == 3) return float4(delinearize(reflection), 1.0f);
	if (debugView == 4) return float4(amount.xxx, 1.0f);

	// Only the reflection is replaced, not the water under it: the foam, the whitecaps, the sun glint
	// and the glow through the waves stay as they were. With GameReflections off, the water drew the
	// sky along its reflected ray, which is known: that is taken out and the found reflection put in
	// its place. With the game's reflection map, which cannot be read here, the water is blended
	// toward the found reflection instead.
	float3 base = linearize(color.rgb);
	// Indoors the water reflects the room's fog, not the sky: blended too.
	bool knownSky = TESR_WaterLighting5.x < 0.5f && TESR_WaterLighting4.z > 0.5f;
	float3 result = knownSky ? max(base + amount * (reflection - getSkyReflection(R)), 0.0f)
	                         : lerp(base, reflection, amount);
	// Alpha 1, as every effect writes: the frame's own alpha on the water is the water shader's
	// shoreline fade, and blending by it would throw the reflection away.
	return float4(delinearize(result), 1.0f);
}

technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 WaterReflections();
		// Written as is: whatever blending or alpha test the frame left on would otherwise weigh the
		// result by the water's alpha (its shoreline fade), as SMAA also guards against.
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
	}
}
