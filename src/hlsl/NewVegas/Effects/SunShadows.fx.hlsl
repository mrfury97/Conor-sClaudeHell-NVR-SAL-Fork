// Image space shadows shader for Oblivion Reloaded

float4x4 TESR_WorldViewProjectionTransform;
float4 TESR_ReciprocalResolution;
float4 TESR_ViewSpaceLightDir;
float4 TESR_ShadowData; // x: quality, y: darkness, z: texel size
float4 TESR_ShadowScreenSpaceData; // x: Enabled, y: blurRadius, z: renderDistance, w: intensity
float4 TESR_ShadowContactData; // x: strength, y: ray length, z: thickness, w: max distance
float4 TESR_SunAmbient;
float4 TESR_ShadowFade; // x: sunset attenuation, y: shadows maps active, z: point lights shadows active
// Injected as a D3DXMACRO by EffectRecord from [Shaders.ShadowsExteriors.Main] ForwardShadows,
// exactly as it is for the game shaders -- so the two halves cannot disagree.
// 1 = the object/terrain/parallax shaders evaluate the sun cascades themselves, so this
//     effect must not also apply them. 0 = stock deferred behaviour.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif
float4 TESR_ShadowForwardData; // x: 1 when the forward path is SUPPRESSED

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_ShadowAtlas : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowBuffer : register(s3)  = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowAtlas : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

// Lamps' contact shadows indoors (technique 1). ShaderManager::GetNearbyLights fills the lamps.
float4 TESR_LampContactData;        // x strength (0 off), y ray length, z thickness, w max distance
float4 TESR_LampContactExtra;       // x samples per ray (ContactSamples), y lamps marched per pixel (ContactLamps), z the longest ray on screen (ContactScreenLength, pixels at 1080 lines)
float4 TESR_LampContactShape;       // x the rays' start off the surface (ContactNormalOffset, units), y the lamps' radius for their softening (PCSSLightSize x ContactSoftness)
float4 TESR_ContactLampPosition[24]; // the nearest point lights: xyz world position, w radius (0 past the last); ContactLampsMax
float4 TESR_ContactLampColor[24];    // rgb colour x dimmer, w shadow info: slot * 2 + fade, -1 none (Shaders/Includes/PointShadow.hlsl)
float4 TESR_ContactLampAnchor[24];   // its slot's anchor (world), w the slot's radius
float4 TESR_PointShadowData;         // as Shaders/Includes/PointShadow.hlsl
float4 TESR_PointShadowParams;
float4 TESR_InverseSquare;           // as Shaders/Includes/InverseSquare.hlsl: x 0 off, else the fill light radius; y q, z k, w scale / (1 - k)
float4 TESR_ShadowAmbientLight;      // the ambient and the cell's directional light, as the object shaders get them
float4 TESR_ShadowSunLight;

#define CONTACT_STEPNUM 12
// Growth of the ray with depth, capped. Contact shadows are for detail the shadow maps are too
// coarse to catch, which only exists close up; a ray that kept growing with distance shadowed
// whole slopes and trees far off that the maps already cover, as a soft halo around their
// shadows.
#define CONTACT_GROWTH (1.0f / 2000.0f)
#define CONTACT_MAX_SCALE 3.0f

static const float DARKNESS = 1-TESR_ShadowData.y;
static const float SSS_MAXDEPTH = TESR_ShadowScreenSpaceData.z * TESR_ShadowScreenSpaceData.x;
// Where the denoising blur stops: no further than RenderDistance, and no further than contact
// shadows reach, since past that the buffer is uniformly lit and blurring it does nothing.
// 0 when contact shadows are off, which clips every pixel and skips both blur passes' work.
static const float CONTACT_BLUR_END = (TESR_ShadowContactData.x > 0.0f) ? min(SSS_MAXDEPTH, TESR_ShadowContactData.w) : 0.0f;


