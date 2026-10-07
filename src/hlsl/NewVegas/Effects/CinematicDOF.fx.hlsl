// Cinematic depth of field: a thin-lens circle of confusion, gathered bokeh with separate near and
// far fields, and autofocus on world depth.
//
// The gather follows the scatter-as-gather design Unity's Post-Processing Stack v2 shipped
// (Keijiro Takahashi's Kino Bokeh lineage), itself in the family of Jimenez, "Next Generation Post
// Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014). It is written from the algorithm,
// not copied: the kernel here is a golden-angle spiral generated in the loop rather than a table.
//
// Pipeline (CinematicDOFEffect::Render drives it; half = half resolution):
//   Focus       1x1   autofocus distance, eased over time         FocusA/B ping-pong
//   Prefilter   half  2x2 downsample: colour + signed CoC          -> HalfA
//   Bokeh       half  disc gather, near and far fields apart       HalfA -> HalfB
//   Postfilter  half  tent filter to hide the gather's sampling    HalfB -> HalfA
//   Combine     full  blend the blurred layers over the sharp frame
//
// What makes it read as a camera rather than a blur filter:
//
//  - Thin-lens CoC. Blur is f^2 / (N (zf - f)) * (z - zf) / z, from a focal length, f-stop and focus
//    distance. Behind the focus plane it levels off toward a maximum; in front it keeps growing. That
//    asymmetry is what an eye reads as optics. The CoC is signed: negative near, positive far.
//
//  - Near field spreads past its own outline. The previous NVR DoF blended each pixel by its OWN
//    near-blur amount, so an out-of-focus foreground object ended in a sharp edge. Here every pixel
//    gathers over the full kernel, and a sample from a near-field object contributes wherever its
//    CoC reaches -- so the blur of a foreground object bleeds over the background behind it, as it
//    does through a real lens. The far field is the opposite: a sample only counts if it AND the
//    centre are out of focus (min of the two CoCs), so a sharp foreground never haloes the blur
//    behind it.
//
//  - Bokeh from highlights. This runs after tonemapping, so there is no HDR to make bright points
//    swell into discs. HighlightBoost inverts a Reinhard curve on the way in -- c / (1 - k*max(c))
//    -- so bright pixels outweigh their neighbours through the gather, and the exact inverse,
//    c / (1 + k*max(c)), puts the range back afterwards. Dark and mid tones are nearly untouched.
//    HighlightThreshold starts the curve higher up, so only the brightest pixels are boosted.
//
//  - Aperture shape. The gather's disc can be reshaped the way a real lens shapes its bokeh:
//    ApertureBlades turns it into a polygon (BladeRotation turns it, BladeCurvature rounds its sides
//    back toward a circle), Anamorphic stretches it into an oval, CatsEye clips discs toward the
//    frame edges into lemon shapes (optical vignetting), and RingBrightness moves light toward the
//    disc's rim (older, "soap bubble" lenses) or its centre (smooth, "creamy" bokeh).
//    BokehShape swaps the aperture for a star, a donut (a mirror lens's central obstruction), a heart
//    or a cross, with ShapeDetail setting the star's point depth, the donut's hole or the cross's arms.
//
//    Two ways of shaping, chosen per shape. Shapes whose outline can be seen whole from their centre
//    (polygon, star, cross) move the samples: each sample's distance is scaled to the outline in its
//    direction, so all of them land inside and none is wasted. The donut's hole and the heart, whose
//    centre is not where its outline is simplest to describe, keep the round disc's samples and mask
//    out the ones that fall outside, using a distance to the edge so the cut is as soft as the rim's.
//
//  - Autofocus on WORLD depth. The combined depth buffer contains the first-person weapon, which can
//    sit across the centre of the screen -- autofocus on that would focus on the gun and blur the
//    world. The world-only depth buffer is decoded with CombineDepth's own conversion.

#define PI 3.14159265f

