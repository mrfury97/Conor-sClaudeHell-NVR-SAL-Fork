// Volumetric clouds, as a post-process for New Vegas Reloaded.
//
// After robobo1221's "Real time PBR Volumetric Clouds" (Shadertoy, single scattering), reworked
// for the game: only the clouds are taken (the sky, the god rays and the tonemapping are the
// game's and NVR's own), lit by the game's sun and sky, drifting with the weather's wind, as many
// of them as the weather calls for.
//
// Runs on the finished frame, before tonemapping, on the sky alone (what the depth buffer holds
// nothing in front of): anything the game draws -- mountains, buildings, the distant land -- stays
// in front of the clouds. Each sky pixel follows its view ray through a layer of cloud wrapped
// round the curve of the world, a few kilometres up, in steps; at each step with cloud in it, the
// sunlight reaching that point through the cloud above it is found by a few more steps toward the
// sun. Light scattered toward the eye is added up, and what lies behind is dimmed by what the
// cloud in front absorbs.
//
// Units: the maths runs in metres (a game unit is 1/70 m), as the shell's curvature at the
// Earth's radius would lose all its precision in game units.
//
// TESR_VolumetricCloudsShape  x: coverage (0-1, from the weather)  y: density  z: base height (m)  w: thickness (m)
// TESR_VolumetricCloudsMarch  x: steps  y: light steps  z: scale (m the noise tile spans)  w: strength
// TESR_VolumetricCloudsWind   xy: drift (m, wrapped to the noise tile)  z: slow change over time  w: DebugView
// TESR_VolumetricCloudsLight  x: sun brightness  y: sky brightness  z: silver lining  w: horizon fade (m)
//   DebugView 1: how much the clouds cover (white opaque)   2: the sunlight inside them   3: the sky mask

float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;          // the sun's direction, world space
float4 TESR_SunColor;
float4 TESR_SkyColor;              // top of the sky
float4 TESR_HorizonColor;
float4 TESR_SunAmount;             // x: 1 by day
float4 TESR_VolumetricCloudsShape;
float4 TESR_VolumetricCloudsMarch;
float4 TESR_VolumetricCloudsWind;
float4 TESR_VolumetricCloudsLight;

sampler2D TESR_SourceBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler3D TESR_CloudNoise : register(s2) < string ResourceName = "Clouds\NVR_CloudNoise.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

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

#define UNITS_PER_METRE 70.0f
#define EARTH_RADIUS    6371000.0f
#define MAX_STEPS       32
#define MAX_LIGHT_STEPS 8
#define PI              3.14159265f

static const float coverage   = saturate(TESR_VolumetricCloudsShape.x);
static const float density    = TESR_VolumetricCloudsShape.y;
static const float baseHeight = TESR_VolumetricCloudsShape.z;
static const float thickness  = max(TESR_VolumetricCloudsShape.w, 10.0f);
static const float steps      = clamp(TESR_VolumetricCloudsMarch.x, 4.0f, (float)MAX_STEPS);
static const float lightSteps = clamp(TESR_VolumetricCloudsMarch.y, 1.0f, (float)MAX_LIGHT_STEPS);
static const float scale      = max(TESR_VolumetricCloudsMarch.z, 100.0f);
static const float strength   = saturate(TESR_VolumetricCloudsMarch.w);
static const float debugView  = TESR_VolumetricCloudsWind.w;