struct VSOUT
{
	float4 vertPos : POSITION;
	float4 normal : TEXCOORD1;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/BlurDepth.hlsl"


VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

// returns a semi random float3 between 0 and 1 based on the given seed. (blue noise)
// tailored to return a different value for each uv coord of the screen.
float3 random(float2 seed)
{
	return tex2D(TESR_NoiseSampler, (seed/256 + 0.5) / TESR_ReciprocalResolution.xy).xyz;
}

float4 ScreenSpaceShadow(VSOUT IN) : COLOR0
{
	// Contact shadows: many samples along a short ray toward the sun, for the shadow a prop
	// leaves where it meets the ground, in a crease or at a character's feet. The shadow maps
	// are too coarse for those; anything larger is theirs. Result in the red channel, point
	// light attenuation passes through in green.
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_PointShadowBuffer, uv);
	// tex2Dlod, not random(): the compiler sinks this sample to where it is used, past the dynamic
	// returns below, and warns (X4121) about a gradient sample in flow control. The noise has no
	// use for mips anyway.
	float3 random3 = tex2Dlod(TESR_NoiseSampler, float4((uv / 256 + 0.5) / TESR_ReciprocalResolution.xy, 0.0f, 0.0f)).xyz;

	if (!TESR_ShadowScreenSpaceData.x || TESR_ShadowContactData.x <= 0.0f) return float4(1.0, color.g, 0, 1);

	// reconstructPosition, sampled with tex2Dlod: it follows a dynamic return.
	float4 originClip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).y, 1.0f);
	float4 originView = mul(originClip, TESR_InvProjectionTransform);
	const float3 origin = originView.xyz / originView.w;
	if (origin.z > TESR_ShadowContactData.w) return float4(1.0, color.g, 0, 1);

	// A surface facing away from the sun has no sunlight for a contact shadow to take away, and
	// the composite would scale whatever this found by a sun share of zero. Skipping it spares
	// the whole march on roughly every surface in the sun's shade. The buffer holds view-space
	// normals, the space TESR_ViewSpaceLightDir is in.
	float3 viewNormal = tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f;
	float NdotL = dot(viewNormal, TESR_ViewSpaceLightDir.xyz);
	if (NdotL <= 0.0f) return float4(1.0, color.g, 0, 1);
	// Near the terminator the march runs almost along the receiving surface, and bilinear depth
	// reads there are slightly off its true plane, an error that cycles with the sub-pixel sample
	// phase: a small fixed bias flips the test on and off in bands across the ray (horizontal black
	// lines on sun-lit surfaces under a high sun). Fade the term in as the surface turns toward the
	// sun (ported from NVR UNOFFICIAL Optimized, P8-P26).
	float facing = saturate(NdotL * 8.0f);

	// The ray grows a little with distance so it keeps a usable size on screen, up to a cap.
	float scale = min(1.0f + origin.z * CONTACT_GROWTH, CONTACT_MAX_SCALE);
	float3 contactStep = TESR_ViewSpaceLightDir.xyz * (TESR_ShadowContactData.y * scale / CONTACT_STEPNUM);
	float contactThickness = TESR_ShadowContactData.z * scale;
	// The self-intersection bias also grows with distance (same port), so grazing lit faces far off do not band.
	float contactBias = max(contactThickness * 0.05f, origin.z * 0.002f);

	// Marched in clip space. Projection is linear in homogeneous coordinates, so a view-space
	// ray maps to a straight line there: project the start and one step once, then each sample
	// costs an add and a divide instead of a full matrix multiply. View depth is linear along
	// the ray too, so it steps the same way.
	//
	// tex2Dlod: the early outs above are dynamic flow, and a gradient sample after them is illegal.
	float3 startPos = origin + contactStep * random3.g;   // jittered start hides the step pattern
	float4 clipPos = mul(float4(startPos, 1.0f), TESR_ProjectionTransform);
	float4 clipStep = mul(float4(contactStep, 0.0f), TESR_ProjectionTransform);
	float rayDepth = startPos.z;

	// The nearer the occluder along the ray, the darker the shadow, fading to nothing at the
	// ray's end. A shadow is darkest where an object meets the surface and fades away from it;
	// a flat-dark mask instead ended in a hard rim around everything that also cast a
	// shadow-map shadow, which read as a second, softer shadow behind the real one.
	//
	// Every hit counts, grass blades included: grass is not in the shadow maps, so this is the
	// only shadow it casts.
	float contact = 0.0f;
	[unroll]
	for (int j = 0; j < CONTACT_STEPNUM; j++) {
		clipPos += clipStep;
		rayDepth += contactStep.z;
		float2 sampleUV = clipPos.xy / clipPos.w * float2(0.5f, -0.5f) + 0.5f;
		float delta = rayDepth - tex2Dlod(TESR_DepthBuffer, float4(sampleUV, 0.0f, 0.0f)).x * farZ;
		contact = (delta > contactBias && delta < contactThickness) ? max(contact, 1.0f - (float)j / CONTACT_STEPNUM) : contact;
	}

	float fade = 1.0f - smoothstep(TESR_ShadowContactData.w * 0.8f, TESR_ShadowContactData.w, origin.z);
	color.r = 1.0f - saturate(contact * TESR_ShadowContactData.x * fade * facing);
	return color;
}