float4 TESR_ReciprocalResolution;   // x: 1 / width, y: 1 / height
float4 TESR_GameTime;               // w: frame time, seconds
float4 TESR_CinematicDOFLens;       // x: lens coefficient f^2 * aspect / (N * 36mm), y: max CoC (screen heights), z: highlight boost, w: weapon blur
float4 TESR_CinematicDOFFocus;      // x: manual focus distance (units), y: autofocus (0/1), z: focus easing this frame, w: previous focus valid (0/1)
float4 TESR_CinematicDOFData;       // x: effect strength 0-1 (fades in and out), y: min focus distance (units), z: focal length (mm), w: debug view
float4 TESR_CinematicDOFNear;       // x: near focus range (units), y: near blur strength
float4 TESR_CinematicDOFAperture;   // x: blades (below 3 = round), y: blade rotation (radians), z: blade curvature 0-1, w: anamorphic squeeze
float4 TESR_CinematicDOFBokeh;      // x: cat's eye 0-1, y: ring brightness -1 to 1, z: highlight threshold 0-0.95
float4 TESR_CinematicDOFShape;      // x: bokeh shape (0 aperture, 1 star, 2 donut, 3 heart, 4 cross), y: shape detail 0-1, z: postfilter radius (half-res texels)
float4 TESR_CinematicDOFWeapon;     // x: weapon focus distance (units), y: weapon blur range (units), z: weapon max CoC (screen heights), w: weapon DoF strength 0-1

sampler2D TESR_SourceBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferWorld : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
// s4-s6 are bound explicitly per pass by CinematicDOFEffect::Render, so no pass ever samples the
// texture it is rendering into. The names are what SetCT binds them to initially.
sampler2D TESR_CinematicDOFFocusA : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_CinematicDOFHalfA : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_CinematicDOFHalfB : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

#include "Includes/Depth.hlsl"

static const float lensCoeff = TESR_CinematicDOFLens.x;
static const float maxCoC = TESR_CinematicDOFLens.y;
static const float highlightBoost = TESR_CinematicDOFLens.z;
static const float weaponBlur = TESR_CinematicDOFLens.w;
static const float manualFocus = TESR_CinematicDOFFocus.x;
static const float autoFocus = TESR_CinematicDOFFocus.y;
static const float focusEasing = TESR_CinematicDOFFocus.z;
static const float focusValid = TESR_CinematicDOFFocus.w;
static const float strength = TESR_CinematicDOFData.x;
static const float minFocus = TESR_CinematicDOFData.y;
static const float focalLength = TESR_CinematicDOFData.z;
static const float debugView = TESR_CinematicDOFData.w;
static const float nearFocusRange = TESR_CinematicDOFNear.x;
static const float nearBlurStrength = TESR_CinematicDOFNear.y;
static const float bladeCount = TESR_CinematicDOFAperture.x;
static const float bladeRotation = TESR_CinematicDOFAperture.y;
static const float bladeCurvature = TESR_CinematicDOFAperture.z;
static const float anamorphic = TESR_CinematicDOFAperture.w;
static const float catsEye = TESR_CinematicDOFBokeh.x;
static const float ringBrightness = TESR_CinematicDOFBokeh.y;
static const float highlightThreshold = TESR_CinematicDOFBokeh.z;
static const float bokehShape = TESR_CinematicDOFShape.x;
static const float shapeDetail = TESR_CinematicDOFShape.y;
static const float postfilterRadius = TESR_CinematicDOFShape.z;
static const float weaponFocus = TESR_CinematicDOFWeapon.x;
static const float weaponRange = TESR_CinematicDOFWeapon.y;
static const float weaponMaxCoC = TESR_CinematicDOFWeapon.z;
static const float weaponStrength = TESR_CinematicDOFWeapon.w;

// BokehShape values. Compared with a half-step margin, since they arrive as floats.
static const float SHAPE_STAR = 1.0f;
static const float SHAPE_DONUT = 2.0f;
static const float SHAPE_HEART = 3.0f;
static const float SHAPE_CROSS = 4.0f;
bool IsShape(float shape) { return abs(bokehShape - shape) < 0.5f; }