// Interleaved gradient noise: each pixel starts its ray a different fraction of a step in, so the
// steps' banding becomes fine noise.
float getNoise(float2 pixel){
	return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

// Distance along a ray from a point hMetres above the ground, going up at mu (the ray's
// z), to the sphere topHeight above the ground. Written so nothing is subtracted from the square of
// the Earth's radius: (R + top)^2 - (R + h)^2 = (top - h)(2R + top + h).
float getShellDistance(float h, float mu, float topHeight){
	float b = (EARTH_RADIUS + h) * mu;
	float c = (topHeight - h) * (2.0f * EARTH_RADIUS + topHeight + h);
	return c > 0.0f ? c / (sqrt(max(b * b + c, 0.0f)) + b) : 0.0f;
}

// How much cloud there is at a point: horizontal position (m, drift included), height fraction in
// the layer (0 base, 1 top). The shape noise, cut off by the coverage -- more cloud, a lower
// threshold -- with the detail noise eating into its edges (nearness: 1 close by, 0 far off, where it
// would only sparkle); rounded off at the base and the top.
float getCloudDensity(float3 p, float heightFraction, float lod, float nearness){
	float3 c = p / scale;
	float4 n = tex3Dlod(TESR_CloudNoise, float4(c + float3(0.0f, 0.0f, TESR_VolumetricCloudsWind.z), lod));
	float profile = saturate(heightFraction * 5.0f) * saturate((1.0f - heightFraction) * 2.5f);
	float cloud = saturate((n.r * profile - (1.0f - coverage)) / max(coverage, 0.05f));
	[branch] if (cloud > 0.0f) {
		float detail = tex3Dlod(TESR_CloudNoise, float4(c * 4.0f + float3(0.37f, 0.61f, TESR_VolumetricCloudsWind.z * 2.0f), lod)).g;
		cloud = saturate(cloud - detail * 0.35f * nearness * (1.0f - cloud));
	}
	return cloud * density;
}

// Henyey-Greenstein, scaled so an even spread is 1.
float getPhase(float cosTheta, float g){
	float g2 = g * g;
	return (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
}

float4 VolumetricClouds(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_SourceBuffer, uv);

	// The sky alone: where the depth buffer holds nothing.
	float depth = readDepth(uv);
	bool sky = depth >= farZ * 0.99f;
	if (debugView == 3) return sky ? white : black;
	if (!sky) return color;

	float3 ray = normalize(toWorld(uv));
	float mu = ray.z;
	// Rays at or below the horizon never reach a layer above the camera.
	if (mu <= 0.0f) return color;

	// Where the ray is in the layer: from its base to its top, from the camera's height (m).
	float cameraHeight = TESR_CameraPosition.z / UNITS_PER_METRE;
	float topHeight = baseHeight + thickness;
	if (cameraHeight >= topHeight) return color;
	float tStart = getShellDistance(cameraHeight, mu, max(baseHeight, cameraHeight));
	float tEnd = getShellDistance(cameraHeight, mu, topHeight);
	// Long, near-horizontal paths are cut short and faded: steps stretched across tens of km would
	// only smear.
	float horizonFade = TESR_VolumetricCloudsLight.w;
	tEnd = min(tEnd, tStart + horizonFade);
	float stepLength = (tEnd - tStart) / steps;
	// Faded with distance, and over the last few degrees above the horizon, where the steps stretch
	// across kilometres and would only sparkle.
	float fade = (1.0f - smoothstep(horizonFade * 0.3f, horizonFade, tStart)) * smoothstep(0.0f, 0.12f, mu);
	if (fade <= 0.0f || stepLength <= 0.0f) return color;

	// Light. The sun (below the horizon it still lights the layer a little while) and the sky.
	float3 sunDir = normalize(TESR_SunDirection.xyz);
	float3 lightDir = normalize(float3(sunDir.xy, max(sunDir.z, 0.03f)));
	float sunUp = saturate((sunDir.z + 0.1f) * 6.0f) * saturate(TESR_SunAmount.x + 0.1f);
	float3 sunLight = linearize(TESR_SunColor.rgb) * sunUp * TESR_VolumetricCloudsLight.x;
	float3 skyTop = linearize(TESR_SkyColor.rgb) * TESR_VolumetricCloudsLight.y;
	float3 skyLow = linearize(TESR_HorizonColor.rgb) * TESR_VolumetricCloudsLight.y;
	// Two lobes: a strong one forward (the silver lining toward the sun), a soft one back.
	float cosTheta = dot(ray, lightDir);
	float silver = TESR_VolumetricCloudsLight.z;
	float phase = min(lerp(getPhase(cosTheta, -0.3f), getPhase(cosTheta, 0.75f), 0.5f * silver), 6.0f);
	float lightStepLength = thickness * 0.5f / lightSteps;
	// Coarser noise far away, where a step spans many noise texels.
	float lod = saturate(tStart / 30000.0f) * 2.0f;
	float nearness = saturate(1.0f - tStart / 20000.0f);

	float3 drift = float3(TESR_VolumetricCloudsWind.xy, 0.0f);
	float jitter = getNoise(uv / TESR_ReciprocalResolution.xy);
	float3 scattering = 0.0f;
	float transmittance = 1.0f;
	float sunlit = 0.0f;
	bool done = false;

	[loop]
	for (int i = 0; i < MAX_STEPS; i++) {
		// A fixed loop; the steps past the setting, and past a fully hidden sky, do nothing.
		[branch] if (!done && i < steps) {
			float t = tStart + stepLength * (i + jitter);
			float3 p = ray * t;
			// Height above the ground, the Earth curving away beneath the layer.
			float h = sqrt((EARTH_RADIUS + cameraHeight) * (EARTH_RADIUS + cameraHeight) + 2.0f * (EARTH_RADIUS + cameraHeight) * p.z + dot(p, p)) - EARTH_RADIUS;
			float heightFraction = saturate((h - baseHeight) / thickness);
			float3 world = float3(p.xy + drift.xy, h);
			float sigma = getCloudDensity(world, heightFraction, lod, nearness);
			[branch] if (sigma > 0.0f) {
				// Sunlight reaching this point through the cloud above it (Beer), with the "powder"
				// darkening of cloud seen from the lit side's inside.
				float lightDepth = 0.0f;
				[loop]
				for (int j = 1; j <= MAX_LIGHT_STEPS; j++) {
					[branch] if (j <= lightSteps) {
						float3 q = world + lightDir * (lightStepLength * j);
						float qFraction = saturate((q.z - baseHeight) / thickness);
						lightDepth += getCloudDensity(q, qFraction, lod + 1.0f, nearness) * lightStepLength;
					}
				}
				float beer = exp(-lightDepth);
				float powder = 1.0f - exp(-sigma * stepLength * 2.0f);
				float3 ambient = lerp(skyLow, skyTop, heightFraction) * (0.6f + 0.4f * heightFraction);
				float3 inScatter = sunLight * beer * lerp(1.0f, powder, 0.5f) * phase + ambient;
				// Energy-conserving step: the light scattered over this step, less what the step
				// itself absorbs of it.
				float stepTransmittance = exp(-sigma * stepLength);
				scattering += transmittance * inScatter * (1.0f - stepTransmittance);
				sunlit += transmittance * beer * (1.0f - stepTransmittance);
				transmittance *= stepTransmittance;
				done = transmittance < 0.01f;
			}
		}
	}

	float cover = (1.0f - transmittance) * fade * strength;
	if (debugView == 1) return float4(cover.xxx, 1.0f);
	if (debugView == 2) return float4(saturate(sunlit / max(1.0f - transmittance, 1e-3f)).xxx, 1.0f);

	float3 base = linearize(color.rgb);
	// The clouds' own light, and what they leave of the sky behind them, faded out toward the horizon.
	float3 clouds = base * transmittance + scattering;
	return float4(delinearize(lerp(base, clouds, fade * strength)), 1.0f);
}

technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 VolumetricClouds();
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
	}
}