// --- Lamps' contact shadows (indoors, with ForwardPointShadows) ---------------------------------
// A march toward the lamps lighting each pixel most (ContactLamps). The object shaders already
// took each lamp's cube shadow out of that lamp's own light (Shaders/Includes/PointShadow.hlsl), so
// this cannot simply darken the pixel: it works out the lamps' shares of the pixel's light -- the
// ambient and the cell's light, and each lamp's light at the pixel as the shaders light it (linear:
// squared) -- and takes away the part of each marched lamp's share that its contact shadow covers
// and its cube shadow did not. The result, the fraction of the pixel's light left, goes in red;
// ShadowsInteriors.fx (technique 1) applies it.

static const float LAMP_CONTACT_END = (TESR_LampContactData.x > 0.0f) ? TESR_LampContactData.w : 0.0f;
// The first blur pass goes a little farther: the second reads its neighbours from it, and past where
// the first stopped its target holds whatever an earlier frame left there (a line at that distance).
static const float LAMP_CONTACT_END_H = LAMP_CONTACT_END * 1.1f;

float LampLuma(float3 c) {
    return dot(c, float3(0.3f, 0.59f, 0.11f));
}

// Shaders/Includes/PointShadow.hlsl's lookup with one bilinear percentage-closer filter (its
// PointShadowVisibility), for the same result.
void LampAtlasTexel(float slot, float3 dir, out float2 corner, out float2 inTile) {
    float3 a = abs(dir);
    float xMajor = (a.x >= a.y && a.x >= a.z) ? 1.0f : 0.0f;
    float yMajor = (1.0f - xMajor) * (a.y >= a.z ? 1.0f : 0.0f);
    float zMajor = 1.0f - xMajor - yMajor;
    float major = dot(dir, float3(xMajor, yMajor, zMajor));
    float s = major > 0.0f ? 1.0f : -1.0f;
    float sc = xMajor * (-s * dir.z) + yMajor * dir.x + zMajor * (s * dir.x);
    float tc = yMajor * (s * dir.z) - (1.0f - yMajor) * dir.y;
    float tile = slot * 6.0f + yMajor * 2.0f + zMajor * 4.0f + (s > 0.0f ? 0.0f : 1.0f);
    float columns = TESR_PointShadowParams.y;
    float row = floor((tile + 0.5f) / columns);
    float size = TESR_PointShadowData.w;
    inTile = clamp((float2(sc, tc) / abs(major) * 0.5f + 0.5f) * size, 1.0f, size - 1.0f);
    corner = float2(tile - row * columns, row) * size;
}

float LampShadowTexel(float2 c, float distance, float radius) {
    float stored = tex2Dlod(TESR_PointShadowAtlas, float4(c * TESR_PointShadowData.yz, 0.0f, 0.0f)).r;
    float blocked = stored < distance ? 1.0f : 0.0f;
    [flatten] if (TESR_PointShadowParams.x > 0.0f)
        blocked *= saturate(stored * radius / TESR_PointShadowParams.x);
    return blocked;
}