// One game unit is 0.5625 in, 14.2875 mm (roughly 70 units to the metre).
static const float UNIT_MM = 14.2875f;

// Golden-angle spiral: sample i sits at radius sqrt((i + 0.5) / N) and angle i * 137.5 degrees, which
// covers a disc evenly for any N. The direction is advanced by a fixed rotation rather than by sin and
// cos of the angle; the angle itself is tracked alongside for the aperture shape.
//
// N is BokehQuality's sample count, a compile-time constant of each Bokeh technique (48, 96, 160)
// passed as a uniform argument -- a runtime loop bound is the construct that has crashed the D3DX
// compiler in this codebase before. The count matters more than it looks: a highlight's bokeh is
// drawn by exactly these N samples, so at 48 over a full MaxBlur disc it is a cloud of separate dots
// and a polygon, star or heart does not read. 96 shows the shapes; 160 fills them.
static const float GOLDEN_COS = -0.7373688f;   // cos(2.3999632)
static const float GOLDEN_SIN = 0.6754903f;    // sin(2.3999632)
static const float GOLDEN_ANGLE = 2.3999632f;

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

float Max3(float3 c)
{
	return max(c.r, max(c.g, c.b));
}

// Inverse Reinhard on the way in, and its exact inverse on the way out. See the header.
//
// Applied to the brightest channel m, above the threshold t only: m -> t + x / (1 - k'x), x = m - t,
// with k' = k / (1 - t) so full white is boosted by the same amount whatever the threshold. At t = 0
// this is exactly c / (1 - k*m). The colour is scaled by the change in m, so hue is kept.
float3 ExpandHighlights(float3 c)
{
	float m = Max3(c);
	float x = max(m - highlightThreshold, 0.0f);
	float k = highlightBoost / (1.0f - highlightThreshold);
	float expanded = highlightThreshold + x / max(1.0f - k * x, 0.05f);
	return m > highlightThreshold ? c * (expanded / max(m, 1e-5f)) : c;
}

float3 CompressHighlights(float3 c)
{
	float m = Max3(c);
	float x = max(m - highlightThreshold, 0.0f);
	float k = highlightBoost / (1.0f - highlightThreshold);
	float compressed = highlightThreshold + x / (1.0f + k * x);
	return m > highlightThreshold ? c * (compressed / max(m, 1e-5f)) : c;
}

// Radius of the bokeh outline in the direction angle, relative to the circle it fits in: 1 for a round
// disc. See BokehPS for what segment and edge hold.
//
// Polygon and star are one construction: the outline runs straight from a point at radius 1 to an
// inner corner half a segment round, and edge is that corner minus the point. A polygon is the star
// whose inner corners sit on the middles of its sides. The cross has four arms of half-width armWidth
// ending at 1, scaled down to fit the circle. BladeCurvature bends any of them back toward the circle.
float OutlineRadius(float angle, float segment, float2 edge, float armWidth, bool isRound)
{
	float a = frac((angle - bladeRotation) / segment) * segment;
	float b = min(a, segment - a);   // angle from the nearest point, 0 to half a segment
	float starOutline = edge.y / (cos(b) * edge.y - sin(b) * edge.x);
	float crossOutline = min(1.0f / cos(b), armWidth / max(sin(b), 1e-4f)) * rsqrt(1.0f + armWidth * armWidth);
	float outline = lerp(IsShape(SHAPE_CROSS) ? crossOutline : starOutline, 1.0f, bladeCurvature);
	return isRound ? 1.0f : outline;
}

// Signed distance to a heart with its tip at the origin and lobes up to y = 1.1, negative inside
// (Inigo Quilez's exact heart distance). y is up.
float HeartDistance(float2 p)
{
	p.x = abs(p.x);
	float lobe = length(p - float2(0.25f, 0.75f)) - 0.35355339f;   // sqrt(2) / 4
	float m = 0.5f * max(p.x + p.y, 0.0f);
	float2 toTop = p - float2(0.0f, 1.0f);
	float2 toSide = p - m;
	float body = sqrt(min(dot(toTop, toTop), dot(toSide, toSide))) * sign(p.x - p.y);
	return p.y + p.x > 1.0f ? lobe : body;
}

// How much of a sample survives the masked shapes, from where the receiving pixel sits inside the
// sample's disc (inDisc, 1 at the rim) and the disc's size (coc, screen heights). Softened over margin.
float ShapeMask(float2 inDisc, float coc, float margin)
{
	// Donut: a round hole of ShapeDetail's size in the middle of the aperture.
	float hole = lerp(0.25f, 0.75f, shapeDetail);
	float edge = length(inDisc) - hole;

	// Heart: the unit disc mapped onto the heart's bounding circle (radius 0.64 about (0, 0.63)). The
	// gather samples a disc's opposite side and screen y points down; the two flips cancel for a
	// shape symmetric left to right, so inDisc is already the right way up.
	float s, c;
	sincos(-bladeRotation, s, c);
	float2 turned = float2(inDisc.x * c - inDisc.y * s, inDisc.x * s + inDisc.y * c);
	float heart = -HeartDistance(turned * 0.64f + float2(0.0f, 0.63f)) / 0.64f;

	float inside = IsShape(SHAPE_DONUT) ? edge : (IsShape(SHAPE_HEART) ? heart : 1.0f);
	return saturate((inside * coc + margin) / margin);
}

float FocusDistance()
{
	return tex2D(TESR_CinematicDOFFocusA, float2(0.5f, 0.5f)).x;
}

// World-only view depth, decoded exactly as CombineDepth does it (reversed depth: 0 is the far plane).
float WorldViewZ(float2 uv)
{
	float d = tex2D(TESR_DepthBufferWorld, uv).x;
	return nearZ * farZ / (nearZ + d * (farZ - nearZ));
}

// Thin-lens blur for a surface at depth when focused at plane, in screen heights.
float ThinLensCoC(float depth, float plane)
{
	float planeMM = plane * UNIT_MM;
	return lensCoeff * (depth - plane) / (max(depth, 1.0f) * max(planeMM - focalLength, focalLength));
}

// Signed circle of confusion in screen heights: negative in front of the focus plane, positive behind.
//
// The two sides are computed apart so the foreground can be shaped on its own, the way engines expose
// a near transition region and a near blur size:
//  - NearFocusRange extends the in-focus zone toward the camera. In front of the focus plane the lens is
//    treated as focused at (focus - range) instead, and clamped so nothing between that plane and the
//    real focus plane blurs. The CoC is exactly zero at the new boundary, so the zone's edge is
//    continuous rather than a step.
//  - NearBlurStrength scales only the foreground result. 0 gives background-only depth of field.
// Behind the focus plane nothing changes.
float CircleOfConfusion(float depth, float focus)
{
	float nearPlane = max(focus - nearFocusRange, 1.0f);
	float nearCoC = min(ThinLensCoC(depth, nearPlane), 0.0f) * nearBlurStrength;
	float farCoC = max(ThinLensCoC(depth, focus), 0.0f);
	float coc = depth < focus ? nearCoC : farCoC;
	return clamp(coc * strength, -maxCoC, maxCoC);
}

// Weapon depth of field: blur on the first-person weapon alone, growing toward the eye, the way a
// camera focused past the gun renders the receiver and the scope's eyepiece soft and the muzzle crisp.
//
// Its own focus rather than the lens's. The weapon sits 5-60 units from the eye; with the lens focused
// out in the world every part of it is so far in front of the focus plane that all of it hits the
// same maximum blur, which is why WeaponBlur can only blur the whole gun evenly. Here the gun is sharp
// beyond WeaponFocusDistance and ramps to WeaponMaxBlur over WeaponBlurRange nearer than that -- a
// straight ramp, since at these distances only its shape matters and a thin lens would saturate.
// Negative: near field, so its blur spreads softly over the world just behind the gun's edges.
float WeaponCoC(float depth)
{
	return -weaponMaxCoC * saturate((weaponFocus - depth) / max(weaponRange, 1.0f)) * weaponStrength;
}