float LampCubeVisibility(float info, float4 anchor, float3 worldPos, float3 normal) {
    float visibility = 1.0f;
    [branch] if (info >= 0.0f && TESR_PointShadowData.x > 0.0f && anchor.w > 0.0f) {
        float3 toLight = anchor.xyz - worldPos;
        float reach = length(toLight);
        float facing = dot(normal, toLight) / reach;
        toLight -= normal * (sign(facing) * TESR_PointShadowParams.z * reach);
        float distance = length(toLight) / anchor.w;
        [branch] if (distance < 1.0f) {
            float slot = floor(info * 0.5f);
            float cosine = max(abs(facing), 0.2f);
            float tilt = min(sqrt(1.0f - cosine * cosine) / cosine, 4.0f);
            float compared = distance * (1.0f - 2.0f / TESR_PointShadowData.w * (0.5f + 1.5f * tilt));
            float2 corner, inTile;
            LampAtlasTexel(slot, toLight * float3(-1.0f, -1.0f, 1.0f), corner, inTile);
            float2 q = corner + inTile;
            float2 b = floor(q - 0.5f) + 0.5f;
            float2 f = q - b;
            float blocked = lerp(lerp(LampShadowTexel(b, compared, anchor.w), LampShadowTexel(b + float2(1.0f, 0.0f), compared, anchor.w), f.x),
                                 lerp(LampShadowTexel(b + float2(0.0f, 1.0f), compared, anchor.w), LampShadowTexel(b + float2(1.0f, 1.0f), compared, anchor.w), f.x), f.y);
            visibility = lerp(1.0f, 1.0f - blocked, (info - slot * 2.0f) * saturate(facing * 4.0f));
        }
    }
    return visibility;
}

// The depth at uv for a contact ray, as Bend's: the two texels across the ray's direction (its
// minor axis; along it the nearest), blended -- unless they are farther apart than the thickness,
// two surfaces rather than one, where the nearer of the two by position is taken. Blending across an
// object's outline made an in-between depth there, a thin occluder that cast a faint line past every
// chin, shoulder and table edge.
float LampContactDepth(float2 uv, bool horizontal, float thickness) {
    float2 px = uv / TESR_ReciprocalResolution.xy - 0.5f;   // texel centres at whole numbers
    float2 base = floor(px);
    float2 f = px - base;
    float2 t0 = horizontal ? float2(round(px.x), base.y) : float2(base.x, round(px.y));
    float2 t1 = t0 + (horizontal ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f));
    float w = horizontal ? f.y : f.x;
    float d0 = tex2Dlod(TESR_DepthBuffer, float4((t0 + 0.5f) * TESR_ReciprocalResolution.xy, 0.0f, 0.0f)).x * farZ;
    float d1 = tex2Dlod(TESR_DepthBuffer, float4((t1 + 0.5f) * TESR_ReciprocalResolution.xy, 0.0f, 0.0f)).x * farZ;
    return abs(d0 - d1) > thickness ? (w < 0.5f ? d0 : d1) : lerp(d0, d1, w);
}