// CoC at a full-resolution pixel. Where weapon depth of field applies, the weapon takes the stronger
// of the world lens's blur (scaled by WeaponBlur) and its own -- both near-field, so the stronger is
// the more negative. Elsewhere on the weapon, and with weapon depth of field off, it is exactly the
// lens's blur scaled by WeaponBlur, as before.
float PixelCoC(float2 uv, float focus)
{
	float depth = readDepth(uv);
	float coc = CircleOfConfusion(depth, focus);
	bool isViewModel = tex2D(TESR_DepthBufferViewModel, uv).x > 0.0f;
	float lens = coc * weaponBlur;
	float own = WeaponCoC(depth);
	float weapon = own < 0.0f ? max(min(lens, own), -maxCoC) : lens;
	return isViewModel ? weapon : coc;
}

// ---- Focus (1x1) ----
// The nearest world surface among five taps around the screen centre, so what is under the crosshair
// wins over the background behind it, eased toward over time the way a real autofocus hunts.
float4 FocusPS(VSOUT IN) : COLOR0
{
	// Sampled unconditionally and selected after: texture fetches that need gradients are not
	// allowed inside dynamic flow control in ps_3_0.
	float nearest = WorldViewZ(float2(0.5f, 0.5f));
	nearest = min(nearest, WorldViewZ(float2(0.48f, 0.5f)));
	nearest = min(nearest, WorldViewZ(float2(0.52f, 0.5f)));
	nearest = min(nearest, WorldViewZ(float2(0.5f, 0.48f)));
	nearest = min(nearest, WorldViewZ(float2(0.5f, 0.52f)));
	float target = max(autoFocus > 0.5f ? nearest : manualFocus, minFocus);

	float previous = FocusDistance();
	float focus = focusValid > 0.5f ? lerp(previous, target, focusEasing) : target;
	return float4(focus, focus, 0.0f, 1.0f);
}

// ---- Prefilter (half) ----
// The quad's UVs carry a half-full-resolution-texel offset (ShaderManager::CreateFrameVertex), so at
// half resolution IN.UVCoord lands on the centre of the first full-res texel of this pixel's 2x2
// block, and the other three are one texel right and/or down.
float4 PrefilterPS(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float2 texel = TESR_ReciprocalResolution.xy;
	float focus = FocusDistance();

	float2 uv0 = uv;
	float2 uv1 = uv + float2(texel.x, 0.0f);
	float2 uv2 = uv + float2(0.0f, texel.y);
	float2 uv3 = uv + texel;

	float3 c0 = ExpandHighlights(tex2D(TESR_SourceBuffer, uv0).rgb);
	float3 c1 = ExpandHighlights(tex2D(TESR_SourceBuffer, uv1).rgb);
	float3 c2 = ExpandHighlights(tex2D(TESR_SourceBuffer, uv2).rgb);
	float3 c3 = ExpandHighlights(tex2D(TESR_SourceBuffer, uv3).rgb);

	float coc0 = PixelCoC(uv0, focus);
	float coc1 = PixelCoC(uv1, focus);
	float coc2 = PixelCoC(uv2, focus);
	float coc3 = PixelCoC(uv3, focus);

	// Weight by blur, so in-focus pixels do not leak into the blurred average of their block.
	float w0 = abs(coc0) + 1e-5f;
	float w1 = abs(coc1) + 1e-5f;
	float w2 = abs(coc2) + 1e-5f;
	float w3 = abs(coc3) + 1e-5f;
	float3 color = (c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) / (w0 + w1 + w2 + w3);

	// Keep the strongest blur in the block, near or far, with its sign.
	float cocMin = min(min(coc0, coc1), min(coc2, coc3));
	float cocMax = max(max(coc0, coc1), max(coc2, coc3));
	float coc = -cocMin > cocMax ? cocMin : cocMax;

	// Fade in-focus colour to black here: the gather must not pick up sharp pixels, and the Combine
	// pass takes in-focus areas from the full-resolution frame anyway.
	color *= smoothstep(0.0f, texel.y * 2.0f, abs(coc));
	return float4(color, coc);
}

// ---- Bokeh (half) ----
float4 BokehPS(VSOUT IN, uniform int sampleCount) : COLOR0
{
	float2 uv = IN.UVCoord + 0.5f * TESR_ReciprocalResolution.xy;   // half-res texel centre
	float halfTexel = TESR_ReciprocalResolution.y * 2.0f;           // one half-res texel, screen heights
	float margin = halfTexel * 2.0f;
	float heightToU = TESR_ReciprocalResolution.x / TESR_ReciprocalResolution.y;   // height / width

	float4 center = tex2D(TESR_CinematicDOFHalfA, uv);
	float4 farAcc = 0.0f;
	float4 nearAcc = 0.0f;
	float nearCoverage = 0.0f;
	float areaSum = 0.0f;
	float2 direction = float2(1.0f, 0.0f);
	float angle = 0.0f;

	// Bokeh shape, fixed for the frame. Anamorphic keeps the disc's area: taller by sqrt(squeeze),
	// narrower by the same.
	//  - A star has ApertureBlades points (5 below 3), inner corners from 0.85 down to 0.3 of the way
	//    out as ShapeDetail rises. A polygon's inner corners are its side middles, cos(half segment).
	//  - A cross has four segments and arms from 0.12 to 0.5 of the way out.
	//  - The heart is masked from a full disc; the aperture and donut are round below 3 blades.
	bool isStar = IsShape(SHAPE_STAR);
	bool isCross = IsShape(SHAPE_CROSS);
	float points = bladeCount >= 3.0f ? bladeCount : (isStar ? 5.0f : 3.0f);
	float segment = isCross ? 0.5f * PI : 2.0f * PI / points;
	float halfSegment = 0.5f * segment;
	float inner = isStar ? lerp(0.85f, 0.3f, shapeDetail) : cos(halfSegment);
	float2 edge = float2(inner * cos(halfSegment) - 1.0f, inner * sin(halfSegment));
	float armWidth = lerp(0.12f, 0.5f, shapeDetail);
	bool isRound = IsShape(SHAPE_HEART) || (!isStar && !isCross && bladeCount < 3.0f);
	bool masked = IsShape(SHAPE_DONUT) || IsShape(SHAPE_HEART);
	float2 squeeze = float2(rsqrt(anamorphic), sqrt(anamorphic));

	// Cat's eye: this pixel's position from the frame centre, 1 at the corners. Each disc is cut by a
	// copy of itself shifted by this much, so discs stay round in the middle and become lemon-shaped,
	// long side along the edge, toward the corners.
	float2 fromCentre = (uv - 0.5f) * 2.0f * float2(1.0f / heightToU, 1.0f);
	float2 eyeShift = fromCentre / length(float2(1.0f / heightToU, 1.0f)) * catsEye;

	[loop]
	for (int i = 0; i < sampleCount; i++) {
		// radius is how far this sample is from the centre in aperture terms -- the blur a disc needs to
		// reach it. The offset actually sampled is that, reshaped by the aperture.
		float radius = sqrt(((float)i + 0.5f) / (float)sampleCount) * maxCoC;
		float outline = OutlineRadius(angle, segment, edge, armWidth, isRound);
		float2 offset = direction * radius * outline * squeeze;

		// Every direction gets the same number of samples, but a long one (a star's point, a cross's
		// arm) spreads them over more area than a short one, so unweighted the shape comes out bright in
		// the middle and dim at its tips. Weighting by the area each sample stands for -- the outline
		// radius squared -- evens it out. 1 for a round disc, so that case is unchanged.
		float area = outline * outline;
		areaSum += area;
		float4 s = tex2Dlod(TESR_CinematicDOFHalfA, float4(uv + float2(offset.x * heightToU, offset.y), 0.0f, 0.0f));

		// Where this pixel sits inside the sample's own disc: 0 at its centre, 1 at its rim.
		float sourceCoC = max(abs(s.a), 1e-5f);
		float2 inDisc = direction * (radius / sourceCoC);

		// Cat's eye: outside the shifted copy of the disc is cut away, softened over the same margin.
		// Exactly 1 when off, so the edge falloff below is untouched.
		float eye = saturate(((1.0f - length(inDisc - eyeShift)) * sourceCoC + margin) / margin);
		eye = catsEye > 0.0f ? eye : 1.0f;
		eye *= masked ? ShapeMask(inDisc, sourceCoC, margin) : 1.0f;

		// Ring brightness: move light toward the rim (positive) or centre (negative). 2r^2 - 1 averages
		// zero over a disc, so the total light is unchanged.
		float rim = saturate(radius / sourceCoC);
		float ring = 1.0f + ringBrightness * (2.0f * rim * rim - 1.0f);

		// Far field: only as much blur as both this sample and the centre have, so an in-focus
		// foreground never spreads into the background's blur.
		float farCoC = max(min(center.a, s.a), 0.0f);
		float farWeight = saturate((farCoC - radius + margin) / margin) * eye;

		// Near field: the sample's own blur decides its reach, so it spreads past its outline.
		float nearWeight = saturate((-s.a - radius + margin) / margin) * eye;
		nearWeight *= step(halfTexel, -s.a);

		farAcc += float4(s.rgb, 1.0f) * farWeight * ring * area;
		nearAcc += float4(s.rgb, 1.0f) * nearWeight * ring * area;
		nearCoverage += nearWeight * area;

		direction = float2(direction.x * GOLDEN_COS - direction.y * GOLDEN_SIN,
		                   direction.x * GOLDEN_SIN + direction.y * GOLDEN_COS);
		angle += GOLDEN_ANGLE;
	}

	// A masked shape can leave a slightly blurred pixel with no sample inside its donut or heart; it
	// keeps its own colour rather than going black.
	farAcc.rgb = farAcc.a > 1e-4f ? farAcc.rgb / max(farAcc.a, 1e-4f) : center.rgb;
	nearAcc.rgb /= nearAcc.a + (nearAcc.a == 0.0f ? 1.0f : 0.0f);

	// How much of this pixel the near field covers: the weights' total, as a fraction of the disc.
	// Counted without the ring weighting, which moves light around the disc but does not change its size,
	// and relative to the total area so a shape's coverage means the same as a disc's.
	float nearAlpha = saturate(nearCoverage * PI / areaSum);
	return float4(lerp(farAcc.rgb, nearAcc.rgb, nearAlpha), nearAlpha);
}