// How much of the march toward lampView (view space) is blocked: 1 at an occluder right at the
// pixel, fading to 0 at the ray's end. After Bend Studio's screen-space shadows (as Community
// Shaders have them): no noise, so nothing to blur away -- the same samples every frame, steady and
// sharp. The ray is laid out on screen: sample k sits k + growth * k(k-1)/2 pixels out, the first a
// pixel from the pixel and the spacing widening evenly to reach the ray's end at the last, so where
// things touch it reads every pixel, and the wider spacing is out where the shadow fades (and does
// not show). A ray shorter than its samples spaces them evenly. The depth is read filtered.
// And as Bend's, a ray is no longer on screen than ContactScreenLength: close up, a ray of some units
// crossed hundreds of pixels, its samples far apart, and every hair and lash cast streaks toward the
// lamp (each pixel's samples hitting or missing it in turn). What is larger on screen than that is
// the shadow maps'.
// The ray starts a little off the surface (ContactNormalOffset): from on it, curved and faceted
// surfaces (lips, a nose, eyelids) found their own neighbours just in front of it and drew hard lines.
// And it hardens toward contact: something in front of the ray counts in full only once it is in
// front by more than the lamp's size would let light round it from that far along the ray -- sharp
// where things touch, softening farther from what casts the shadow, as the cubes' PCSS.
float LampContactMarch(float3 origin, float3 lampView, float3 viewNormal) {
    float scale = min(1.0f + origin.z * CONTACT_GROWTH, CONTACT_MAX_SCALE);
    origin += viewNormal * (TESR_LampContactShape.x * scale);
    float3 toLamp = lampView - origin;
    float distance = length(toLamp);
    float rayLength = min(TESR_LampContactData.y * scale, distance * 0.9f);   // never past the lamp
    float3 endPos = origin + toLamp * (rayLength / distance);
    // A ray toward a lamp behind the camera stops short of the near plane.
    float nearEnd = nearZ * 1.5f;
    [flatten] if (endPos.z < nearEnd)
        endPos = origin + (endPos - origin) * saturate((origin.z - nearEnd) / max(origin.z - endPos.z, 0.0001f));
    float thickness = TESR_LampContactData.z * scale;
    float bias = max(thickness * 0.05f, origin.z * 0.002f);

    float4 c0 = mul(float4(origin, 1.0f), TESR_ProjectionTransform);
    float4 c1 = mul(float4(endPos, 1.0f), TESR_ProjectionTransform);
    float2 screenDir = c1.xy / c1.w - c0.xy / c0.w;
    float pixels = length(screenDir * 0.5f / TESR_ReciprocalResolution.xy);
    bool horizontal = abs(screenDir.x / TESR_ReciprocalResolution.x) > abs(screenDir.y / TESR_ReciprocalResolution.y);
    float worldLength = length(endPos - origin);
    float longest = TESR_LampContactExtra.z * max(1.0f / (TESR_ReciprocalResolution.y * 1080.0f), 1.0f);
    [flatten] if (pixels > longest) {   // the end brought in along the ray, on screen
        float t = longest / pixels;
        float cut = t * c0.w / ((1.0f - t) * c1.w + t * c0.w);
        c1 = lerp(c0, c1, cut);
        worldLength *= cut;
        pixels = longest;
    }
    float lampAngle = TESR_LampContactShape.y / distance;   // the lamp's angular radius from here
    float count = TESR_LampContactExtra.x;
    float growth = max(2.0f * (pixels - count) / (count * (count - 1.0f)), 0.0f);
    float spacing = min(pixels / count, 1.0f) / max(pixels, 0.001f);   // a sample's place in pixels to its place along the ray

    float contact = 0.0f;
    [branch] if (pixels >= 0.5f) {   // a ray within its own pixel finds nothing
        [loop] for (float k = 1.0f; k <= count; k += 1.0f) {
            float t = (k + growth * k * (k - 1.0f) * 0.5f) * spacing;   // along the ray on screen, 0-1
            // The same point in clip space (on screen the ray is not spaced as in view space).
            float u = t * c0.w / ((1.0f - t) * c1.w + t * c0.w);
            float4 c = lerp(c0, c1, u);
            float2 sampleUV = c.xy / c.w * float2(0.5f, -0.5f) + 0.5f;
            if (any(saturate(sampleUV) != sampleUV)) break;   // off the screen, and it does not come back
            float delta = c.w - LampContactDepth(sampleUV, horizontal, thickness);
            // Something in front of the ray by more than the bias and less than the thickness: in
            // full once in front by the bias and what the lamp's size lets light round it from this
            // far along (u: the clip-space step is the view-space one), and faded out near the ray's end.
            float soften = bias + u * worldLength * lampAngle;
            float hit = saturate((delta - bias) / soften) * (delta < thickness ? 1.0f : 0.0f);
            contact = max(contact, hit * (1.0f - t * t * t));
            if (contact > 0.99f) break;
        }
    }
    return contact;
}

// One marched lamp: how much of its share light it loses to its contact shadow (beyond what its
// cube shadow took), and in shown, its share as the object shaders left it.
float LampContactLoss(float share, float4 lamp, float4 anchor, float info, float3 origin, float3 viewNormal, float3 worldPos, float3 worldNormal, inout float shown) {
    float loss = 0.0f;
    [branch] if (share > 0.0f) {
        float cube = LampCubeVisibility(info, anchor, worldPos, worldNormal);
        shown -= share * (1.0f - cube);
        float3 lampView = mul(float4(lamp.xyz - TESR_CameraPosition.xyz, 0.0f), TESR_ViewTransform).xyz;
        // Faded in as the surface turns toward the lamp, as the sun's (banding near the terminator).
        float facing = saturate(dot(viewNormal, normalize(lampView - origin)) * 8.0f);
        float fade = 1.0f - smoothstep(TESR_LampContactData.w * 0.8f, TESR_LampContactData.w, origin.z);
        float contact = LampContactMarch(origin, lampView, viewNormal) * TESR_LampContactData.x * facing * fade;
        loss = share * max(cube - (1.0f - contact), 0.0f);
    }
    return loss;
}