// ---- Postfilter (half) ----
// A 3x3 tent (1 2 1 by 1 2 1, over 16) smoothing the gather's sampling pattern, its taps
// PostfilterRadius half-res texels apart. At 1 every tap lands on a texel centre, so this is exactly
// the 9-texel tent the effect has always used; 0 collapses it onto the pixel (no smoothing); above 1
// it widens, the bilinear sampler blending between texels, which hides a sparse gather's dots at the
// cost of softening bokeh edges and shapes.
float4 PostfilterPS(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord + 0.5f * TESR_ReciprocalResolution.xy;   // half-res texel centre
	float2 d = TESR_ReciprocalResolution.xy * 2.0f * postfilterRadius;
	float4 acc = tex2D(TESR_CinematicDOFHalfB, uv) * 4.0f;
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2(-d.x, 0.0f)) * 2.0f;
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2( d.x, 0.0f)) * 2.0f;
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2(0.0f, -d.y)) * 2.0f;
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2(0.0f,  d.y)) * 2.0f;
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2(-d.x, -d.y));
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2( d.x, -d.y));
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2(-d.x,  d.y));
	acc += tex2D(TESR_CinematicDOFHalfB, uv + float2( d.x,  d.y));
	acc /= 16.0f;
	return float4(CompressHighlights(acc.rgb), acc.a);
}

// ---- Combine (full) ----
float4 CombinePS(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float coc = PixelCoC(uv, FocusDistance());
	float3 color = tex2D(TESR_SourceBuffer, uv).rgb;
	float4 dof = tex2D(TESR_CinematicDOFHalfA, uv);

	// Far field from this pixel's own full-resolution CoC; near field from how much the gather says
	// foreground blur covers it. Combined as two layers of coverage.
	float texel = TESR_ReciprocalResolution.y;
	float farAlpha = smoothstep(texel * 2.0f, texel * 4.0f, coc);
	float alpha = farAlpha + dof.a - farAlpha * dof.a;
	float3 result = lerp(color, dof.rgb, alpha);

	// DebugView 1: red = far blur, blue = near blur, full brightness at MaxBlur; black = in focus.
	float3 cocView = float3(saturate(coc / maxCoC), 0.0f, saturate(-coc / maxCoC));

	// DebugView 2, weapon: the world as a dim grey backdrop, the first-person weapon from green
	// (sharp) to blue (at WeaponMaxBlur). Shows where weapon depth of field starts along the gun
	// while tuning WeaponFocusDistance and WeaponBlurRange.
	float gray = dot(color, float3(0.299f, 0.587f, 0.114f)) * 0.3f;
	bool isViewModel = tex2D(TESR_DepthBufferViewModel, uv).x > 0.0f;
	float weaponAmount = saturate(-coc / max(weaponMaxCoC, 1e-5f));
	float3 weaponView = isViewModel ? lerp(float3(0.0f, 0.6f, 0.0f), float3(0.0f, 0.2f, 1.0f), weaponAmount) : gray;

	// DebugView 3, focus: the dim backdrop with the surfaces at the current focus distance (within
	// 3%) in yellow, and the five autofocus taps as red dots. Shows what autofocus has locked onto.
	float focus = FocusDistance();
	float depth = readDepth(uv);
	float3 focusView = abs(depth - focus) < focus * 0.03f ? float3(1.0f, 0.9f, 0.1f) : gray;
	float2 pixel = uv / TESR_ReciprocalResolution.xy;
	float2 screen = 1.0f / TESR_ReciprocalResolution.xy;
	float nearestTap = length(pixel - screen * float2(0.5f, 0.5f));
	nearestTap = min(nearestTap, length(pixel - screen * float2(0.48f, 0.5f)));
	nearestTap = min(nearestTap, length(pixel - screen * float2(0.52f, 0.5f)));
	nearestTap = min(nearestTap, length(pixel - screen * float2(0.5f, 0.48f)));
	nearestTap = min(nearestTap, length(pixel - screen * float2(0.5f, 0.52f)));
	focusView = nearestTap < 3.0f ? float3(1.0f, 0.0f, 0.0f) : focusView;

	result = debugView > 0.5f ? cocView : result;
	result = debugView > 1.5f ? weaponView : result;
	result = debugView > 2.5f ? focusView : result;
	return float4(result, 1.0f);
}

technique Focus
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 FocusPS();
	}
}

technique Prefilter
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 PrefilterPS();
	}
}

// One Bokeh technique per BokehQuality level; CinematicDOFEffect::Render picks by name.
technique Bokeh48
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BokehPS(48);
	}
}

technique Bokeh96
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BokehPS(96);
	}
}

technique Bokeh160
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BokehPS(160);
	}
}

technique Postfilter
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 PostfilterPS();
	}
}

technique Combine
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CombinePS();
	}
}