float4 LampContactShadow(VSOUT IN) : COLOR0
{
    float2 uv = IN.UVCoord;
    float4 color = tex2D(TESR_PointShadowBuffer, uv);

    if (TESR_LampContactData.x <= 0.0f) return float4(1.0f, color.g, 0.0f, 1.0f);
    float4 originClip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).y, 1.0f);
    float4 originView = mul(originClip, TESR_InvProjectionTransform);
    const float3 origin = originView.xyz / originView.w;
    if (origin.z > TESR_LampContactData.w) return float4(1.0f, color.g, 0.0f, 1.0f);

    float3 viewNormal = tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f;
    float3 worldNormal = normalize(mul(TESR_ViewTransform, float4(viewNormal, 0.0f)).xyz);   // GetWorldNormal
    float3 worldPos = mul(float4(origin, 0.0f), TESR_InvViewTransform).xyz + TESR_CameraPosition.xyz;

    // The pixel's light: the ambient and the cell's light (its direction unknown here: half), then
    // every lamp, keeping the four that light it most, in order.
    float ambient = LampLuma(TESR_ShadowAmbientLight.rgb);
    float cellLight = LampLuma(TESR_ShadowSunLight.rgb);
    float total = ambient * ambient + cellLight * cellLight * 0.5f;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float4 l1 = 0.0f, l2 = 0.0f, l3 = 0.0f, l4 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, a4 = 0.0f;
    float i1 = -1.0f, i2 = -1.0f, i3 = -1.0f, i4 = -1.0f;
    [loop]
    for (int i = 0; i < 24; i++) {
        float4 lamp = TESR_ContactLampPosition[i];
        if (lamp.w <= 0.0f) break;   // filled from the start
        float3 toLamp = lamp.xyz - worldPos;
        float3 d = toLamp / lamp.w;
        // The lamp's light here, linear, as the object shaders falloff (Shaders/Includes/InverseSquare.hlsl).
        float x = saturate(dot(d, d));
        float luma = LampLuma(TESR_ContactLampColor[i].rgb);
        float falloff = (TESR_InverseSquare.x > 0.0f && lamp.w <= TESR_InverseSquare.x) ? max(TESR_InverseSquare.y / (TESR_InverseSquare.y + x) - TESR_InverseSquare.z, 0.0f) * TESR_InverseSquare.w
                                                    : (1.0f - x) * (1.0f - x);
        float share = luma * luma * falloff * saturate(dot(worldNormal, normalize(toLamp)));
        total += share;
        float4 anchor = TESR_ContactLampAnchor[i];
        float info = TESR_ContactLampColor[i].w;
        // Into its place among the four, those below moving down one.
        bool b1 = share > s1, b2 = share > s2, b3 = share > s3, b4 = share > s4;
        s4 = b3 ? s3 : (b4 ? share : s4);  l4 = b3 ? l3 : (b4 ? lamp : l4);  a4 = b3 ? a3 : (b4 ? anchor : a4);  i4 = b3 ? i3 : (b4 ? info : i4);
        s3 = b2 ? s2 : (b3 ? share : s3);  l3 = b2 ? l2 : (b3 ? lamp : l3);  a3 = b2 ? a2 : (b3 ? anchor : a3);  i3 = b2 ? i2 : (b3 ? info : i3);
        s2 = b1 ? s1 : (b2 ? share : s2);  l2 = b1 ? l1 : (b2 ? lamp : l2);  a2 = b1 ? a1 : (b2 ? anchor : a2);  i2 = b1 ? i1 : (b2 ? info : i2);
        s1 = b1 ? share : s1;              l1 = b1 ? lamp : l1;              a1 = b1 ? anchor : a1;              i1 = b1 ? info : i1;
    }
    if (s1 <= 0.0f || total <= 0.0f) return float4(1.0f, color.g, 0.0f, 1.0f);

    // The ContactLamps strongest, in order; a lamp lighting the pixel far less than the first is not
    // worth its march (nor, then, are those after it).
    float shown = total;
    float loss = 0.0f;
    [loop]
    for (int k = 0; k < 4; k++) {
        if (k >= (int)TESR_LampContactExtra.y) break;
        float share = k == 0 ? s1 : (k == 1 ? s2 : (k == 2 ? s3 : s4));
        if (share <= s1 * 0.1f && k > 0) break;
        float4 lamp = k == 0 ? l1 : (k == 1 ? l2 : (k == 2 ? l3 : l4));
        float4 anchor = k == 0 ? a1 : (k == 1 ? a2 : (k == 2 ? a3 : a4));
        float info = k == 0 ? i1 : (k == 1 ? i2 : (k == 2 ? i3 : i4));
        loss += LampContactLoss(share, lamp, anchor, info, origin, viewNormal, worldPos, worldNormal, shown);
    }
    float left = 1.0f - loss / max(shown, total * 0.001f);
    return float4(saturate(left), color.g, 0.0f, 1.0f);
}

// returns a shadow value from darkness setting value (full shadow) to 1 (full light)
float4 Shadow(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;

	// Sample Screen Space shadows
	float4 Shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
    Shadow = pow(Shadow, TESR_ShadowScreenSpaceData.w);

	if (!TESR_ShadowFade.y) return Shadow; // disable shadow maps if ShadowFade.y == 0 (setting for shadow map disabled)

	// Sample shadows from shadowmaps.
	//
	// Skipped when the forward path is doing the cascade lookup: ObjectTemplate.hlsl and
	// friends then apply the result to the sun term alone, which this screen-space composite
	// cannot do -- it can only scale the finished pixel, dimming ambient, emittance and
	// specular along with the sun.
	//
	// Screen-space contact shadows (already in Shadow.r) and point lights (Shadow.g) stay
	// deferred either way; the forward path only takes over the cascade lookup.
	//
	// FORWARD_SHADOWS decides whether the forward code was COMPILED INTO the game shaders;
	// TESR_ShadowForwardData.x decides whether it is RUNNING. When forward is compiled in we
	// must branch at runtime rather than compile this out, so that turning the setting off
	// mid-session hands the cascades back here in the same frame -- game shaders cannot be
	// recompiled at runtime, so a macro alone would leave neither path drawing shadows.
#if FORWARD_SHADOWS
	if (!TESR_ShadowForwardData.x) return Shadow;
#endif

	// Only reached when the cascades are ours, so the position and normal are only paid for
	// then. tex2Dlod for the normal: it is sampled after the dynamic returns above.
    float viewDepth;
    float4 worldPos = reconstructWorldPosition(uv, viewDepth);
	float3 normal = mul(TESR_ViewTransform, float4(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f, 1.0f)).xyz;   // GetWorldNormal
	Shadow.r = min(Shadow.r, GetLightAmount(worldPos, normal)); // darkest of screenspace & sun

	return Shadow;
}


technique {

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadow();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlur(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, CONTACT_BLUR_END);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlur(TESR_PointShadowBuffer, OffsetMaskV, TESR_ShadowScreenSpaceData.y, 3500, CONTACT_BLUR_END);
	}

    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Shadow();
    }

}

// Lamps' contact shadows (indoors): rendered by ShaderManager into TESR_PointShadowBuffer, through
// SunShadowsEffect's alternating targets; ShadowsInteriors.fx technique 1 applies them. Technique 1
// as marched (sharp); technique 2 blurred as well (ContactBlur).
technique {

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 LampContactShadow();
	}

}

technique {

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 LampContactShadow();
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlur(TESR_PointShadowBuffer, OffsetMaskH, TESR_ShadowScreenSpaceData.y, 3500, LAMP_CONTACT_END_H);
	}

	pass {
		VertexShader = compile vs_3_0 FrameVS();
	 	PixelShader = compile ps_3_0 DepthBlur(TESR_PointShadowBuffer, OffsetMaskV, TESR_ShadowScreenSpaceData.y, 3500, LAMP_CONTACT_END);
	}

}
